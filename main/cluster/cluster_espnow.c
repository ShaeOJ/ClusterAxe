/**
 * @file cluster_espnow.c
 * @brief ESP-NOW transport implementation for ClusterAxe
 *
 * @author ClusterAxe Project
 * @license GPL-3.0
 */

#include "cluster_espnow.h"
#include "cluster_config.h"

#if CLUSTER_ENABLED && defined(CONFIG_CLUSTER_TRANSPORT_ESPNOW)

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "cluster_espnow";

// ============================================================================
// Static Variables
// ============================================================================

static cluster_espnow_state_t g_espnow = {0};

// Broadcast MAC address
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ============================================================================
// Forward Declarations
// ============================================================================

static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                           const uint8_t *data, int len);
static void espnow_rx_task(void *pvParameters);
static void espnow_discovery_task(void *pvParameters);
static int64_t get_time_ms(void);

// ============================================================================
// Initialization
// ============================================================================

esp_err_t cluster_espnow_init(void)
{
    if (g_espnow.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing ESP-NOW transport");

    // Get our MAC address
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, g_espnow.self_mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MAC address: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Self MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             g_espnow.self_mac[0], g_espnow.self_mac[1],
             g_espnow.self_mac[2], g_espnow.self_mac[3],
             g_espnow.self_mac[4], g_espnow.self_mac[5]);

    // Initialize ESP-NOW
    ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register callbacks
    ret = esp_now_register_send_cb(espnow_send_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register send callback: %s", esp_err_to_name(ret));
        esp_now_deinit();
        return ret;
    }

    ret = esp_now_register_recv_cb(espnow_recv_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register recv callback: %s", esp_err_to_name(ret));
        esp_now_deinit();
        return ret;
    }

    // Set Primary Master Key if encryption enabled
#if CONFIG_CLUSTER_ESPNOW_ENCRYPT
    g_espnow.encrypt_enabled = true;
    memset(g_espnow.pmk, 0, ESPNOW_PMK_LEN);
    strncpy((char *)g_espnow.pmk, CONFIG_CLUSTER_ESPNOW_PMK, ESPNOW_PMK_LEN);
    ret = esp_now_set_pmk(g_espnow.pmk);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set PMK: %s", esp_err_to_name(ret));
    }
#else
    g_espnow.encrypt_enabled = false;
#endif

    // Get channel
    uint8_t primary_channel;
    wifi_second_chan_t secondary_channel;
    esp_wifi_get_channel(&primary_channel, &secondary_channel);
    g_espnow.channel = primary_channel;
    ESP_LOGI(TAG, "Using WiFi channel %d", g_espnow.channel);

    // Create mutex
    g_espnow.peers_mutex = xSemaphoreCreateMutex();
    if (!g_espnow.peers_mutex) {
        ESP_LOGE(TAG, "Failed to create peers mutex");
        esp_now_deinit();
        return ESP_ERR_NO_MEM;
    }

    // Create RX queue
    g_espnow.rx_queue = xQueueCreate(16, sizeof(cluster_espnow_rx_msg_t));
    if (!g_espnow.rx_queue) {
        ESP_LOGE(TAG, "Failed to create RX queue");
        vSemaphoreDelete(g_espnow.peers_mutex);
        esp_now_deinit();
        return ESP_ERR_NO_MEM;
    }

    // Create RX task
    BaseType_t task_ret = xTaskCreate(espnow_rx_task,
                                       "espnow_rx",
                                       4096,
                                       NULL,
                                       5,
                                       &g_espnow.rx_task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create RX task");
        vQueueDelete(g_espnow.rx_queue);
        vSemaphoreDelete(g_espnow.peers_mutex);
        esp_now_deinit();
        return ESP_ERR_NO_MEM;
    }

    // Add broadcast peer for discovery
    esp_now_peer_info_t broadcast_peer = {
        .channel = g_espnow.channel,
        .ifidx = WIFI_IF_STA,
        .encrypt = false
    };
    memcpy(broadcast_peer.peer_addr, BROADCAST_MAC, 6);

    ret = esp_now_add_peer(&broadcast_peer);
    if (ret != ESP_OK && ret != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "Failed to add broadcast peer: %s", esp_err_to_name(ret));
    }

    // Initialize peer array
    memset(g_espnow.peers, 0, sizeof(g_espnow.peers));
    g_espnow.peer_count = 0;

    g_espnow.initialized = true;
    ESP_LOGI(TAG, "ESP-NOW initialized successfully");

    return ESP_OK;
}

void cluster_espnow_deinit(void)
{
    if (!g_espnow.initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing ESP-NOW");

    // Stop discovery
    cluster_espnow_stop_discovery();

    // Stop RX task
    if (g_espnow.rx_task) {
        vTaskDelete(g_espnow.rx_task);
        g_espnow.rx_task = NULL;
    }

    // Delete queue
    if (g_espnow.rx_queue) {
        vQueueDelete(g_espnow.rx_queue);
        g_espnow.rx_queue = NULL;
    }

    // Delete mutex
    if (g_espnow.peers_mutex) {
        vSemaphoreDelete(g_espnow.peers_mutex);
        g_espnow.peers_mutex = NULL;
    }

    // Deinitialize ESP-NOW
    esp_now_deinit();

    g_espnow.initialized = false;
}

bool cluster_espnow_is_initialized(void)
{
    return g_espnow.initialized;
}

// ============================================================================
// Send/Receive Implementation
// ============================================================================

esp_err_t cluster_espnow_send(uint8_t slave_id, const uint8_t *data, size_t len)
{
    if (!g_espnow.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (len > ESPNOW_MAX_DATA_LEN) {
        ESP_LOGE(TAG, "Data too large: %d > %d", len, ESPNOW_MAX_DATA_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

    // Find peer MAC by slave_id
    uint8_t *target_mac = NULL;

    if (slave_id == CLUSTER_TRANSPORT_BROADCAST) {
        target_mac = (uint8_t *)BROADCAST_MAC;
    } else {
        xSemaphoreTake(g_espnow.peers_mutex, portMAX_DELAY);
        for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
            if (g_espnow.peers[i].active && g_espnow.peers[i].slave_id == slave_id) {
                target_mac = g_espnow.peers[i].mac_addr;
                break;
            }
        }
        xSemaphoreGive(g_espnow.peers_mutex);

        if (!target_mac) {
            ESP_LOGW(TAG, "Peer not found for slave_id %d", slave_id);
            return ESP_ERR_NOT_FOUND;
        }
    }

    esp_err_t ret = esp_now_send(target_mac, data, len);
    if (ret == ESP_OK) {
        g_espnow.total_tx++;
    } else {
        g_espnow.total_tx_fail++;
        ESP_LOGW(TAG, "Send failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t cluster_espnow_send_to_master(const uint8_t *data, size_t len)
{
    if (!g_espnow.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!g_espnow.master_known) {
        ESP_LOGW(TAG, "Master MAC not known");
        return ESP_ERR_NOT_FOUND;
    }

    if (len > ESPNOW_MAX_DATA_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = esp_now_send(g_espnow.master_mac, data, len);
    if (ret == ESP_OK) {
        g_espnow.total_tx++;
    } else {
        g_espnow.total_tx_fail++;
    }

    return ret;
}

esp_err_t cluster_espnow_broadcast(const uint8_t *data, size_t len)
{
    if (!g_espnow.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (len > ESPNOW_MAX_DATA_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = esp_now_send(BROADCAST_MAC, data, len);
    if (ret == ESP_OK) {
        g_espnow.total_tx++;
    } else {
        g_espnow.total_tx_fail++;
    }

    return ret;
}

esp_err_t cluster_espnow_register_rx_callback(cluster_transport_rx_cb_t cb, void *ctx)
{
    g_espnow.rx_callback = cb;
    g_espnow.rx_callback_ctx = ctx;
    return ESP_OK;
}

esp_err_t cluster_espnow_register_tx_callback(cluster_transport_tx_cb_t cb)
{
    g_espnow.tx_callback = cb;
    return ESP_OK;
}

// ============================================================================
// Discovery Implementation
// ============================================================================

esp_err_t cluster_espnow_start_discovery(const char *cluster_id)
{
    if (!g_espnow.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_espnow.discovery_active) {
        ESP_LOGW(TAG, "Discovery already active");
        return ESP_OK;
    }

    strncpy(g_espnow.cluster_id, cluster_id ? cluster_id : "default", sizeof(g_espnow.cluster_id) - 1);

    g_espnow.discovery_active = true;

    BaseType_t ret = xTaskCreate(espnow_discovery_task,
                                  "espnow_disc",
                                  2048,
                                  NULL,
                                  4,
                                  &g_espnow.discovery_task);

    if (ret != pdPASS) {
        g_espnow.discovery_active = false;
        ESP_LOGE(TAG, "Failed to create discovery task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Discovery started for cluster '%s'", g_espnow.cluster_id);
    return ESP_OK;
}

void cluster_espnow_stop_discovery(void)
{
    if (!g_espnow.discovery_active) {
        return;
    }

    g_espnow.discovery_active = false;

    // Task will self-delete when it sees discovery_active = false
    // Give it time to clean up
    vTaskDelay(pdMS_TO_TICKS(100));

    if (g_espnow.discovery_task) {
        // Force delete if still running
        vTaskDelete(g_espnow.discovery_task);
        g_espnow.discovery_task = NULL;
    }

    ESP_LOGI(TAG, "Discovery stopped");
}

bool cluster_espnow_is_discovering(void)
{
    return g_espnow.discovery_active;
}

esp_err_t cluster_espnow_register_peer_callback(cluster_transport_peer_cb_t cb)
{
    g_espnow.peer_callback = cb;
    return ESP_OK;
}

// ============================================================================
// Peer Management Implementation
// ============================================================================

esp_err_t cluster_espnow_add_peer(uint8_t slave_id,
                                   const uint8_t *mac_addr,
                                   bool encrypted,
                                   const uint8_t *lmk)
{
    if (!g_espnow.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (slave_id >= CLUSTER_MAX_SLAVES) {
        return ESP_ERR_INVALID_ARG;
    }

    // Add to ESP-NOW
    esp_now_peer_info_t peer_info = {
        .channel = g_espnow.channel,
        .ifidx = WIFI_IF_STA,
        .encrypt = encrypted && g_espnow.encrypt_enabled
    };
    memcpy(peer_info.peer_addr, mac_addr, 6);

    if (peer_info.encrypt && lmk) {
        memcpy(peer_info.lmk, lmk, ESPNOW_LMK_LEN);
    }

    esp_err_t ret = esp_now_add_peer(&peer_info);
    if (ret != ESP_OK && ret != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "Failed to add ESP-NOW peer: %s", esp_err_to_name(ret));
        return ret;
    }

    // Add to our tracking
    xSemaphoreTake(g_espnow.peers_mutex, portMAX_DELAY);

    // Find empty slot or existing entry
    int slot = -1;
    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (!g_espnow.peers[i].active) {
            if (slot < 0) slot = i;
        } else if (memcmp(g_espnow.peers[i].mac_addr, mac_addr, 6) == 0) {
            slot = i;  // Update existing
            break;
        }
    }

    if (slot < 0) {
        xSemaphoreGive(g_espnow.peers_mutex);
        ESP_LOGE(TAG, "No free peer slots");
        return ESP_ERR_NO_MEM;
    }

    g_espnow.peers[slot].active = true;
    g_espnow.peers[slot].slave_id = slave_id;
    memcpy(g_espnow.peers[slot].mac_addr, mac_addr, 6);
    g_espnow.peers[slot].encrypted = encrypted;
    g_espnow.peers[slot].last_seen = get_time_ms();

    if (lmk) {
        memcpy(g_espnow.peers[slot].lmk, lmk, ESPNOW_LMK_LEN);
    }

    g_espnow.peer_count++;

    xSemaphoreGive(g_espnow.peers_mutex);

    ESP_LOGI(TAG, "Added peer %02X:%02X:%02X:%02X:%02X:%02X as slave %d",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5], slave_id);

    return ESP_OK;
}

esp_err_t cluster_espnow_remove_peer(uint8_t slave_id)
{
    if (!g_espnow.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(g_espnow.peers_mutex, portMAX_DELAY);

    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (g_espnow.peers[i].active && g_espnow.peers[i].slave_id == slave_id) {
            // Remove from ESP-NOW
            esp_now_del_peer(g_espnow.peers[i].mac_addr);

            // Clear our entry
            g_espnow.peers[i].active = false;
            g_espnow.peer_count--;

            xSemaphoreGive(g_espnow.peers_mutex);
            return ESP_OK;
        }
    }

    xSemaphoreGive(g_espnow.peers_mutex);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t cluster_espnow_get_peer(uint8_t slave_id, cluster_espnow_peer_t *peer)
{
    if (!peer) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(g_espnow.peers_mutex, portMAX_DELAY);

    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (g_espnow.peers[i].active && g_espnow.peers[i].slave_id == slave_id) {
            memcpy(peer, &g_espnow.peers[i], sizeof(cluster_espnow_peer_t));
            xSemaphoreGive(g_espnow.peers_mutex);
            return ESP_OK;
        }
    }

    xSemaphoreGive(g_espnow.peers_mutex);
    return ESP_ERR_NOT_FOUND;
}

uint8_t cluster_espnow_find_peer_by_mac(const uint8_t *mac_addr)
{
    xSemaphoreTake(g_espnow.peers_mutex, portMAX_DELAY);

    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (g_espnow.peers[i].active &&
            memcmp(g_espnow.peers[i].mac_addr, mac_addr, 6) == 0) {
            uint8_t slave_id = g_espnow.peers[i].slave_id;
            xSemaphoreGive(g_espnow.peers_mutex);
            return slave_id;
        }
    }

    xSemaphoreGive(g_espnow.peers_mutex);
    return 0xFF;  // Not found
}

esp_err_t cluster_espnow_set_master_mac(const uint8_t *mac_addr)
{
    memcpy(g_espnow.master_mac, mac_addr, 6);
    g_espnow.master_known = true;

    // Add master as ESP-NOW peer
    esp_now_peer_info_t peer_info = {
        .channel = g_espnow.channel,
        .ifidx = WIFI_IF_STA,
        .encrypt = g_espnow.encrypt_enabled
    };
    memcpy(peer_info.peer_addr, mac_addr, 6);

    esp_err_t ret = esp_now_add_peer(&peer_info);
    if (ret != ESP_OK && ret != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGW(TAG, "Failed to add master peer: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Master MAC set: %02X:%02X:%02X:%02X:%02X:%02X",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5]);

    return ESP_OK;
}

esp_err_t cluster_espnow_get_master_mac(uint8_t *mac_addr)
{
    if (!g_espnow.master_known) {
        return ESP_ERR_NOT_FOUND;
    }
    memcpy(mac_addr, g_espnow.master_mac, 6);
    return ESP_OK;
}

uint8_t cluster_espnow_get_peer_count(void)
{
    return g_espnow.peer_count;
}

// ============================================================================
// Status/Info Implementation
// ============================================================================

int8_t cluster_espnow_get_rssi(uint8_t slave_id)
{
    xSemaphoreTake(g_espnow.peers_mutex, portMAX_DELAY);

    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (g_espnow.peers[i].active && g_espnow.peers[i].slave_id == slave_id) {
            int8_t rssi = g_espnow.peers[i].rssi;
            xSemaphoreGive(g_espnow.peers_mutex);
            return rssi;
        }
    }

    xSemaphoreGive(g_espnow.peers_mutex);
    return 0;
}

void cluster_espnow_get_mac(uint8_t *mac_addr)
{
    memcpy(mac_addr, g_espnow.self_mac, 6);
}

uint8_t cluster_espnow_get_channel(void)
{
    return g_espnow.channel;
}

void cluster_espnow_get_stats(uint32_t *tx_count, uint32_t *rx_count, uint32_t *tx_fail)
{
    if (tx_count) *tx_count = g_espnow.total_tx;
    if (rx_count) *rx_count = g_espnow.total_rx;
    if (tx_fail) *tx_fail = g_espnow.total_tx_fail;
}

// ============================================================================
// Internal Callbacks
// ============================================================================

static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    // Find slave_id for this MAC
    uint8_t slave_id = cluster_espnow_find_peer_by_mac(mac_addr);

    if (status != ESP_NOW_SEND_SUCCESS) {
        // Update failure count
        xSemaphoreTake(g_espnow.peers_mutex, portMAX_DELAY);
        for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
            if (g_espnow.peers[i].active &&
                memcmp(g_espnow.peers[i].mac_addr, mac_addr, 6) == 0) {
                g_espnow.peers[i].tx_failures++;
                break;
            }
        }
        xSemaphoreGive(g_espnow.peers_mutex);
    }

    // Call user callback
    if (g_espnow.tx_callback) {
        g_espnow.tx_callback(slave_id, status == ESP_NOW_SEND_SUCCESS);
    }
}

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                           const uint8_t *data, int len)
{
    if (!g_espnow.rx_queue) {
        return;
    }

    // Queue message for processing (don't do heavy work in callback)
    cluster_espnow_rx_msg_t msg;
    memcpy(msg.src_mac, recv_info->src_addr, 6);
    msg.data_len = (len > ESPNOW_MAX_DATA_LEN) ? ESPNOW_MAX_DATA_LEN : len;
    memcpy(msg.data, data, msg.data_len);
    msg.rssi = recv_info->rx_ctrl ? recv_info->rx_ctrl->rssi : 0;
    msg.timestamp = get_time_ms();

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(g_espnow.rx_queue, &msg, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ============================================================================
// Internal Tasks
// ============================================================================

static void espnow_rx_task(void *pvParameters)
{
    cluster_espnow_rx_msg_t msg;

    while (1) {
        if (xQueueReceive(g_espnow.rx_queue, &msg, portMAX_DELAY)) {
            g_espnow.total_rx++;

            // Update RSSI for known peer
            uint8_t slave_id = cluster_espnow_find_peer_by_mac(msg.src_mac);
            if (slave_id != 0xFF) {
                xSemaphoreTake(g_espnow.peers_mutex, portMAX_DELAY);
                for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
                    if (g_espnow.peers[i].active &&
                        memcmp(g_espnow.peers[i].mac_addr, msg.src_mac, 6) == 0) {
                        g_espnow.peers[i].rssi = msg.rssi;
                        g_espnow.peers[i].last_seen = msg.timestamp;
                        g_espnow.peers[i].rx_count++;
                        break;
                    }
                }
                xSemaphoreGive(g_espnow.peers_mutex);
            }

            // Check for discovery messages
            if (msg.data_len > 6 && memcmp(msg.data, "$CLDSC", 6) == 0) {
                // Discovery beacon - notify peer callback
                if (g_espnow.peer_callback) {
                    g_espnow.peer_callback(msg.src_mac, msg.rssi);
                }
                continue;
            }

            // Forward to cluster message handler
            if (g_espnow.rx_callback) {
                g_espnow.rx_callback(msg.data, msg.data_len, slave_id, g_espnow.rx_callback_ctx);
            }
        }
    }
}

static void espnow_discovery_task(void *pvParameters)
{
    char beacon[128];

    ESP_LOGI(TAG, "Discovery task started");

    while (g_espnow.discovery_active) {
        // Format discovery beacon: $CLDSC,mac,cluster_id,channel*XX
        int len = snprintf(beacon, sizeof(beacon),
                          "$CLDSC,%02X%02X%02X%02X%02X%02X,%s,%d",
                          g_espnow.self_mac[0], g_espnow.self_mac[1],
                          g_espnow.self_mac[2], g_espnow.self_mac[3],
                          g_espnow.self_mac[4], g_espnow.self_mac[5],
                          g_espnow.cluster_id,
                          g_espnow.channel);

        // Add simple checksum
        uint8_t checksum = 0;
        for (int i = 1; i < len; i++) {  // Skip $
            checksum ^= beacon[i];
        }
        len += snprintf(beacon + len, sizeof(beacon) - len, "*%02X\r\n", checksum);

        // Broadcast
        cluster_espnow_broadcast((uint8_t *)beacon, len);

        vTaskDelay(pdMS_TO_TICKS(CONFIG_CLUSTER_ESPNOW_DISCOVERY_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "Discovery task ended");
    g_espnow.discovery_task = NULL;
    vTaskDelete(NULL);
}

// ============================================================================
// Utility Functions
// ============================================================================

static int64_t get_time_ms(void)
{
    return esp_timer_get_time() / 1000;
}

#endif // CLUSTER_ENABLED && CONFIG_CLUSTER_TRANSPORT_ESPNOW
