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
 */
esp_err_t cluster_slave_receive_work(const cluster_work_t *work)
{
    if (!g_slave || !work) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(g_slave->work_mutex, portMAX_DELAY);

    // Store the new work
    memcpy(&g_slave->current_work, work, sizeof(cluster_work_t));
    g_slave->work_valid = true;
    g_slave->last_work_received = esp_timer_get_time() / 1000;

    xSemaphoreGive(g_slave->work_mutex);

    ESP_LOGI(TAG, "Received work: job %lu, nonce range 0x%08lX - 0x%08lX",
             (unsigned long)work->job_id,
             (unsigned long)work->nonce_start,
             (unsigned long)work->nonce_end);

    // Notify worker task that new work is available
    if (g_slave->worker_task) {
        xTaskNotifyGive(g_slave->worker_task);
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

    // Send to master
    extern esp_err_t BAP_uart_send_raw(const char *data, size_t len);
    esp_err_t ret = BAP_uart_send_raw(payload, len);

    if (ret == ESP_OK) {
        g_slave->shares_submitted++;
        ESP_LOGI(TAG, "Submitted share: job %lu, nonce 0x%08lX",
                 (unsigned long)share->job_id,
                 (unsigned long)share->nonce);
    }

    return ret;
}

/**
 * @brief Called by ASIC driver when a share is found
 * This integrates with the existing ESP-Miner ASIC result handling
 */
void cluster_slave_on_share_found(uint32_t nonce, uint32_t job_id)
{
    if (!g_slave || !g_slave->work_valid) {
        return;
    }

    g_slave->shares_found++;

    // Build share structure
    cluster_share_t share = {
        .job_id = job_id,
        .nonce = nonce,
        .slave_id = g_slave->my_id,
        .timestamp = esp_timer_get_time() / 1000
    };

    // Copy extranonce2 from current work
    xSemaphoreTake(g_slave->work_mutex, portMAX_DELAY);
    memcpy(share.extranonce2, g_slave->current_work.extranonce2, 8);
    share.extranonce2_len = g_slave->current_work.extranonce2_len;
    share.ntime = g_slave->current_work.ntime;
    share.version = g_slave->current_work.version;
    xSemaphoreGive(g_slave->work_mutex);

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
    ESP_LOGI(TAG, "Worker task started");

    cluster_work_t work;
    uint32_t last_job_id = 0;

    while (1) {
        // Wait for notification of new work (or timeout for polling)
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));

        if (!g_slave->work_valid) {
            continue;
        }

        // Get current work
        if (cluster_slave_get_work(&work) != ESP_OK) {
            continue;
        }

        // Check if this is new work
        if (work.job_id != last_job_id) {
            last_job_id = work.job_id;

            ESP_LOGI(TAG, "Loading new work: job %lu", (unsigned long)work.job_id);

            // Submit work to ASIC via integration layer
            // This will be connected to the create_jobs_task
            cluster_submit_work_to_asic(&work);
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
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }

    g_slave = state;

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

    // Create tasks
    xTaskCreate(worker_task, "cluster_worker", 4096, NULL, 6,
                &g_slave->worker_task);
    xTaskCreate(heartbeat_task, "cluster_hb", 2048, NULL, 4,
                &g_slave->heartbeat_task);

    // Share sender task (lower priority than worker)
    TaskHandle_t share_task;
    xTaskCreate(share_sender_task, "cluster_shares", 2048, NULL, 5,
                &share_task);

    g_slave->initialized = true;

    ESP_LOGI(TAG, "Cluster slave initialized, waiting for master...");

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

#endif // CLUSTER_ENABLED && CLUSTER_IS_SLAVE
