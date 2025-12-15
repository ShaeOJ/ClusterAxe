/**
 * @file cluster_espnow.c
 * @brief ESP-NOW transport implementation for ClusterAxe
 *
 * Uses the native ESP-IDF ESP-NOW API for wireless communication.
 * Compatible with ESP-IDF 5.5.x
 *
 * @author ClusterAxe Project
 * @license GPL-3.0
 */

#include "cluster_espnow.h"
#include "cluster_config.h"

#if CLUSTER_ENABLED && (defined(CONFIG_CLUSTER_TRANSPORT_ESPNOW) || defined(CONFIG_CLUSTER_TRANSPORT_BOTH))

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "cluster_espnow";

// ============================================================================
// Constants
// ============================================================================

#define ESPNOW_QUEUE_SIZE       16
#define ESPNOW_MAX_DATA_LEN     250

// ============================================================================
// State
// ============================================================================

typedef struct {
    uint8_t src_mac[6];
    uint8_t data[ESPNOW_MAX_DATA_LEN];
    size_t len;
} espnow_rx_event_t;

static struct {
    bool initialized;
    uint8_t self_mac[6];
    uint8_t channel;
    cluster_transport_rx_cb_t rx_callback;
    void *rx_callback_ctx;
    TaskHandle_t rx_task;
    TaskHandle_t discovery_task;
    QueueHandle_t rx_queue;
    SemaphoreHandle_t send_sem;
    SemaphoreHandle_t send_mutex;
    bool discovery_active;
    esp_now_send_status_t last_send_status;
    bool registration_sent;           // Track if we've sent registration to master
    uint8_t master_mac[6];            // MAC of the master we registered with
} g_espnow = {0};

// Broadcast MAC for discovery
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Discovery beacon magic header
static const char BEACON_MAGIC[] = "CLAXE";

// ============================================================================
// Callbacks (called from WiFi task context)
// ============================================================================

/**
 * @brief ESP-NOW send callback (ESP-IDF 5.5.x signature)
 */
static void espnow_send_cb(const esp_now_send_info_t *send_info, esp_now_send_status_t status)
{
    (void)send_info;  // Unused for now
    g_espnow.last_send_status = status;
    if (g_espnow.send_sem) {
        xSemaphoreGive(g_espnow.send_sem);
    }
}

/**
 * @brief ESP-NOW receive callback
 */
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (!recv_info || !data || len <= 0 || len > ESPNOW_MAX_DATA_LEN) {
        return;
    }

    if (!g_espnow.rx_queue) {
        return;
    }

    // Log ALL received packets for debugging
    ESP_LOGI(TAG, "RX from " MACSTR " len=%d: %.10s...",
             MAC2STR(recv_info->src_addr), len, (const char*)data);

    // Queue the data for processing in our task
    espnow_rx_event_t evt;
    memcpy(evt.src_mac, recv_info->src_addr, 6);
    memcpy(evt.data, data, len);
    evt.len = len;

    // Don't block if queue is full
    xQueueSend(g_espnow.rx_queue, &evt, 0);
}

// ============================================================================
// Receive Task
// ============================================================================

static void espnow_rx_task(void *pvParameters)
{
    espnow_rx_event_t evt;

    ESP_LOGI(TAG, "RX task started");

    while (1) {
        if (xQueueReceive(g_espnow.rx_queue, &evt, portMAX_DELAY) == pdTRUE) {
            ESP_LOGD(TAG, "Received %d bytes from " MACSTR,
                     (int)evt.len, MAC2STR(evt.src_mac));

            // Check if it's a discovery beacon
            if (evt.len >= sizeof(BEACON_MAGIC) - 1 &&
                memcmp(evt.data, BEACON_MAGIC, sizeof(BEACON_MAGIC) - 1) == 0) {

                // Check if this is the same master we already registered with
                bool same_master = (g_espnow.registration_sent &&
                                   memcmp(g_espnow.master_mac, evt.src_mac, 6) == 0);

                if (same_master) {
                    // Already registered with this master, ignore beacon
                    ESP_LOGD(TAG, "Ignoring beacon from known master " MACSTR, MAC2STR(evt.src_mac));
                    continue;
                }

                // New master or first beacon - process it
                ESP_LOGI(TAG, "Discovery beacon from " MACSTR, MAC2STR(evt.src_mac));

                // Add Master as peer
                esp_now_peer_info_t peer = {0};
                memcpy(peer.peer_addr, evt.src_mac, 6);
                peer.channel = g_espnow.channel;
                peer.ifidx = WIFI_IF_STA;
                peer.encrypt = false;

                if (!esp_now_is_peer_exist(evt.src_mac)) {
                    esp_now_add_peer(&peer);
                    ESP_LOGI(TAG, "Added Master " MACSTR " to peers", MAC2STR(evt.src_mac));
                }

                // Send Registration Message (only once per master)
                extern const char* cluster_get_hostname(void);
                extern const char* cluster_get_ip_addr(void);

                const char *ip_addr = cluster_get_ip_addr();

                // IP is optional for ESP-NOW (direct MAC-to-MAC communication)
                // Use "N/A" if no valid IP - it's only for display purposes
                const char *ip_to_send = ip_addr;
                if (!ip_addr || ip_addr[0] == '\0' || strcmp(ip_addr, "0.0.0.0") == 0) {
                    ip_to_send = "N/A";
                    ESP_LOGI(TAG, "No IP assigned yet - registering with IP=N/A");
                }

                char payload[64];
                snprintf(payload, sizeof(payload), "%s,%s",
                         cluster_get_hostname(),
                         ip_to_send);

                char msg_buf[128];
                int msg_len = snprintf(msg_buf, sizeof(msg_buf), "$REGISTER,%s", payload);

                // Calc checksum (XOR of chars between $ and *)
                uint8_t checksum = 0;
                for(int i = 1; i < msg_len; i++) {
                     checksum ^= msg_buf[i];
                }

                // Append checksum
                int final_len = snprintf(msg_buf + msg_len, sizeof(msg_buf) - msg_len, "*%02X\r\n", checksum);

                // Send directly to Master MAC
                esp_err_t send_ret = cluster_espnow_send(evt.src_mac, msg_buf, msg_len + final_len);
                if (send_ret == ESP_OK) {
                    // Mark registration as sent and remember master MAC
                    g_espnow.registration_sent = true;
                    memcpy(g_espnow.master_mac, evt.src_mac, 6);
                    ESP_LOGI(TAG, "Sent registration to Master");
                } else {
                    ESP_LOGW(TAG, "Failed to send registration: %s", esp_err_to_name(send_ret));
                }

                continue;
            }

            // The data should be a null-terminated cluster message
            char *msg = (char *)evt.data;
            if (evt.len < ESPNOW_MAX_DATA_LEN) {
                msg[evt.len] = '\0';  // Ensure null termination
            }

            ESP_LOGI(TAG, "Processing message: %.20s... (len=%d)", msg, (int)evt.len);

            // Parse message type (format: $MSGTYPE,...)
            if (msg[0] != '$') {
                ESP_LOGW(TAG, "Message doesn't start with $: 0x%02X", msg[0]);
                continue;
            }

            char *comma = strchr(msg, ',');
            if (!comma) {
                ESP_LOGW(TAG, "Message has no comma separator");
                continue;
            }

            size_t type_len = comma - msg - 1;
            char msg_type[16] = {0};
            if (type_len >= sizeof(msg_type)) {
                ESP_LOGW(TAG, "Message type too long: %d", (int)type_len);
                continue;
            }
            strncpy(msg_type, msg + 1, type_len);

#if CLUSTER_IS_MASTER
            // Master: Update slave MAC from heartbeats (fixes stale/wrong MAC from old registration)
            if (strcmp(msg_type, "CLHBT") == 0) {
                const char *payload = comma + 1;
                int slave_id = atoi(payload);
                if (slave_id >= 0 && slave_id < 8) {
                    extern void cluster_master_update_slave_mac(uint8_t slave_id, const uint8_t *mac);
                    cluster_master_update_slave_mac(slave_id, evt.src_mac);
                }
            }

            // Master: Handle REGISTER messages specially to capture MAC address
            if (strcmp(msg_type, "REGISTER") == 0) {
                // Parse hostname,ip from payload
                const char *payload = comma + 1;
                char hostname[32] = {0};
                char ip_addr[16] = {0};

                // Find next comma for IP
                const char *ip_comma = strchr(payload, ',');
                if (ip_comma) {
                    size_t hostname_len = ip_comma - payload;
                    if (hostname_len < sizeof(hostname)) {
                        strncpy(hostname, payload, hostname_len);
                    }
                    // Copy IP (up to * or end)
                    const char *ip_start = ip_comma + 1;
                    const char *ip_end = strchr(ip_start, '*');
                    size_t ip_len = ip_end ? (size_t)(ip_end - ip_start) : strlen(ip_start);
                    if (ip_len < sizeof(ip_addr)) {
                        strncpy(ip_addr, ip_start, ip_len);
                    }
                } else {
                    // No IP, just hostname
                    const char *end = strchr(payload, '*');
                    size_t len = end ? (size_t)(end - payload) : strlen(payload);
                    if (len < sizeof(hostname)) {
                        strncpy(hostname, payload, len);
                    }
                }

                ESP_LOGI(TAG, "Registration from " MACSTR ": hostname='%s', ip='%s'",
                         MAC2STR(evt.src_mac), hostname, ip_addr);

                // Call registration handler with MAC address
                extern esp_err_t cluster_master_handle_registration_with_mac(
                    const char *hostname, const char *ip_addr, const uint8_t *mac_addr);
                cluster_master_handle_registration_with_mac(hostname, ip_addr, evt.src_mac);
                continue;
            }
#endif

            // Forward to cluster message handler
            if (g_espnow.rx_callback) {
                // Use LOGW for share messages to ensure visibility
                if (strcmp(msg_type, "CLSHR") == 0) {
                    ESP_LOGW(TAG, "SHARE: Forwarding CLSHR to callback from " MACSTR,
                             MAC2STR(evt.src_mac));
                } else {
                    ESP_LOGI(TAG, "Forwarding to callback: type=%s", msg_type);
                }
                g_espnow.rx_callback(msg_type, comma + 1,
                                     evt.len - (comma - msg) - 1,
                                     evt.src_mac,
                                     g_espnow.rx_callback_ctx);
            } else {
                ESP_LOGW(TAG, "No rx_callback set!");
            }
        }
    }
}

// ============================================================================
// Discovery Task (Master only)
// ============================================================================

#if CLUSTER_IS_MASTER
static void discovery_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Discovery task started");

    // Discovery beacon payload
    char beacon[32];
    snprintf(beacon, sizeof(beacon), "%s,MASTER", BEACON_MAGIC);

    while (g_espnow.discovery_active) {
        // Broadcast discovery beacon
        esp_err_t ret = esp_now_send(BROADCAST_MAC, (uint8_t *)beacon, strlen(beacon));

        if (ret != ESP_OK) {
            // Only log errors occasionally to prevent spamming logs during heavy traffic
            static int64_t last_log = 0;
            if (esp_timer_get_time() - last_log > 5000000) { // 5 seconds
                ESP_LOGW(TAG, "Failed to send discovery beacon: %s", esp_err_to_name(ret));
                last_log = esp_timer_get_time();
            }
        } else {
            ESP_LOGD(TAG, "Discovery beacon sent");
        }

        // Add random jitter to avoid collisions and yield
        int jitter = esp_random() % 200;
        vTaskDelay(pdMS_TO_TICKS(CONFIG_CLUSTER_ESPNOW_DISCOVERY_INTERVAL_MS + jitter));
    }

    ESP_LOGI(TAG, "Discovery task stopped");
    vTaskDelete(NULL);
}
#endif

// ============================================================================
// Peer Management
// ============================================================================

static esp_err_t add_broadcast_peer(void)
{
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, BROADCAST_MAC, 6);
    peer.channel = 0;  // Use current channel
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    esp_err_t ret = esp_now_add_peer(&peer);
    if (ret == ESP_ERR_ESPNOW_EXIST) {
        return ESP_OK;  // Already exists, that's fine
    }
    return ret;
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t cluster_espnow_init(void)
{
    if (g_espnow.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing ESP-NOW transport (native API)");

    // Get our MAC address
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, g_espnow.self_mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MAC address: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGW(TAG, "=== MY MAC ADDRESS: " MACSTR " ===", MAC2STR(g_espnow.self_mac));

    // Get current WiFi channel
    uint8_t primary_channel;
    wifi_second_chan_t secondary_channel;
    esp_wifi_get_channel(&primary_channel, &secondary_channel);
    g_espnow.channel = primary_channel;
    ESP_LOGI(TAG, "Using WiFi channel %d", g_espnow.channel);

    // Create synchronization primitives
    g_espnow.rx_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnow_rx_event_t));
    if (!g_espnow.rx_queue) {
        ESP_LOGE(TAG, "Failed to create RX queue");
        return ESP_ERR_NO_MEM;
    }

    g_espnow.send_sem = xSemaphoreCreateBinary();
    if (!g_espnow.send_sem) {
        ESP_LOGE(TAG, "Failed to create send semaphore");
        vQueueDelete(g_espnow.rx_queue);
        return ESP_ERR_NO_MEM;
    }

    g_espnow.send_mutex = xSemaphoreCreateMutex();
    if (!g_espnow.send_mutex) {
        ESP_LOGE(TAG, "Failed to create send mutex");
        vQueueDelete(g_espnow.rx_queue);
        vSemaphoreDelete(g_espnow.send_sem);
        return ESP_ERR_NO_MEM;
    }

    // Initialize ESP-NOW
    ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW: %s", esp_err_to_name(ret));
        vQueueDelete(g_espnow.rx_queue);
        vSemaphoreDelete(g_espnow.send_sem);
        vSemaphoreDelete(g_espnow.send_mutex);
        return ret;
    }

    // Register callbacks
    ret = esp_now_register_send_cb(espnow_send_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register send callback: %s", esp_err_to_name(ret));
        esp_now_deinit();
        vQueueDelete(g_espnow.rx_queue);
        vSemaphoreDelete(g_espnow.send_sem);
        vSemaphoreDelete(g_espnow.send_mutex);
        return ret;
    }

    ret = esp_now_register_recv_cb(espnow_recv_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register receive callback: %s", esp_err_to_name(ret));
        esp_now_deinit();
        vQueueDelete(g_espnow.rx_queue);
        vSemaphoreDelete(g_espnow.send_sem);
        vSemaphoreDelete(g_espnow.send_mutex);
        return ret;
    }

    // Add broadcast peer for discovery
    ret = add_broadcast_peer();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add broadcast peer: %s", esp_err_to_name(ret));
        esp_now_deinit();
        vQueueDelete(g_espnow.rx_queue);
        vSemaphoreDelete(g_espnow.send_sem);
        vSemaphoreDelete(g_espnow.send_mutex);
        return ret;
    }

    // Create RX processing task (4KB stack is sufficient)
    BaseType_t task_ret = xTaskCreate(espnow_rx_task,
                                       "espnow_rx",
                                       4096,
                                       NULL,
                                       5,
                                       &g_espnow.rx_task);

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create RX task");
        esp_now_deinit();
        vQueueDelete(g_espnow.rx_queue);
        vSemaphoreDelete(g_espnow.send_sem);
        vSemaphoreDelete(g_espnow.send_mutex);
        return ESP_ERR_NO_MEM;
    }

    g_espnow.initialized = true;
    ESP_LOGI(TAG, "ESP-NOW transport initialized");

    return ESP_OK;
}

void cluster_espnow_deinit(void)
{
    if (!g_espnow.initialized) {
        return;
    }

    cluster_espnow_stop_discovery();

    // Delete RX task
    if (g_espnow.rx_task) {
        vTaskDelete(g_espnow.rx_task);
        g_espnow.rx_task = NULL;
    }

    esp_now_deinit();

    if (g_espnow.rx_queue) {
        vQueueDelete(g_espnow.rx_queue);
        g_espnow.rx_queue = NULL;
    }

    if (g_espnow.send_sem) {
        vSemaphoreDelete(g_espnow.send_sem);
        g_espnow.send_sem = NULL;
    }

    if (g_espnow.send_mutex) {
        vSemaphoreDelete(g_espnow.send_mutex);
        g_espnow.send_mutex = NULL;
    }

    memset(&g_espnow, 0, sizeof(g_espnow));
    ESP_LOGI(TAG, "ESP-NOW transport deinitialized");
}

esp_err_t cluster_espnow_send(const uint8_t *dest_mac, const char *data, size_t len)
{
    if (!g_espnow.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > ESPNOW_MAX_DATA_LEN) {
        ESP_LOGE(TAG, "Message too large for ESP-NOW: %d bytes (max %d)", (int)len, ESPNOW_MAX_DATA_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

    // Protect access to the radio and the send_sem
    // Use shorter timeout to avoid blocking HTTP server thread
    if (xSemaphoreTake(g_espnow.send_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "Send mutex timeout");
        return ESP_ERR_TIMEOUT;
    }

    const uint8_t *target = dest_mac ? dest_mac : BROADCAST_MAC;

    // If sending to a specific peer (not broadcast), ensure peer exists
    if (dest_mac && memcmp(dest_mac, BROADCAST_MAC, 6) != 0) {
        if (!esp_now_is_peer_exist(dest_mac)) {
            esp_now_peer_info_t peer = {0};
            memcpy(peer.peer_addr, dest_mac, 6);
            peer.channel = 0;
            peer.ifidx = WIFI_IF_STA;
            peer.encrypt = false;

            esp_err_t ret = esp_now_add_peer(&peer);
            if (ret != ESP_OK && ret != ESP_ERR_ESPNOW_EXIST) {
                ESP_LOGW(TAG, "Failed to add peer: %s", esp_err_to_name(ret));
                xSemaphoreGive(g_espnow.send_mutex);
                return ret;
            }
        }
    }

    // Send with timeout
    xSemaphoreTake(g_espnow.send_sem, 0);  // Clear any pending signal

    esp_err_t ret = esp_now_send(target, (uint8_t *)data, len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Send failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(g_espnow.send_mutex);
        return ret;
    }

    // Wait for send callback (50ms timeout to avoid blocking other tasks)
    if (xSemaphoreTake(g_espnow.send_sem, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "Send timeout");
        xSemaphoreGive(g_espnow.send_mutex);
        return ESP_ERR_TIMEOUT;
    }

    if (g_espnow.last_send_status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "Send failed (no ACK)");
        xSemaphoreGive(g_espnow.send_mutex);
        return ESP_FAIL;
    }

    xSemaphoreGive(g_espnow.send_mutex);
    return ESP_OK;
}

esp_err_t cluster_espnow_broadcast(const char *data, size_t len)
{
    return cluster_espnow_send(NULL, data, len);
}

void cluster_espnow_set_rx_callback(cluster_transport_rx_cb_t callback, void *ctx)
{
    g_espnow.rx_callback = callback;
    g_espnow.rx_callback_ctx = ctx;
}

esp_err_t cluster_espnow_start_discovery(void)
{
#if CLUSTER_IS_MASTER
    if (g_espnow.discovery_active) {
        return ESP_OK;
    }

    g_espnow.discovery_active = true;

    BaseType_t ret = xTaskCreate(discovery_task,
                                  "espnow_disc",
                                  8192,
                                  NULL,
                                  4,
                                  &g_espnow.discovery_task);

    if (ret != pdPASS) {
        g_espnow.discovery_active = false;
        ESP_LOGE(TAG, "Failed to create discovery task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Discovery started");
    return ESP_OK;
#else
    ESP_LOGW(TAG, "Discovery only available on master");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void cluster_espnow_stop_discovery(void)
{
#if CLUSTER_IS_MASTER
    if (!g_espnow.discovery_active) {
        return;
    }

    g_espnow.discovery_active = false;

    // Task will delete itself
    if (g_espnow.discovery_task) {
        // Give task time to exit
        vTaskDelay(pdMS_TO_TICKS(200));
        g_espnow.discovery_task = NULL;
    }

    ESP_LOGI(TAG, "Discovery stopped");
#endif
}

bool cluster_espnow_is_initialized(void)
{
    return g_espnow.initialized;
}

void cluster_espnow_get_self_mac(uint8_t *mac)
{
    if (mac && g_espnow.initialized) {
        memcpy(mac, g_espnow.self_mac, 6);
    }
}

bool cluster_espnow_get_master_mac(uint8_t *mac)
{
    if (!mac || !g_espnow.initialized) {
        return false;
    }

    // Check if we have a valid master MAC (not all zeros)
    bool has_master = false;
    for (int i = 0; i < 6; i++) {
        if (g_espnow.master_mac[i] != 0) {
            has_master = true;
            break;
        }
    }

    if (has_master) {
        memcpy(mac, g_espnow.master_mac, 6);
        return true;
    }
    return false;
}

uint8_t cluster_espnow_get_channel(void)
{
    return g_espnow.channel;
}

esp_err_t cluster_espnow_add_peer(const uint8_t *mac)
{
    if (!g_espnow.initialized || !mac) {
        return ESP_ERR_INVALID_STATE;
    }

    if (esp_now_is_peer_exist(mac)) {
        return ESP_OK;
    }

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    return esp_now_add_peer(&peer);
}

esp_err_t cluster_espnow_remove_peer(const uint8_t *mac)
{
    if (!g_espnow.initialized || !mac) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!esp_now_is_peer_exist(mac)) {
        return ESP_OK;
    }

    return esp_now_del_peer(mac);
}

void cluster_espnow_on_wifi_reconnect(void)
{
    if (!g_espnow.initialized) {
        return;
    }

    // Update channel to match new WiFi connection
    uint8_t primary_channel;
    wifi_second_chan_t secondary_channel;
    esp_wifi_get_channel(&primary_channel, &secondary_channel);

    if (primary_channel != g_espnow.channel) {
        ESP_LOGI(TAG, "WiFi channel changed: %d -> %d", g_espnow.channel, primary_channel);
        g_espnow.channel = primary_channel;

        // Note: Existing peers will continue to work as they use channel 0 (auto)
        // New peers will be added with the updated channel
    }

    // Reset registration state so slave will re-register with master
    // This handles the case where master or slave rebooted
    cluster_espnow_reset_registration();

    ESP_LOGI(TAG, "ESP-NOW updated after WiFi reconnect (channel %d)", g_espnow.channel);
}

void cluster_espnow_reset_registration(void)
{
    if (!g_espnow.initialized) {
        return;
    }

    // Clear registration state
    g_espnow.registration_sent = false;
    memset(g_espnow.master_mac, 0, 6);

    ESP_LOGI(TAG, "Registration state reset - will re-register on next beacon");
}

#endif // CLUSTER_ENABLED && ESP-NOW transport
