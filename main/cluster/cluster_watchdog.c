/**
 * @file cluster_watchdog.c
 * @brief Cluster Safety Watchdog Implementation
 *
 * Monitors temperature and input voltage on master and all slaves.
 * Takes protective action (reduces frequency/voltage) when thresholds exceeded.
 *
 * @author ClusterAxe Project
 * @license GPL-3.0
 */

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

#include "cluster_watchdog.h"
#include "cluster.h"
#include "cluster_integration.h"
#include "nvs_config.h"
#include "http_server.h"
#include "global_state.h"

static const char *TAG = "cluster_watchdog";

// ============================================================================
// State
// ============================================================================

static struct {
    bool                    initialized;
    bool                    enabled;
    bool                    running;
    SemaphoreHandle_t       mutex;
    TaskHandle_t            task_handle;
    watchdog_device_status_t master;
    watchdog_device_status_t slaves[CLUSTER_MAX_SLAVES];
    uint8_t                 throttled_count;
} s_watchdog = {0};

// ============================================================================
// Forward Declarations
// ============================================================================

static void watchdog_task(void *pvParameters);
static void check_master_device(void);
#if CLUSTER_IS_MASTER
static void check_slave_device(uint8_t slot);
static void throttle_slave(uint8_t slot, uint8_t reason);
static void recover_slave(uint8_t slot);
#endif
static void throttle_master(uint8_t reason);
static void recover_master(void);

// ============================================================================
// Public API
// ============================================================================

esp_err_t cluster_watchdog_init(void)
{
    if (s_watchdog.initialized) {
        return ESP_OK;
    }

    s_watchdog.mutex = xSemaphoreCreateMutex();
    if (!s_watchdog.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize device statuses
    memset(&s_watchdog.master, 0, sizeof(s_watchdog.master));
    memset(s_watchdog.slaves, 0, sizeof(s_watchdog.slaves));

    // Load enabled state from NVS
    nvs_handle_t nvs;
    if (nvs_open("watchdog", NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t enabled = 0;
        if (nvs_get_u8(nvs, "enabled", &enabled) == ESP_OK) {
            s_watchdog.enabled = (enabled != 0);
        }
        nvs_close(nvs);
    }

    s_watchdog.initialized = true;
    ESP_LOGI(TAG, "Watchdog initialized (enabled=%d)", s_watchdog.enabled);

    // Start task if enabled
    if (s_watchdog.enabled) {
        cluster_watchdog_enable(true);
    }

    return ESP_OK;
}

esp_err_t cluster_watchdog_enable(bool enable)
{
    if (!s_watchdog.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_watchdog.mutex, portMAX_DELAY);

    if (enable && !s_watchdog.running) {
        // Start watchdog task
        BaseType_t ret = xTaskCreate(
            watchdog_task,
            "watchdog",
            4096,
            NULL,
            5,  // Medium priority
            &s_watchdog.task_handle
        );
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create watchdog task");
            xSemaphoreGive(s_watchdog.mutex);
            return ESP_ERR_NO_MEM;
        }
        s_watchdog.running = true;
        ESP_LOGI(TAG, "Watchdog task started");
    } else if (!enable && s_watchdog.running) {
        // Stop watchdog task
        if (s_watchdog.task_handle) {
            vTaskDelete(s_watchdog.task_handle);
            s_watchdog.task_handle = NULL;
        }
        s_watchdog.running = false;
        ESP_LOGI(TAG, "Watchdog task stopped");
    }

    s_watchdog.enabled = enable;

    // Save to NVS
    nvs_handle_t nvs;
    if (nvs_open("watchdog", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "enabled", enable ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    xSemaphoreGive(s_watchdog.mutex);
    return ESP_OK;
}

bool cluster_watchdog_is_enabled(void)
{
    return s_watchdog.enabled;
}

bool cluster_watchdog_is_running(void)
{
    return s_watchdog.running;
}

void cluster_watchdog_get_status(watchdog_status_t *status)
{
    if (!status) return;

    // Safe to call even if not initialized
    if (!s_watchdog.initialized || !s_watchdog.mutex) {
        memset(status, 0, sizeof(*status));
        return;
    }

    xSemaphoreTake(s_watchdog.mutex, portMAX_DELAY);

    status->enabled = s_watchdog.enabled;
    status->running = s_watchdog.running;
    status->master = s_watchdog.master;
    memcpy(status->slaves, s_watchdog.slaves, sizeof(status->slaves));
    status->throttled_count = s_watchdog.throttled_count;

    xSemaphoreGive(s_watchdog.mutex);
}

uint8_t cluster_watchdog_get_throttled_count(void)
{
    return s_watchdog.throttled_count;
}

// ============================================================================
// Watchdog Task
// ============================================================================

static void watchdog_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Watchdog task running - checking every %d ms", WATCHDOG_CHECK_INTERVAL_MS);
    ESP_LOGI(TAG, "Thresholds: Temp >= %.1f C, Vin <= %.1f V",
             WATCHDOG_TEMP_THRESHOLD, WATCHDOG_VIN_THRESHOLD);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_CHECK_INTERVAL_MS));

        if (!s_watchdog.enabled) {
            continue;
        }

        xSemaphoreTake(s_watchdog.mutex, portMAX_DELAY);

        // Check master device
        check_master_device();

#if CLUSTER_IS_MASTER
        // Check slave devices (only in master mode)
        cluster_mode_t mode = cluster_get_mode();
        if (mode == CLUSTER_MODE_MASTER) {
            for (uint8_t i = 0; i < CLUSTER_MAX_SLAVES; i++) {
                check_slave_device(i);
            }
        }
#endif

        // Update throttled count
        s_watchdog.throttled_count = 0;
        if (s_watchdog.master.is_throttled) {
            s_watchdog.throttled_count++;
        }
        for (uint8_t i = 0; i < CLUSTER_MAX_SLAVES; i++) {
            if (s_watchdog.slaves[i].is_throttled) {
                s_watchdog.throttled_count++;
            }
        }

        xSemaphoreGive(s_watchdog.mutex);
    }
}

// ============================================================================
// Device Checking
// ============================================================================

static void check_master_device(void)
{
    // Get current readings from cluster integration layer
    float temp = cluster_get_chip_temp();
    float vin = cluster_get_voltage_in();
    uint16_t freq = cluster_get_asic_frequency();
    uint16_t voltage = cluster_get_core_voltage();

    s_watchdog.master.last_temp = temp;
    s_watchdog.master.last_vin = vin;
    s_watchdog.master.current_frequency = freq;
    s_watchdog.master.current_voltage = voltage;

    // Determine throttle reason
    uint8_t reason = THROTTLE_NONE;
    if (temp >= WATCHDOG_TEMP_THRESHOLD) {
        reason |= THROTTLE_TEMP_HIGH;
    }
    if (vin > 0 && vin <= WATCHDOG_VIN_THRESHOLD) {
        reason |= THROTTLE_VIN_LOW;
    }

    if (reason != THROTTLE_NONE) {
        // Need to throttle
        if (!s_watchdog.master.is_throttled) {
            // Store original values before first throttle
            s_watchdog.master.original_frequency = freq;
            s_watchdog.master.original_voltage = voltage;
        }
        // Reset recovery state - conditions are not safe
        s_watchdog.master.safe_since = 0;
        s_watchdog.master.is_recovering = false;
        throttle_master(reason);
    } else if (s_watchdog.master.is_throttled) {
        // Conditions no longer dangerous - check for recovery
        s_watchdog.master.throttle_reason = THROTTLE_NONE;

        // Check if conditions are within recovery thresholds (hysteresis)
        bool temp_safe = (temp <= WATCHDOG_TEMP_RECOVERY_THRESHOLD);
        bool vin_safe = (vin <= 0 || vin >= WATCHDOG_VIN_RECOVERY_THRESHOLD);

        if (temp_safe && vin_safe) {
            // Conditions are safe for recovery
            int64_t now = esp_timer_get_time() / 1000;

            if (s_watchdog.master.safe_since == 0) {
                // Just became safe - start tracking
                s_watchdog.master.safe_since = now;
                ESP_LOGI(TAG, "MASTER: Conditions safe, starting stability timer");
            }

            // Check if stable for long enough
            int64_t stable_duration = now - s_watchdog.master.safe_since;
            if (stable_duration >= WATCHDOG_RECOVERY_STABILITY_MS) {
                // Attempt recovery
                recover_master();
            }
        } else {
            // Not within recovery thresholds yet - reset stability timer
            if (s_watchdog.master.safe_since != 0) {
                ESP_LOGI(TAG, "MASTER: Conditions not yet safe for recovery (temp=%.1f, vin=%.2f)",
                         temp, vin);
            }
            s_watchdog.master.safe_since = 0;
            s_watchdog.master.is_recovering = false;
        }
    }
}

#if CLUSTER_IS_MASTER
static void check_slave_device(uint8_t slot)
{
    cluster_slave_t slave_info;
    if (cluster_master_get_slave_info(slot, &slave_info) != ESP_OK) {
        // No slave in this slot
        s_watchdog.slaves[slot].is_throttled = false;
        s_watchdog.slaves[slot].is_recovering = false;
        s_watchdog.slaves[slot].safe_since = 0;
        return;
    }

    // Only check active slaves
    if (slave_info.state != SLAVE_STATE_ACTIVE) {
        return;
    }

    float temp = slave_info.temperature;
    float vin = slave_info.voltage_in;
    uint16_t freq = slave_info.frequency;
    uint16_t voltage = slave_info.core_voltage;

    s_watchdog.slaves[slot].last_temp = temp;
    s_watchdog.slaves[slot].last_vin = vin;
    s_watchdog.slaves[slot].current_frequency = freq;
    s_watchdog.slaves[slot].current_voltage = voltage;

    // Determine throttle reason
    uint8_t reason = THROTTLE_NONE;
    if (temp >= WATCHDOG_TEMP_THRESHOLD) {
        reason |= THROTTLE_TEMP_HIGH;
    }
    if (vin > 0 && vin <= WATCHDOG_VIN_THRESHOLD) {
        reason |= THROTTLE_VIN_LOW;
    }

    if (reason != THROTTLE_NONE) {
        // Need to throttle
        if (!s_watchdog.slaves[slot].is_throttled) {
            // Store original values before first throttle
            s_watchdog.slaves[slot].original_frequency = freq;
            s_watchdog.slaves[slot].original_voltage = voltage;
        }
        // Reset recovery state - conditions are not safe
        s_watchdog.slaves[slot].safe_since = 0;
        s_watchdog.slaves[slot].is_recovering = false;
        throttle_slave(slot, reason);
    } else if (s_watchdog.slaves[slot].is_throttled) {
        // Conditions no longer dangerous - check for recovery
        s_watchdog.slaves[slot].throttle_reason = THROTTLE_NONE;

        // Check if conditions are within recovery thresholds (hysteresis)
        bool temp_safe = (temp <= WATCHDOG_TEMP_RECOVERY_THRESHOLD);
        bool vin_safe = (vin <= 0 || vin >= WATCHDOG_VIN_RECOVERY_THRESHOLD);

        if (temp_safe && vin_safe) {
            // Conditions are safe for recovery
            int64_t now = esp_timer_get_time() / 1000;

            if (s_watchdog.slaves[slot].safe_since == 0) {
                // Just became safe - start tracking
                s_watchdog.slaves[slot].safe_since = now;
                ESP_LOGI(TAG, "SLAVE %d: Conditions safe, starting stability timer", slot);
            }

            // Check if stable for long enough
            int64_t stable_duration = now - s_watchdog.slaves[slot].safe_since;
            if (stable_duration >= WATCHDOG_RECOVERY_STABILITY_MS) {
                // Attempt recovery
                recover_slave(slot);
            }
        } else {
            // Not within recovery thresholds yet - reset stability timer
            if (s_watchdog.slaves[slot].safe_since != 0) {
                ESP_LOGI(TAG, "SLAVE %d: Conditions not yet safe for recovery (temp=%.1f, vin=%.2f)",
                         slot, temp, vin);
            }
            s_watchdog.slaves[slot].safe_since = 0;
            s_watchdog.slaves[slot].is_recovering = false;
        }
    }
}

#endif // CLUSTER_IS_MASTER (check_slave_device)

// ============================================================================
// Throttling Actions
// ============================================================================

static void throttle_master(uint8_t reason)
{
    uint16_t current_freq = s_watchdog.master.current_frequency;
    uint16_t current_voltage = s_watchdog.master.current_voltage;

    // Calculate new values (reduce by step, but not below minimum)
    uint16_t new_freq = current_freq;
    uint16_t new_voltage = current_voltage;

    if (current_freq > WATCHDOG_MIN_FREQUENCY) {
        new_freq = current_freq - WATCHDOG_FREQ_STEP;
        if (new_freq < WATCHDOG_MIN_FREQUENCY) {
            new_freq = WATCHDOG_MIN_FREQUENCY;
        }
    }

    if (current_voltage > WATCHDOG_MIN_VOLTAGE) {
        new_voltage = current_voltage - WATCHDOG_VOLTAGE_STEP;
        if (new_voltage < WATCHDOG_MIN_VOLTAGE) {
            new_voltage = WATCHDOG_MIN_VOLTAGE;
        }
    }

    // Apply new settings if changed
    bool changed = false;
    if (new_freq != current_freq) {
        ESP_LOGW(TAG, "MASTER throttle: freq %d -> %d MHz (reason: 0x%02X)",
                 current_freq, new_freq, reason);
        nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, (float)new_freq);
        changed = true;
    }
    if (new_voltage != current_voltage) {
        ESP_LOGW(TAG, "MASTER throttle: voltage %d -> %d mV (reason: 0x%02X)",
                 current_voltage, new_voltage, reason);
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, new_voltage);
        changed = true;
    }

    if (changed) {
        s_watchdog.master.is_throttled = true;
        s_watchdog.master.throttle_reason = reason;
        s_watchdog.master.throttle_count++;
        s_watchdog.master.last_throttle_time = esp_timer_get_time() / 1000;
        s_watchdog.master.current_frequency = new_freq;
        s_watchdog.master.current_voltage = new_voltage;
    }
}

#if CLUSTER_IS_MASTER
static void throttle_slave(uint8_t slot, uint8_t reason)
{
    // Get slave info to get IP address
    cluster_slave_t slave_info;
    if (cluster_master_get_slave_info(slot, &slave_info) != ESP_OK) {
        return;
    }

    // Check if slave has a valid IP for HTTP
    if (slave_info.ip_addr[0] == '\0' || strcmp(slave_info.ip_addr, "N/A") == 0) {
        // No IP - can't send HTTP, just mark as needing attention
        if (!s_watchdog.slaves[slot].is_throttled) {
            ESP_LOGW(TAG, "SLAVE %d needs attention but has no IP for HTTP throttling", slot);
            s_watchdog.slaves[slot].is_throttled = true;
            s_watchdog.slaves[slot].throttle_reason = reason;
            s_watchdog.slaves[slot].throttle_count++;
            s_watchdog.slaves[slot].last_throttle_time = esp_timer_get_time() / 1000;
        }
        return;
    }

    uint16_t current_freq = s_watchdog.slaves[slot].current_frequency;
    uint16_t current_voltage = s_watchdog.slaves[slot].current_voltage;

    // Calculate new values (reduce by step, but not below minimum)
    uint16_t new_freq = current_freq;
    uint16_t new_voltage = current_voltage;

    if (current_freq > WATCHDOG_MIN_FREQUENCY) {
        new_freq = current_freq - WATCHDOG_FREQ_STEP;
        if (new_freq < WATCHDOG_MIN_FREQUENCY) {
            new_freq = WATCHDOG_MIN_FREQUENCY;
        }
    }

    if (current_voltage > WATCHDOG_MIN_VOLTAGE) {
        new_voltage = current_voltage - WATCHDOG_VOLTAGE_STEP;
        if (new_voltage < WATCHDOG_MIN_VOLTAGE) {
            new_voltage = WATCHDOG_MIN_VOLTAGE;
        }
    }

    // Apply new settings via HTTP PATCH
    bool changed = false;
    char patch_data[128];
    char *response = NULL;

    if (new_freq != current_freq) {
        ESP_LOGW(TAG, "SLAVE %d (%s) throttle: freq %d -> %d MHz (reason: 0x%02X)",
                 slot, slave_info.ip_addr, current_freq, new_freq, reason);
        snprintf(patch_data, sizeof(patch_data), "{\"frequency\":%d}", new_freq);
        esp_err_t err = http_proxy_to_slave(slave_info.ip_addr, "/api/system", HTTP_METHOD_PATCH, patch_data, &response);
        if (response) free(response);
        if (err == ESP_OK) {
            changed = true;
            s_watchdog.slaves[slot].current_frequency = new_freq;
        } else {
            ESP_LOGE(TAG, "Failed to apply frequency to slave %d", slot);
        }
    }

    if (new_voltage != current_voltage) {
        ESP_LOGW(TAG, "SLAVE %d (%s) throttle: voltage %d -> %d mV (reason: 0x%02X)",
                 slot, slave_info.ip_addr, current_voltage, new_voltage, reason);
        snprintf(patch_data, sizeof(patch_data), "{\"coreVoltage\":%d}", new_voltage);
        esp_err_t err = http_proxy_to_slave(slave_info.ip_addr, "/api/system", HTTP_METHOD_PATCH, patch_data, &response);
        if (response) free(response);
        if (err == ESP_OK) {
            changed = true;
            s_watchdog.slaves[slot].current_voltage = new_voltage;
        } else {
            ESP_LOGE(TAG, "Failed to apply voltage to slave %d", slot);
        }
    }

    if (changed || !s_watchdog.slaves[slot].is_throttled) {
        s_watchdog.slaves[slot].is_throttled = true;
        s_watchdog.slaves[slot].throttle_reason = reason;
        s_watchdog.slaves[slot].throttle_count++;
        s_watchdog.slaves[slot].last_throttle_time = esp_timer_get_time() / 1000;
    }
}

#endif // CLUSTER_IS_MASTER (throttle_slave)

// ============================================================================
// Recovery Actions
// ============================================================================

static void recover_master(void)
{
    int64_t now = esp_timer_get_time() / 1000;

    // Check if enough time has passed since last recovery step
    if (s_watchdog.master.last_recovery_time != 0) {
        int64_t since_last = now - s_watchdog.master.last_recovery_time;
        if (since_last < WATCHDOG_RECOVERY_INTERVAL_MS) {
            return;  // Too soon for next step
        }
    }

    uint16_t current_freq = s_watchdog.master.current_frequency;
    uint16_t current_voltage = s_watchdog.master.current_voltage;
    uint16_t target_freq = s_watchdog.master.original_frequency;
    uint16_t target_voltage = s_watchdog.master.original_voltage;

    // Check if already recovered
    if (current_freq >= target_freq && current_voltage >= target_voltage) {
        // Fully recovered
        ESP_LOGI(TAG, "MASTER: Recovery complete - restored to freq=%d MHz, voltage=%d mV",
                 target_freq, target_voltage);
        s_watchdog.master.is_throttled = false;
        s_watchdog.master.is_recovering = false;
        s_watchdog.master.safe_since = 0;
        s_watchdog.master.last_recovery_time = 0;
        return;
    }

    // Mark as recovering
    if (!s_watchdog.master.is_recovering) {
        ESP_LOGI(TAG, "MASTER: Starting recovery (target freq=%d MHz, voltage=%d mV)",
                 target_freq, target_voltage);
        s_watchdog.master.is_recovering = true;
    }

    // Step up frequency and voltage toward original values
    bool changed = false;
    uint16_t new_freq = current_freq;
    uint16_t new_voltage = current_voltage;

    if (current_freq < target_freq) {
        new_freq = current_freq + WATCHDOG_FREQ_STEP;
        if (new_freq > target_freq) {
            new_freq = target_freq;
        }
    }

    if (current_voltage < target_voltage) {
        new_voltage = current_voltage + WATCHDOG_VOLTAGE_STEP;
        if (new_voltage > target_voltage) {
            new_voltage = target_voltage;
        }
    }

    // Apply new settings if changed
    if (new_freq != current_freq) {
        ESP_LOGI(TAG, "MASTER recovery: freq %d -> %d MHz (target: %d)",
                 current_freq, new_freq, target_freq);
        nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, (float)new_freq);
        s_watchdog.master.current_frequency = new_freq;
        changed = true;
    }
    if (new_voltage != current_voltage) {
        ESP_LOGI(TAG, "MASTER recovery: voltage %d -> %d mV (target: %d)",
                 current_voltage, new_voltage, target_voltage);
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, new_voltage);
        s_watchdog.master.current_voltage = new_voltage;
        changed = true;
    }

    if (changed) {
        s_watchdog.master.last_recovery_time = now;
    }
}

#if CLUSTER_IS_MASTER
static void recover_slave(uint8_t slot)
{
    int64_t now = esp_timer_get_time() / 1000;

    // Check if enough time has passed since last recovery step
    if (s_watchdog.slaves[slot].last_recovery_time != 0) {
        int64_t since_last = now - s_watchdog.slaves[slot].last_recovery_time;
        if (since_last < WATCHDOG_RECOVERY_INTERVAL_MS) {
            return;  // Too soon for next step
        }
    }

    // Get slave info for IP address
    cluster_slave_t slave_info;
    if (cluster_master_get_slave_info(slot, &slave_info) != ESP_OK) {
        return;
    }

    // Check if slave has a valid IP for HTTP
    if (slave_info.ip_addr[0] == '\0' || strcmp(slave_info.ip_addr, "N/A") == 0) {
        ESP_LOGW(TAG, "SLAVE %d: Cannot recover - no IP for HTTP", slot);
        return;
    }

    uint16_t current_freq = s_watchdog.slaves[slot].current_frequency;
    uint16_t current_voltage = s_watchdog.slaves[slot].current_voltage;
    uint16_t target_freq = s_watchdog.slaves[slot].original_frequency;
    uint16_t target_voltage = s_watchdog.slaves[slot].original_voltage;

    // Check if already recovered
    if (current_freq >= target_freq && current_voltage >= target_voltage) {
        // Fully recovered
        ESP_LOGI(TAG, "SLAVE %d: Recovery complete - restored to freq=%d MHz, voltage=%d mV",
                 slot, target_freq, target_voltage);
        s_watchdog.slaves[slot].is_throttled = false;
        s_watchdog.slaves[slot].is_recovering = false;
        s_watchdog.slaves[slot].safe_since = 0;
        s_watchdog.slaves[slot].last_recovery_time = 0;
        return;
    }

    // Mark as recovering
    if (!s_watchdog.slaves[slot].is_recovering) {
        ESP_LOGI(TAG, "SLAVE %d (%s): Starting recovery (target freq=%d MHz, voltage=%d mV)",
                 slot, slave_info.ip_addr, target_freq, target_voltage);
        s_watchdog.slaves[slot].is_recovering = true;
    }

    // Step up frequency and voltage toward original values
    uint16_t new_freq = current_freq;
    uint16_t new_voltage = current_voltage;

    if (current_freq < target_freq) {
        new_freq = current_freq + WATCHDOG_FREQ_STEP;
        if (new_freq > target_freq) {
            new_freq = target_freq;
        }
    }

    if (current_voltage < target_voltage) {
        new_voltage = current_voltage + WATCHDOG_VOLTAGE_STEP;
        if (new_voltage > target_voltage) {
            new_voltage = target_voltage;
        }
    }

    // Apply new settings via HTTP PATCH
    bool changed = false;
    char patch_data[128];
    char *response = NULL;

    if (new_freq != current_freq) {
        ESP_LOGI(TAG, "SLAVE %d (%s) recovery: freq %d -> %d MHz (target: %d)",
                 slot, slave_info.ip_addr, current_freq, new_freq, target_freq);
        snprintf(patch_data, sizeof(patch_data), "{\"frequency\":%d}", new_freq);
        esp_err_t err = http_proxy_to_slave(slave_info.ip_addr, "/api/system", HTTP_METHOD_PATCH, patch_data, &response);
        if (response) free(response);
        if (err == ESP_OK) {
            s_watchdog.slaves[slot].current_frequency = new_freq;
            changed = true;
        } else {
            ESP_LOGE(TAG, "Failed to apply frequency to slave %d during recovery", slot);
        }
    }

    if (new_voltage != current_voltage) {
        ESP_LOGI(TAG, "SLAVE %d (%s) recovery: voltage %d -> %d mV (target: %d)",
                 slot, slave_info.ip_addr, current_voltage, new_voltage, target_voltage);
        snprintf(patch_data, sizeof(patch_data), "{\"coreVoltage\":%d}", new_voltage);
        esp_err_t err = http_proxy_to_slave(slave_info.ip_addr, "/api/system", HTTP_METHOD_PATCH, patch_data, &response);
        if (response) free(response);
        if (err == ESP_OK) {
            s_watchdog.slaves[slot].current_voltage = new_voltage;
            changed = true;
        } else {
            ESP_LOGE(TAG, "Failed to apply voltage to slave %d during recovery", slot);
        }
    }

    if (changed) {
        s_watchdog.slaves[slot].last_recovery_time = now;
    }
}
#endif // CLUSTER_IS_MASTER
