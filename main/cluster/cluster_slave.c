/**
 * @file cluster_slave.c
 * @brief Bitaxe Cluster Slave Implementation
 *
 * The slave node receives work from the master via BAP UART,
 * performs mining on its assigned nonce range, and reports
 * found shares back to the master.
 *
 * @author Clusteraxe Project
 * @license GPL-3.0
 */

#include "cluster.h"
#include "cluster_protocol.h"
#include "cluster_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "string.h"
#include "stdio.h"

#if CLUSTER_ENABLED && CLUSTER_IS_SLAVE

static const char *TAG = "cluster_slave";

// ============================================================================
// External References (from ESP-Miner codebase)
// ============================================================================

// These will be implemented in the ESP-Miner integration layer
// For now we declare them as weak symbols with defaults

// Get current hashrate from ASIC module
__attribute__((weak)) uint32_t cluster_get_asic_hashrate(void)
{
    // Will be linked to actual ASIC hashrate function
    return 0;
}

// Get chip temperature from thermal module
__attribute__((weak)) float cluster_get_chip_temp(void)
{
    // Will be linked to actual temperature function
    return 0.0f;
}

// Get fan RPM from thermal module
__attribute__((weak)) uint16_t cluster_get_fan_rpm(void)
{
    // Will be linked to actual fan RPM function
    return 0;
}

// Get device hostname from system module
__attribute__((weak)) const char* cluster_get_hostname(void)
{
    // Will be linked to actual hostname getter
    return "bitaxe-slave";
}

// Get device IP address
__attribute__((weak)) const char* cluster_get_ip_addr(void)
{
    // Will be linked to actual IP getter from WiFi module
    return "";
}

// Get ASIC frequency in MHz
__attribute__((weak)) uint16_t cluster_get_asic_frequency(void)
{
    // Will be linked to actual frequency getter
    return 0;
}

// Get core voltage in mV
__attribute__((weak)) uint16_t cluster_get_core_voltage(void)
{
    // Will be linked to actual voltage getter
    return 0;
}

// Get power consumption in Watts
__attribute__((weak)) float cluster_get_power(void)
{
    // Will be linked to actual power getter
    return 0.0f;
}

// Get input voltage in Volts
__attribute__((weak)) float cluster_get_voltage_in(void)
{
    // Will be linked to actual voltage input getter
    return 0.0f;
}

// Submit work to ASIC (will be integrated with create_jobs_task)
__attribute__((weak)) void cluster_submit_work_to_asic(const cluster_work_t *work)
{
    // Will be integrated with ASIC task
    ESP_LOGW(TAG, "cluster_submit_work_to_asic not implemented yet");
}

// ============================================================================
// Private State
// ============================================================================

static cluster_slave_state_t *g_slave = NULL;

// ============================================================================
// Work Management
// ============================================================================

/**
 * @brief Process work received from master
 *
 * Since work is broadcast to all slaves, we filter by target_slave_id
 * to only process work intended for this specific slave.
 */
esp_err_t cluster_slave_receive_work(const cluster_work_t *work)
{
    if (!g_slave || !work) {
        return ESP_ERR_INVALID_ARG;
    }

    // Filter: only process work targeted at this slave
    // Work is broadcast, so each slave receives all work messages
    if (!g_slave->registered || g_slave->my_id == 0xFF) {
        ESP_LOGW(TAG, "Ignoring work - not registered yet");
        return ESP_ERR_INVALID_STATE;
    }

    if (work->target_slave_id != g_slave->my_id) {
        ESP_LOGD(TAG, "Ignoring work for slave %d (I am slave %d)",
                 work->target_slave_id, g_slave->my_id);
        return ESP_OK;  // Not an error, just not for us
    }

    ESP_LOGI(TAG, "Work is for me (slave %d), processing...", g_slave->my_id);

    xSemaphoreTake(g_slave->work_mutex, portMAX_DELAY);

    // Store the new work
    memcpy(&g_slave->current_work, work, sizeof(cluster_work_t));
    g_slave->work_valid = true;
    g_slave->last_work_received = esp_timer_get_time() / 1000;

    xSemaphoreGive(g_slave->work_mutex);

    ESP_LOGW(TAG, "Received work: job %lu, nonce range 0x%08lX - 0x%08lX",
             (unsigned long)work->job_id,
             (unsigned long)work->nonce_start,
             (unsigned long)work->nonce_end);
    ESP_LOGW(TAG, "Work details: version=0x%08lX, version_mask=0x%08lX, pool_diff=%lu",
             (unsigned long)work->version,
             (unsigned long)work->version_mask,
             (unsigned long)work->pool_diff);

    // Log extranonce2 for debugging
    char en2_hex[17];
    for (int i = 0; i < work->extranonce2_len && i < 8; i++) {
        sprintf(en2_hex + i * 2, "%02x", work->extranonce2[i]);
    }
    en2_hex[work->extranonce2_len * 2] = '\0';
    ESP_LOGW(TAG, "Work extranonce2: %s (len=%d)", en2_hex, work->extranonce2_len);

    // Notify worker task that new work is available
    if (g_slave->worker_task) {
        ESP_LOGW(TAG, "Notifying worker_task (handle=%p) of new work", (void*)g_slave->worker_task);
        xTaskNotifyGive(g_slave->worker_task);
    } else {
        ESP_LOGE(TAG, "ERROR: worker_task handle is NULL - cannot notify!");
    }

    return ESP_OK;
}

/**
 * @brief Get current work for ASIC
 */
esp_err_t cluster_slave_get_work(cluster_work_t *work)
{
    if (!g_slave || !work) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_slave->work_valid) {
        return ESP_ERR_NOT_FOUND;
    }

    xSemaphoreTake(g_slave->work_mutex, portMAX_DELAY);
    memcpy(work, &g_slave->current_work, sizeof(cluster_work_t));
    xSemaphoreGive(g_slave->work_mutex);

    return ESP_OK;
}

bool cluster_slave_has_work(void)
{
    return g_slave && g_slave->work_valid;
}

// ============================================================================
// Share Submission
// ============================================================================

/**
 * @brief Submit share found by ASIC to master
 */
esp_err_t cluster_slave_submit_share(const cluster_share_t *share)
{
    if (!g_slave || !share) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_slave->registered) {
        ESP_LOGW(TAG, "Cannot submit share - not registered with master");
        return ESP_ERR_INVALID_STATE;
    }

    // Encode share for BAP transmission
    char payload[256];
    int len = cluster_protocol_encode_share(share, payload, sizeof(payload));

    if (len < 0) {
        ESP_LOGE(TAG, "Failed to encode share");
        return ESP_FAIL;
    }

    // Log the full share message being sent
    ESP_LOGW(TAG, "SHARE TX: %s (len=%d)", payload, len);

    esp_err_t ret = ESP_FAIL;

    // Try ESP-NOW first if we have master MAC
    // Send multiple times with delays to improve reliability (master may be busy TX)
#if defined(CONFIG_CLUSTER_TRANSPORT_ESPNOW) || defined(CONFIG_CLUSTER_TRANSPORT_BOTH)
    extern bool cluster_espnow_get_master_mac(uint8_t *mac);
    extern esp_err_t cluster_espnow_send(const uint8_t *dest_mac, const char *data, size_t len);

    uint8_t master_mac[6];
    if (cluster_espnow_get_master_mac(master_mac)) {
        ESP_LOGW(TAG, "Sending share via ESP-NOW to %02X:%02X:%02X:%02X:%02X:%02X",
                 master_mac[0], master_mac[1], master_mac[2],
                 master_mac[3], master_mac[4], master_mac[5]);

        // Retry up to 3 times with delays (master broadcasts work frequently, may miss our TX)
        for (int attempt = 0; attempt < 3; attempt++) {
            ret = cluster_espnow_send(master_mac, payload, len);
            if (ret == ESP_OK) {
                ESP_LOGW(TAG, "ESP-NOW share send SUCCESS (attempt %d)", attempt + 1);
                break;
            }
            ESP_LOGW(TAG, "ESP-NOW share attempt %d failed: %s", attempt + 1, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(30));  // Small delay before retry
        }

        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "All ESP-NOW attempts failed, falling back to broadcast");
        }
    } else {
        ESP_LOGW(TAG, "No master MAC available for share, using broadcast");
    }
#endif

    // Fallback to UART broadcast if ESP-NOW failed or unavailable
    if (ret != ESP_OK) {
        extern esp_err_t BAP_uart_send_raw(const char *data, size_t len);
        ESP_LOGW(TAG, "Sending share via broadcast fallback");
        ret = BAP_uart_send_raw(payload, len);
    }

    if (ret == ESP_OK) {
        g_slave->shares_submitted++;
        ESP_LOGI(TAG, "Submitted share: job %lu, nonce 0x%08lX",
                 (unsigned long)share->job_id,
                 (unsigned long)share->nonce);
    } else {
        ESP_LOGE(TAG, "Share submission FAILED: %s", esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief Simple deduplication for shares found by slave
 */
#define SLAVE_RECENT_SHARES 16
static struct {
    uint32_t nonce;
    uint32_t job_id;
    bool valid;  // Track if entry is valid
} slave_recent_shares[SLAVE_RECENT_SHARES];
static int slave_recent_idx = 0;

static bool slave_is_duplicate(uint32_t nonce, uint32_t job_id)
{
    for (int i = 0; i < SLAVE_RECENT_SHARES; i++) {
        if (slave_recent_shares[i].valid &&
            slave_recent_shares[i].nonce == nonce &&
            slave_recent_shares[i].job_id == job_id) {
            return true;
        }
    }
    return false;
}

static void slave_record_share(uint32_t nonce, uint32_t job_id)
{
    slave_recent_shares[slave_recent_idx].nonce = nonce;
    slave_recent_shares[slave_recent_idx].job_id = job_id;
    slave_recent_shares[slave_recent_idx].valid = true;
    slave_recent_idx = (slave_recent_idx + 1) % SLAVE_RECENT_SHARES;
}

/**
 * @brief Called by ASIC driver when a share is found
 * This integrates with the existing ESP-Miner ASIC result handling
 *
 * @param nonce The winning nonce found by ASIC
 * @param job_id Numeric job ID
 * @param version The actual rolled version bits from the ASIC (not base version)
 * @param ntime The ntime value (may be rolled)
 */
void cluster_slave_on_share_found(uint32_t nonce, uint32_t job_id, uint32_t version, uint32_t ntime)
{
    ESP_LOGW(TAG, "on_share_found: nonce=0x%08lX, job=%lu, ver=0x%08lX",
             (unsigned long)nonce, (unsigned long)job_id, (unsigned long)version);

    if (!g_slave) {
        ESP_LOGE(TAG, "ERROR: g_slave is NULL in on_share_found");
        return;
    }
    if (!g_slave->work_valid) {
        ESP_LOGW(TAG, "Ignoring share - no valid work (work_valid=false)");
        return;
    }

    // Check for duplicate share (ASIC might report same result multiple times)
    if (slave_is_duplicate(nonce, job_id)) {
        ESP_LOGW(TAG, "Ignoring DUPLICATE share: nonce=0x%08lX, job=%lu", (unsigned long)nonce, (unsigned long)job_id);
        return;
    }
    slave_record_share(nonce, job_id);

    g_slave->shares_found++;

    // Build share structure with actual ASIC values
    cluster_share_t share = {
        .job_id = job_id,
        .nonce = nonce,
        .slave_id = g_slave->my_id,
        .version = version,    // Use actual rolled version from ASIC
        .ntime = ntime,        // Use actual ntime (may be rolled)
        .timestamp = esp_timer_get_time() / 1000
    };

    // Copy extranonce2 from current work
    xSemaphoreTake(g_slave->work_mutex, portMAX_DELAY);
    memcpy(share.extranonce2, g_slave->current_work.extranonce2, 8);
    share.extranonce2_len = g_slave->current_work.extranonce2_len;
    xSemaphoreGive(g_slave->work_mutex);

    ESP_LOGI(TAG, "Share found: nonce=0x%08lX, version=0x%08lX",
             (unsigned long)nonce, (unsigned long)version);

    // Queue for transmission to master
    if (xQueueSend(g_slave->share_queue, &share, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Share queue full, dropping share");
    }
}

// ============================================================================
// Registration & Heartbeat
// ============================================================================

/**
 * @brief Register with master node
 */
esp_err_t cluster_slave_register(const char *hostname)
{
    if (!g_slave) {
        return ESP_ERR_INVALID_STATE;
    }

    // Get our IP address for the master to display
    const char *ip_addr = cluster_get_ip_addr();

    // Build registration message with extended format (includes IP)
    char payload[128];
    int len = cluster_protocol_encode_register_ex(
        hostname ? hostname : "bitaxe",
        ip_addr ? ip_addr : "",
        payload,
        sizeof(payload));

    if (len < 0) {
        ESP_LOGE(TAG, "Failed to encode registration message");
        return ESP_FAIL;
    }

    // Send registration request
    extern esp_err_t BAP_uart_send_raw(const char *data, size_t len);
    esp_err_t ret = BAP_uart_send_raw(payload, len);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sent registration request as '%s' (IP: %s)",
                 hostname, ip_addr ? ip_addr : "unknown");
    }

    return ret;
}

/**
 * @brief Handle registration acknowledgment from master
 */
esp_err_t cluster_slave_handle_ack(uint8_t assigned_id, const char *hostname)
{
    if (!g_slave) {
        return ESP_ERR_INVALID_STATE;
    }

    g_slave->my_id = assigned_id;
    g_slave->registered = true;

    if (hostname) {
        strncpy(g_slave->master_hostname, hostname, 31);
        g_slave->master_hostname[31] = '\0';
    }

    ESP_LOGI(TAG, "Registered with master, assigned ID: %d", assigned_id);

    return ESP_OK;
}

/**
 * @brief Send heartbeat to master with extended stats
 */
static esp_err_t send_heartbeat(void)
{
    if (!g_slave || !g_slave->registered) {
        return ESP_ERR_INVALID_STATE;
    }

    // Gather current stats from ESP-Miner modules
    cluster_heartbeat_data_t hb_data = {
        .slave_id = g_slave->my_id,
        .hashrate = cluster_get_asic_hashrate(),
        .temp = cluster_get_chip_temp(),
        .fan_rpm = cluster_get_fan_rpm(),
        .shares = g_slave->shares_found,
        .frequency = cluster_get_asic_frequency(),
        .core_voltage = cluster_get_core_voltage(),
        .power = cluster_get_power(),
        .voltage_in = cluster_get_voltage_in()
    };

    // Build heartbeat payload with extended data
    char payload[128];
    int len = cluster_protocol_encode_heartbeat_ex(&hb_data, payload, sizeof(payload));

    if (len < 0) {
        ESP_LOGE(TAG, "Failed to encode heartbeat");
        return ESP_FAIL;
    }

    // Try ESP-NOW first if we have master MAC
#if defined(CONFIG_CLUSTER_TRANSPORT_ESPNOW) || defined(CONFIG_CLUSTER_TRANSPORT_BOTH)
    extern bool cluster_espnow_get_master_mac(uint8_t *mac);
    extern esp_err_t cluster_espnow_send(const uint8_t *dest_mac, const char *data, size_t len);

    uint8_t master_mac[6];
    if (cluster_espnow_get_master_mac(master_mac)) {
        ESP_LOGI(TAG, "Sending heartbeat via ESP-NOW to %02X:%02X:%02X:%02X:%02X:%02X",
                 master_mac[0], master_mac[1], master_mac[2],
                 master_mac[3], master_mac[4], master_mac[5]);
        esp_err_t ret = cluster_espnow_send(master_mac, payload, len);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Heartbeat sent via ESP-NOW OK");
            return ESP_OK;
        }
        ESP_LOGW(TAG, "ESP-NOW heartbeat failed: %s, falling back", esp_err_to_name(ret));
    } else {
        ESP_LOGW(TAG, "No master MAC - heartbeat via broadcast");
    }
#endif

    // Fallback to UART broadcast
    extern esp_err_t BAP_uart_send_raw(const char *data, size_t len);
    return BAP_uart_send_raw(payload, len);
}

// ============================================================================
// Tasks
// ============================================================================

/**
 * @brief Task: Send shares from queue to master
 */
static void share_sender_task(void *pvParameters)
{
    cluster_share_t share;

    ESP_LOGI(TAG, "Share sender task started");

    while (1) {
        if (xQueueReceive(g_slave->share_queue, &share, portMAX_DELAY) == pdTRUE) {
            cluster_slave_submit_share(&share);

            // Small delay between share submissions
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/**
 * @brief Task: Send periodic heartbeats to master
 */
static void heartbeat_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Heartbeat task started");

    // Initial registration
    const char *hostname = cluster_get_hostname();
    cluster_slave_register(hostname);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(CLUSTER_HEARTBEAT_MS));

        if (g_slave->registered) {
            send_heartbeat();
        } else {
            // Retry registration
            ESP_LOGI(TAG, "Retrying registration...");
            cluster_slave_register(hostname);
        }
    }
}

/**
 * @brief Task: Monitor for new work and feed ASIC
 *
 * In slave mode, this replaces the normal stratum work fetching
 */
static void worker_task(void *pvParameters)
{
    ESP_LOGW(TAG, "=== WORKER TASK STARTED ===");

    cluster_work_t work;
    uint32_t last_job_id = 0;
    uint8_t last_extranonce2[8] = {0};
    uint8_t last_en2_len = 0;
    uint32_t loop_count = 0;

    while (1) {
        // Wait for notification of new work (or timeout for polling)
        uint32_t notif = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        loop_count++;

        // Log every 10 iterations to show we're alive
        if (loop_count % 10 == 1) {
            ESP_LOGW(TAG, "worker_task loop %lu: notif=%lu, work_valid=%d",
                     (unsigned long)loop_count, (unsigned long)notif,
                     g_slave ? g_slave->work_valid : -1);
        }

        if (!g_slave) {
            ESP_LOGE(TAG, "worker_task: g_slave is NULL!");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!g_slave->work_valid) {
            // Only log occasionally when waiting for work
            if (loop_count % 10 == 1) {
                ESP_LOGW(TAG, "worker_task: work_valid=false, waiting for work...");
            }
            continue;
        }

        // Get current work
        if (cluster_slave_get_work(&work) != ESP_OK) {
            ESP_LOGW(TAG, "worker_task: Failed to get work from queue");
            continue;
        }

        // Check if this is new work - compare BOTH job_id AND extranonce2
        // Different extranonce2 means different merkle root = different work!
        bool job_changed = (work.job_id != last_job_id);
        bool en2_changed = (work.extranonce2_len != last_en2_len) ||
                           (memcmp(work.extranonce2, last_extranonce2, work.extranonce2_len) != 0);

        if (job_changed || en2_changed) {
            // Log the change
            char en2_hex[17];
            for (int i = 0; i < work.extranonce2_len && i < 8; i++) {
                sprintf(en2_hex + i * 2, "%02x", work.extranonce2[i]);
            }
            en2_hex[work.extranonce2_len * 2] = '\0';

            ESP_LOGW(TAG, "*** NEW WORK: job %lu, en2=%s (job_changed=%d, en2_changed=%d) ***",
                     (unsigned long)work.job_id, en2_hex, job_changed, en2_changed);

            // Update tracking
            last_job_id = work.job_id;
            memcpy(last_extranonce2, work.extranonce2, work.extranonce2_len);
            last_en2_len = work.extranonce2_len;

            // Submit work to ASIC via integration layer
            cluster_submit_work_to_asic(&work);

            ESP_LOGW(TAG, "*** cluster_submit_work_to_asic returned ***");
        } else {
            // Only log occasionally to reduce spam
            if (loop_count % 10 == 1) {
                ESP_LOGD(TAG, "Same work (job=%lu), not resubmitting", (unsigned long)work.job_id);
            }
        }

        // Check for stale work
        int64_t age = (esp_timer_get_time() / 1000) - g_slave->last_work_received;
        if (age > 30000) {  // 30 seconds
            ESP_LOGW(TAG, "Work is stale (%lld ms old)", age);
        }
    }
}

// ============================================================================
// Initialization
// ============================================================================

esp_err_t cluster_slave_init(cluster_slave_state_t *state)
{
    ESP_LOGW(TAG, "=== CLUSTER SLAVE INIT CALLED ===");

    if (!state) {
        ESP_LOGE(TAG, "cluster_slave_init: state is NULL!");
        return ESP_ERR_INVALID_ARG;
    }

    g_slave = state;
    ESP_LOGW(TAG, "g_slave set to %p", (void*)g_slave);

    // Initialize synchronization primitives
    g_slave->work_mutex = xSemaphoreCreateMutex();
    g_slave->share_queue = xQueueCreate(CLUSTER_SHARE_QUEUE_SIZE,
                                         sizeof(cluster_share_t));

    if (!g_slave->work_mutex || !g_slave->share_queue) {
        ESP_LOGE(TAG, "Failed to create synchronization primitives");
        return ESP_ERR_NO_MEM;
    }

    // Initialize state
    g_slave->registered = false;
    g_slave->work_valid = false;
    g_slave->my_id = 0xFF;  // Invalid until assigned
    g_slave->shares_found = 0;
    g_slave->shares_submitted = 0;

    ESP_LOGW(TAG, "Creating worker_task...");
    // Create tasks with balanced stack sizes (avoid memory exhaustion)
    BaseType_t ret = xTaskCreate(worker_task, "cluster_worker", 3072, NULL, 6,
                &g_slave->worker_task);
    ESP_LOGW(TAG, "worker_task create result: %d, handle: %p",
             ret, (void*)g_slave->worker_task);

    xTaskCreate(heartbeat_task, "cluster_hb", 3072, NULL, 4,
                &g_slave->heartbeat_task);

    TaskHandle_t share_task;
    xTaskCreate(share_sender_task, "cluster_shares", 3072, NULL, 5,
                &share_task);

    g_slave->initialized = true;

    ESP_LOGW(TAG, "=== CLUSTER SLAVE INIT COMPLETE ===");

    return ESP_OK;
}

void cluster_slave_deinit(void)
{
    if (!g_slave) return;

    // Stop tasks
    if (g_slave->worker_task) {
        vTaskDelete(g_slave->worker_task);
    }
    if (g_slave->heartbeat_task) {
        vTaskDelete(g_slave->heartbeat_task);
    }

    // Clean up resources
    if (g_slave->work_mutex) {
        vSemaphoreDelete(g_slave->work_mutex);
    }
    if (g_slave->share_queue) {
        vQueueDelete(g_slave->share_queue);
    }

    g_slave->initialized = false;
    g_slave = NULL;

    ESP_LOGI(TAG, "Cluster slave deinitialized");
}

void cluster_slave_get_shares(uint32_t *shares_found, uint32_t *shares_submitted)
{
    if (shares_found) {
        *shares_found = g_slave ? g_slave->shares_found : 0;
    }
    if (shares_submitted) {
        *shares_submitted = g_slave ? g_slave->shares_submitted : 0;
    }
}

#endif // CLUSTER_ENABLED && CLUSTER_IS_SLAVE
