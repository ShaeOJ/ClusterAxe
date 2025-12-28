/**
 * @file cluster_autotune.c
 * @brief ClusterAxe Auto-Tuning Implementation
 *
 * Implements automatic frequency and voltage optimization for maximum efficiency.
 *
 * @author ClusterAxe Project
 * @license GPL-3.0
 */

#include "cluster_autotune.h"
#include "cluster_config.h"
#include "cluster_integration.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_config.h"
#include "asic.h"
#include "power/vcore.h"
#include "global_state.h"
#include <string.h>
#include <math.h>

static const char *TAG = "autotune";

// ============================================================================
// Configuration
// ============================================================================

#define AUTOTUNE_STABILIZE_TIME_MS    30000   // Wait 30s for hashrate to stabilize
#define AUTOTUNE_TEST_TIME_MS         60000   // Test each setting for 60s
#define AUTOTUNE_MIN_HASHRATE_RATIO   0.90f   // Minimum 90% of expected hashrate
#define AUTOTUNE_TASK_STACK_SIZE      4096
#define AUTOTUNE_TASK_PRIORITY        5

// Frequency/voltage step sizes
#define FREQ_STEP_MHZ     25
#define VOLTAGE_STEP_MV   50

// Limits (will be adjusted based on device)
#define FREQ_MIN_MHZ      400
#define FREQ_MAX_MHZ      700
#define VOLTAGE_MIN_MV    1000
#define VOLTAGE_MAX_MV    1350

// ============================================================================
// State
// ============================================================================

static struct {
    autotune_status_t status;
    bool initialized;
    bool enabled;           // Runtime enabled flag (not persisted)
    bool task_running;
    TaskHandle_t task_handle;
    SemaphoreHandle_t mutex;

    // Measurement accumulators
    float hashrate_sum;
    float power_sum;
    uint32_t sample_count;
    uint32_t test_start_time;
    uint32_t autotune_start_time;

    // Global state reference
    GlobalState *global_state;
} g_autotune = {0};

// ============================================================================
// Helper Functions
// ============================================================================

static void lock(void)
{
    if (g_autotune.mutex) {
        xSemaphoreTake(g_autotune.mutex, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (g_autotune.mutex) {
        xSemaphoreGive(g_autotune.mutex);
    }
}

static float get_current_hashrate(void)
{
    return cluster_get_asic_hashrate() / 100.0f;  // Convert from GH/s * 100
}

static float get_current_power(void)
{
    return cluster_get_power();
}

static float calculate_efficiency(float hashrate_gh, float power_w)
{
    if (hashrate_gh <= 0 || power_w <= 0) {
        return 999999.0f;  // Invalid - return very high (bad) efficiency
    }
    // J/TH = (Watts * 1000) / (GH/s)
    // Since we have GH/s and want TH/s: hashrate_th = hashrate_gh / 1000
    // J/TH = Watts / (hashrate_gh / 1000) = (Watts * 1000) / hashrate_gh
    return (power_w * 1000.0f) / hashrate_gh;
}

static void reset_measurements(void)
{
    g_autotune.hashrate_sum = 0;
    g_autotune.power_sum = 0;
    g_autotune.sample_count = 0;
    g_autotune.test_start_time = esp_timer_get_time() / 1000;
}

static void collect_sample(void)
{
    g_autotune.hashrate_sum += get_current_hashrate();
    g_autotune.power_sum += get_current_power();
    g_autotune.sample_count++;
}

static void get_average_measurements(float *avg_hashrate, float *avg_power)
{
    if (g_autotune.sample_count > 0) {
        *avg_hashrate = g_autotune.hashrate_sum / g_autotune.sample_count;
        *avg_power = g_autotune.power_sum / g_autotune.sample_count;
    } else {
        *avg_hashrate = 0;
        *avg_power = 0;
    }
}

// ============================================================================
// API Implementation
// ============================================================================

esp_err_t cluster_autotune_init(void)
{
    if (g_autotune.initialized) {
        return ESP_OK;
    }

    g_autotune.mutex = xSemaphoreCreateMutex();
    if (!g_autotune.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(&g_autotune.status, 0, sizeof(g_autotune.status));
    g_autotune.status.state = AUTOTUNE_STATE_IDLE;
    g_autotune.status.mode = AUTOTUNE_MODE_EFFICIENCY;
    g_autotune.enabled = false;

    // Best values start at 0 (will be populated during autotune)
    g_autotune.status.best_frequency = 0;
    g_autotune.status.best_voltage = 0;

    g_autotune.initialized = true;
    ESP_LOGI(TAG, "Autotune module initialized");

    return ESP_OK;
}

esp_err_t cluster_autotune_start(autotune_mode_t mode)
{
    if (!g_autotune.initialized) {
        cluster_autotune_init();
    }

    lock();

    if (g_autotune.task_running) {
        unlock();
        ESP_LOGW(TAG, "Autotune already running");
        return ESP_ERR_INVALID_STATE;
    }

    g_autotune.status.state = AUTOTUNE_STATE_STARTING;
    g_autotune.status.mode = mode;
    g_autotune.status.progress_percent = 0;
    g_autotune.status.tests_completed = 0;
    g_autotune.status.tests_total = 0;
    g_autotune.status.error_msg[0] = '\0';
    g_autotune.autotune_start_time = esp_timer_get_time() / 1000;

    // Get current settings as starting point
    g_autotune.status.current_frequency = cluster_get_asic_frequency();
    g_autotune.status.current_voltage = cluster_get_core_voltage();

    // Create autotune task
    BaseType_t ret = xTaskCreate(
        cluster_autotune_task,
        "autotune",
        AUTOTUNE_TASK_STACK_SIZE,
        NULL,
        AUTOTUNE_TASK_PRIORITY,
        &g_autotune.task_handle
    );

    if (ret != pdPASS) {
        g_autotune.status.state = AUTOTUNE_STATE_ERROR;
        strncpy(g_autotune.status.error_msg, "Failed to create task", sizeof(g_autotune.status.error_msg));
        unlock();
        return ESP_ERR_NO_MEM;
    }

    g_autotune.task_running = true;
    unlock();

    ESP_LOGI(TAG, "Autotune started in mode %d", mode);
    return ESP_OK;
}

esp_err_t cluster_autotune_stop(bool apply_best)
{
    lock();

    if (!g_autotune.task_running) {
        unlock();
        return ESP_OK;
    }

    // Signal task to stop
    g_autotune.task_running = false;

    // Wait for task to finish
    if (g_autotune.task_handle) {
        // Give task time to clean up
        unlock();
        vTaskDelay(pdMS_TO_TICKS(100));
        lock();
    }

    if (apply_best && g_autotune.status.best_frequency > 0 && g_autotune.status.best_voltage > 0) {
        ESP_LOGI(TAG, "Applying best settings: %d MHz, %d mV",
                 g_autotune.status.best_frequency, g_autotune.status.best_voltage);
        cluster_autotune_apply_settings(g_autotune.status.best_frequency,
                                         g_autotune.status.best_voltage);
    }

    g_autotune.status.state = AUTOTUNE_STATE_IDLE;
    unlock();

    ESP_LOGI(TAG, "Autotune stopped");
    return ESP_OK;
}

bool cluster_autotune_is_running(void)
{
    return g_autotune.task_running;
}

esp_err_t cluster_autotune_get_status(autotune_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }

    lock();
    memcpy(status, &g_autotune.status, sizeof(autotune_status_t));

    // Update duration
    if (g_autotune.task_running) {
        status->total_duration_ms = (esp_timer_get_time() / 1000) - g_autotune.autotune_start_time;
        status->test_duration_ms = (esp_timer_get_time() / 1000) - g_autotune.test_start_time;
    }

    unlock();
    return ESP_OK;
}

esp_err_t cluster_autotune_set_enabled(bool enable)
{
    g_autotune.enabled = enable;

    if (enable && !g_autotune.task_running) {
        return cluster_autotune_start(AUTOTUNE_MODE_EFFICIENCY);
    } else if (!enable && g_autotune.task_running) {
        return cluster_autotune_stop(true);
    }

    return ESP_OK;
}

bool cluster_autotune_is_enabled(void)
{
    return g_autotune.enabled;
}

esp_err_t cluster_autotune_apply_settings(uint16_t frequency_mhz, uint16_t voltage_mv)
{
    GlobalState *GLOBAL_STATE = cluster_get_global_state();

    if (!GLOBAL_STATE) {
        ESP_LOGE(TAG, "GLOBAL_STATE not available");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Applying settings: %d MHz, %d mV", frequency_mhz, voltage_mv);

    // Set voltage first (safer to have higher voltage when increasing frequency)
    float voltage_v = voltage_mv / 1000.0f;
    VCORE_set_voltage(GLOBAL_STATE, voltage_v);

    // Small delay for voltage to stabilize
    vTaskDelay(pdMS_TO_TICKS(100));

    // Set frequency
    ASIC_set_frequency(GLOBAL_STATE, (float)frequency_mhz);

    // Save to NVS
    nvs_config_set_u16(NVS_CONFIG_ASIC_FREQUENCY, frequency_mhz);
    nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, voltage_mv);

    // Update best values
    lock();
    g_autotune.status.current_frequency = frequency_mhz;
    g_autotune.status.current_voltage = voltage_mv;
    unlock();

    return ESP_OK;
}

// ============================================================================
// Autotune Task
// ============================================================================

void cluster_autotune_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "Autotune task started");

    g_autotune.global_state = cluster_get_global_state();

    // Starting frequency/voltage - begin with current settings
    uint16_t test_freq = g_autotune.status.current_frequency;
    uint16_t test_voltage = g_autotune.status.current_voltage;

    // Best found values
    float best_efficiency = 999999.0f;
    uint16_t best_freq = test_freq;
    uint16_t best_voltage = test_voltage;
    float best_hashrate = 0;

    // Calculate total tests (simplified grid search)
    int freq_steps = (FREQ_MAX_MHZ - FREQ_MIN_MHZ) / FREQ_STEP_MHZ + 1;
    int voltage_steps = (VOLTAGE_MAX_MV - VOLTAGE_MIN_MV) / VOLTAGE_STEP_MV + 1;
    g_autotune.status.tests_total = freq_steps * voltage_steps;

    lock();
    g_autotune.status.state = AUTOTUNE_STATE_STABILIZING;
    unlock();

    // Initial stabilization period
    ESP_LOGI(TAG, "Waiting for initial stabilization...");
    reset_measurements();

    for (int i = 0; i < AUTOTUNE_STABILIZE_TIME_MS / 1000 && g_autotune.task_running; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        collect_sample();
    }

    // Main autotune loop - test different frequency/voltage combinations
    for (test_freq = FREQ_MIN_MHZ; test_freq <= FREQ_MAX_MHZ && g_autotune.task_running; test_freq += FREQ_STEP_MHZ) {
        for (test_voltage = VOLTAGE_MIN_MV; test_voltage <= VOLTAGE_MAX_MV && g_autotune.task_running; test_voltage += VOLTAGE_STEP_MV) {

            lock();
            g_autotune.status.state = AUTOTUNE_STATE_ADJUSTING;
            g_autotune.status.current_frequency = test_freq;
            g_autotune.status.current_voltage = test_voltage;
            unlock();

            ESP_LOGI(TAG, "Testing: %d MHz, %d mV", test_freq, test_voltage);

            // Apply settings
            cluster_autotune_apply_settings(test_freq, test_voltage);

            // Wait for stabilization
            lock();
            g_autotune.status.state = AUTOTUNE_STATE_STABILIZING;
            unlock();

            vTaskDelay(pdMS_TO_TICKS(AUTOTUNE_STABILIZE_TIME_MS / 2));

            // Test phase - collect measurements
            lock();
            g_autotune.status.state = AUTOTUNE_STATE_TESTING;
            unlock();

            reset_measurements();

            for (int i = 0; i < AUTOTUNE_TEST_TIME_MS / 1000 && g_autotune.task_running; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                collect_sample();

                // Update progress
                lock();
                g_autotune.status.test_duration_ms = (esp_timer_get_time() / 1000) - g_autotune.test_start_time;
                unlock();
            }

            if (!g_autotune.task_running) break;

            // Calculate results
            float avg_hashrate, avg_power;
            get_average_measurements(&avg_hashrate, &avg_power);
            float efficiency = calculate_efficiency(avg_hashrate, avg_power);

            ESP_LOGI(TAG, "Results: %.2f GH/s, %.2f W, %.2f J/TH",
                     avg_hashrate, avg_power, efficiency);

            // Check if this is better (lower efficiency = better for J/TH)
            bool is_better = false;
            if (g_autotune.status.mode == AUTOTUNE_MODE_EFFICIENCY) {
                is_better = (efficiency < best_efficiency) && (avg_hashrate > 0);
            } else if (g_autotune.status.mode == AUTOTUNE_MODE_HASHRATE) {
                is_better = (avg_hashrate > best_hashrate);
            } else {
                // Balanced: weighted score
                float score = avg_hashrate / efficiency;
                float best_score = best_hashrate / best_efficiency;
                is_better = (score > best_score);
            }

            if (is_better) {
                best_efficiency = efficiency;
                best_freq = test_freq;
                best_voltage = test_voltage;
                best_hashrate = avg_hashrate;

                lock();
                g_autotune.status.best_frequency = best_freq;
                g_autotune.status.best_voltage = best_voltage;
                g_autotune.status.best_efficiency = best_efficiency;
                g_autotune.status.best_hashrate = best_hashrate;
                unlock();

                ESP_LOGI(TAG, "New best: %d MHz, %d mV, %.2f J/TH",
                         best_freq, best_voltage, best_efficiency);
            }

            // Update progress
            lock();
            g_autotune.status.tests_completed++;
            g_autotune.status.progress_percent =
                (g_autotune.status.tests_completed * 100) / g_autotune.status.tests_total;
            unlock();
        }
    }

    // Apply best settings
    if (g_autotune.task_running && best_freq > 0 && best_voltage > 0) {
        ESP_LOGI(TAG, "Autotune complete. Applying best: %d MHz, %d mV, %.2f J/TH",
                 best_freq, best_voltage, best_efficiency);

        cluster_autotune_apply_settings(best_freq, best_voltage);

        lock();
        g_autotune.status.state = AUTOTUNE_STATE_LOCKED;
        unlock();
    } else {
        lock();
        g_autotune.status.state = AUTOTUNE_STATE_IDLE;
        unlock();
    }

    g_autotune.task_running = false;
    g_autotune.task_handle = NULL;

    ESP_LOGI(TAG, "Autotune task finished");
    vTaskDelete(NULL);
}

// ============================================================================
// Master Remote Autotune (Placeholder - requires protocol extension)
// ============================================================================

#if CLUSTER_IS_MASTER

esp_err_t cluster_autotune_slave_enable(uint8_t slave_id, bool enable)
{
    // TODO: Send autotune enable command to slave via ESP-NOW
    ESP_LOGI(TAG, "Slave %d autotune %s (not yet implemented)", slave_id, enable ? "enable" : "disable");
    return ESP_OK;
}

esp_err_t cluster_autotune_all_slaves_enable(bool enable)
{
    // TODO: Send autotune enable command to all slaves
    ESP_LOGI(TAG, "All slaves autotune %s (not yet implemented)", enable ? "enable" : "disable");
    return ESP_OK;
}

esp_err_t cluster_autotune_slave_get_status(uint8_t slave_id, autotune_status_t *status)
{
    // TODO: Request status from slave via ESP-NOW
    if (status) {
        memset(status, 0, sizeof(autotune_status_t));
    }
    return ESP_OK;
}

#endif // CLUSTER_IS_MASTER
