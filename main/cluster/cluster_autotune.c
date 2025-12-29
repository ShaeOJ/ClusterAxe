/**
 * @file cluster_autotune.c
 * @brief ClusterAxe Auto-Tuning Implementation
 *
 * Implements automatic frequency and voltage optimization for maximum efficiency.
 * Uses specific step values for precise tuning with temperature and input voltage protection.
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
#include "device_config.h"
#include <string.h>
#include <math.h>

static const char *TAG = "autotune";

// ============================================================================
// Configuration
// ============================================================================

#define AUTOTUNE_STABILIZE_TIME_MS    20000   // Wait 20s for hashrate to stabilize
#define AUTOTUNE_TEST_TIME_MS         45000   // Test each setting for 45s
#define AUTOTUNE_TASK_STACK_SIZE      4096
#define AUTOTUNE_TASK_PRIORITY        5

// Temperature limits
#define TEMP_TARGET_C         65       // Target max temperature - reject settings above this
#define TEMP_CHECK_INTERVAL   5        // Check temp every N seconds during test

// Input voltage protection
#define VIN_MIN_SAFE          4.9f     // Minimum safe input voltage
#define VIN_OK_MIN            5.0f     // Input voltage OK range start
#define VIN_OK_MAX            5.4f     // Input voltage OK range end
#define VOLTAGE_SAFE_MV       1100     // Drop to this voltage if Vin too low

// Base starting point
#define FREQ_BASE_MHZ         450
#define VOLTAGE_BASE_MV       1100

// Frequency steps to test (specific values, not linear)
static const uint16_t FREQ_STEPS[] = {450, 500, 525, 550, 600, 625, 650, 700, 725, 750, 800};
#define NUM_FREQ_STEPS (sizeof(FREQ_STEPS) / sizeof(FREQ_STEPS[0]))

// Voltage steps to test (specific values)
static const uint16_t VOLTAGE_STEPS[] = {1100, 1150, 1200, 1225, 1250, 1275, 1300};
#define NUM_VOLTAGE_STEPS (sizeof(VOLTAGE_STEPS) / sizeof(VOLTAGE_STEPS[0]))

// Mode limits
#define FREQ_MAX_MHZ_EFFICIENCY   625
#define FREQ_MAX_MHZ_BALANCED     700
#define FREQ_MAX_MHZ_HASHRATE     800

#define VOLTAGE_MAX_MV_EFFICIENCY 1175
#define VOLTAGE_MAX_MV_BALANCED   1200
#define VOLTAGE_MAX_MV_HASHRATE   1300

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
    float temp_sum;
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

static float get_current_temp(void)
{
    return cluster_get_chip_temp();
}

static float get_input_voltage(void)
{
    return cluster_get_voltage_in();
}

static uint16_t get_max_freq_for_mode(autotune_mode_t mode)
{
    switch (mode) {
        case AUTOTUNE_MODE_EFFICIENCY:
            return FREQ_MAX_MHZ_EFFICIENCY;
        case AUTOTUNE_MODE_BALANCED:
            return FREQ_MAX_MHZ_BALANCED;
        case AUTOTUNE_MODE_HASHRATE:
            return FREQ_MAX_MHZ_HASHRATE;
        default:
            return FREQ_MAX_MHZ_EFFICIENCY;
    }
}

static uint16_t get_max_voltage_for_mode(autotune_mode_t mode)
{
    switch (mode) {
        case AUTOTUNE_MODE_EFFICIENCY:
            return VOLTAGE_MAX_MV_EFFICIENCY;
        case AUTOTUNE_MODE_BALANCED:
            return VOLTAGE_MAX_MV_BALANCED;
        case AUTOTUNE_MODE_HASHRATE:
            return VOLTAGE_MAX_MV_HASHRATE;
        default:
            return VOLTAGE_MAX_MV_EFFICIENCY;
    }
}

/**
 * @brief Get count of frequency steps for a given mode
 */
static int get_freq_step_count(autotune_mode_t mode)
{
    uint16_t max_freq = get_max_freq_for_mode(mode);
    int count = 0;
    for (int i = 0; i < NUM_FREQ_STEPS; i++) {
        if (FREQ_STEPS[i] <= max_freq) {
            count++;
        }
    }
    return count;
}

/**
 * @brief Get count of voltage steps for a given mode
 */
static int get_voltage_step_count(autotune_mode_t mode)
{
    uint16_t max_voltage = get_max_voltage_for_mode(mode);
    int count = 0;
    for (int i = 0; i < NUM_VOLTAGE_STEPS; i++) {
        if (VOLTAGE_STEPS[i] <= max_voltage) {
            count++;
        }
    }
    return count;
}

static float calculate_efficiency(float hashrate_gh, float power_w)
{
    if (hashrate_gh <= 0 || power_w <= 0) {
        return 999999.0f;  // Invalid - return very high (bad) efficiency
    }
    // J/TH = (Watts * 1000) / (GH/s)
    return (power_w * 1000.0f) / hashrate_gh;
}

static void reset_measurements(void)
{
    g_autotune.hashrate_sum = 0;
    g_autotune.power_sum = 0;
    g_autotune.temp_sum = 0;
    g_autotune.sample_count = 0;
    g_autotune.test_start_time = esp_timer_get_time() / 1000;
}

static void collect_sample(void)
{
    g_autotune.hashrate_sum += get_current_hashrate();
    g_autotune.power_sum += get_current_power();
    g_autotune.temp_sum += get_current_temp();
    g_autotune.sample_count++;
}

static void get_average_measurements(float *avg_hashrate, float *avg_power, float *avg_temp)
{
    if (g_autotune.sample_count > 0) {
        *avg_hashrate = g_autotune.hashrate_sum / g_autotune.sample_count;
        *avg_power = g_autotune.power_sum / g_autotune.sample_count;
        *avg_temp = g_autotune.temp_sum / g_autotune.sample_count;
    } else {
        *avg_hashrate = 0;
        *avg_power = 0;
        *avg_temp = 0;
    }
}

/**
 * @brief Check input voltage and apply protection if needed
 * @return true if voltage is OK, false if protection was applied
 */
static bool check_input_voltage_protection(void)
{
    float vin = get_input_voltage();

    if (vin < VIN_MIN_SAFE) {
        ESP_LOGW(TAG, "INPUT VOLTAGE LOW: %.2fV < %.2fV - Dropping core voltage to %d mV for protection!",
                 vin, VIN_MIN_SAFE, VOLTAGE_SAFE_MV);

        // Apply safe voltage immediately
        GlobalState *GLOBAL_STATE = cluster_get_global_state();
        if (GLOBAL_STATE) {
            VCORE_set_voltage(GLOBAL_STATE, VOLTAGE_SAFE_MV / 1000.0f);
            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, VOLTAGE_SAFE_MV);

            // Update status
            lock();
            g_autotune.status.current_voltage = VOLTAGE_SAFE_MV;
            strncpy(g_autotune.status.error_msg, "Low input voltage - reduced core voltage",
                    sizeof(g_autotune.status.error_msg));
            unlock();
        }
        return false;
    }

    if (vin < VIN_OK_MIN || vin > VIN_OK_MAX) {
        ESP_LOGW(TAG, "Input voltage %.2fV outside optimal range (%.1f-%.1fV)",
                 vin, VIN_OK_MIN, VIN_OK_MAX);
    }

    return true;
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
    g_autotune.status.error_msg[0] = '\0';
    g_autotune.autotune_start_time = esp_timer_get_time() / 1000;

    // Calculate total tests based on mode limits
    int freq_count = get_freq_step_count(mode);
    int voltage_count = get_voltage_step_count(mode);
    g_autotune.status.tests_total = freq_count * voltage_count;

    // Get current settings
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

    ESP_LOGI(TAG, "Autotune started in mode %d (%d freq x %d voltage = %d tests)",
             mode, freq_count, voltage_count, g_autotune.status.tests_total);
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

    // Check input voltage before applying - protect against undervoltage
    float vin = get_input_voltage();
    if (vin < VIN_MIN_SAFE) {
        ESP_LOGW(TAG, "Input voltage %.2fV too low - limiting core voltage to %d mV",
                 vin, VOLTAGE_SAFE_MV);
        voltage_mv = VOLTAGE_SAFE_MV;
    }

    // Set voltage first (safer to have higher voltage when increasing frequency)
    float voltage_v = voltage_mv / 1000.0f;
    VCORE_set_voltage(GLOBAL_STATE, voltage_v);

    // Small delay for voltage to stabilize
    vTaskDelay(pdMS_TO_TICKS(100));

    // Set frequency on ASIC hardware
    ASIC_set_frequency(GLOBAL_STATE, (float)frequency_mhz);

    // Save to NVS - NOTE: frequency must be saved as float, not u16!
    nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, (float)frequency_mhz);
    nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, voltage_mv);

    // Update POWER_MANAGEMENT_MODULE directly so UI reflects changes immediately
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value = (float)frequency_mhz;
    // Also update expected hashrate calculation
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate =
        (float)frequency_mhz * GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count *
        GLOBAL_STATE->DEVICE_CONFIG.family.asic_count / 1000.0f;

    // Update autotune status
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

    // Get mode limits
    uint16_t freq_max = get_max_freq_for_mode(g_autotune.status.mode);
    uint16_t voltage_max = get_max_voltage_for_mode(g_autotune.status.mode);

    ESP_LOGI(TAG, "Mode %d: max %d MHz, %d mV | Temp target: %d°C",
             g_autotune.status.mode, freq_max, voltage_max, TEMP_TARGET_C);

    // Best found values - start with base settings
    float best_efficiency = 999999.0f;
    uint16_t best_freq = FREQ_BASE_MHZ;
    uint16_t best_voltage = VOLTAGE_BASE_MV;
    float best_hashrate = 0;
    float best_temp = 0;

    // First, apply base settings and stabilize
    lock();
    g_autotune.status.state = AUTOTUNE_STATE_STABILIZING;
    unlock();

    ESP_LOGI(TAG, "Applying base settings: %d MHz, %d mV", FREQ_BASE_MHZ, VOLTAGE_BASE_MV);
    cluster_autotune_apply_settings(FREQ_BASE_MHZ, VOLTAGE_BASE_MV);

    // Initial stabilization period
    ESP_LOGI(TAG, "Waiting for initial stabilization (%d seconds)...", AUTOTUNE_STABILIZE_TIME_MS / 1000);
    reset_measurements();

    for (int i = 0; i < AUTOTUNE_STABILIZE_TIME_MS / 1000 && g_autotune.task_running; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        collect_sample();

        // Check input voltage periodically during stabilization
        if (i % TEMP_CHECK_INTERVAL == 0) {
            if (!check_input_voltage_protection()) {
                ESP_LOGW(TAG, "Input voltage protection triggered during stabilization");
            }
        }
    }

    if (!g_autotune.task_running) {
        ESP_LOGI(TAG, "Autotune stopped during stabilization");
        vTaskDelete(NULL);
        return;
    }

    // Main autotune loop - test frequency/voltage combinations
    // Iterate through frequency steps
    for (int fi = 0; fi < NUM_FREQ_STEPS && g_autotune.task_running; fi++) {
        uint16_t test_freq = FREQ_STEPS[fi];

        // Skip frequencies above mode limit
        if (test_freq > freq_max) {
            continue;
        }

        // Iterate through voltage steps
        for (int vi = 0; vi < NUM_VOLTAGE_STEPS && g_autotune.task_running; vi++) {
            uint16_t test_voltage = VOLTAGE_STEPS[vi];

            // Skip voltages above mode limit
            if (test_voltage > voltage_max) {
                continue;
            }

            // Check input voltage before each test
            if (!check_input_voltage_protection()) {
                ESP_LOGW(TAG, "Skipping test due to low input voltage");
                lock();
                g_autotune.status.tests_completed++;
                g_autotune.status.progress_percent =
                    (g_autotune.status.tests_completed * 100) / g_autotune.status.tests_total;
                unlock();
                continue;
            }

            lock();
            g_autotune.status.state = AUTOTUNE_STATE_ADJUSTING;
            g_autotune.status.current_frequency = test_freq;
            g_autotune.status.current_voltage = test_voltage;
            unlock();

            ESP_LOGI(TAG, "Testing: %d MHz, %d mV (test %d/%d)",
                     test_freq, test_voltage,
                     g_autotune.status.tests_completed + 1,
                     g_autotune.status.tests_total);

            // Apply settings
            cluster_autotune_apply_settings(test_freq, test_voltage);

            // Wait for stabilization
            lock();
            g_autotune.status.state = AUTOTUNE_STATE_STABILIZING;
            unlock();

            vTaskDelay(pdMS_TO_TICKS(AUTOTUNE_STABILIZE_TIME_MS / 2));

            // Check temperature after stabilization - skip if already too hot
            float temp = get_current_temp();
            if (temp > TEMP_TARGET_C) {
                ESP_LOGW(TAG, "Temperature %.1f°C exceeds target %d°C after stabilization - skipping",
                         temp, TEMP_TARGET_C);
                lock();
                g_autotune.status.tests_completed++;
                g_autotune.status.progress_percent =
                    (g_autotune.status.tests_completed * 100) / g_autotune.status.tests_total;
                unlock();
                continue;
            }

            // Test phase - collect measurements with temperature monitoring
            lock();
            g_autotune.status.state = AUTOTUNE_STATE_TESTING;
            unlock();

            reset_measurements();
            bool temp_exceeded = false;
            float max_temp_seen = 0;

            for (int i = 0; i < AUTOTUNE_TEST_TIME_MS / 1000 && g_autotune.task_running; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                collect_sample();

                // Check temperature every TEMP_CHECK_INTERVAL seconds
                if (i % TEMP_CHECK_INTERVAL == 0) {
                    temp = get_current_temp();
                    if (temp > max_temp_seen) max_temp_seen = temp;

                    // Check if temperature exceeds target
                    if (temp > TEMP_TARGET_C) {
                        ESP_LOGW(TAG, "Temperature %.1f°C exceeded target %d°C during test",
                                 temp, TEMP_TARGET_C);
                        temp_exceeded = true;
                        break;
                    }

                    // Also check input voltage during test
                    if (!check_input_voltage_protection()) {
                        ESP_LOGW(TAG, "Input voltage protection triggered during test");
                        temp_exceeded = true;  // Use this flag to skip result
                        break;
                    }
                }

                // Update progress
                lock();
                g_autotune.status.test_duration_ms = (esp_timer_get_time() / 1000) - g_autotune.test_start_time;
                unlock();
            }

            if (!g_autotune.task_running) break;

            // Skip this result if temperature exceeded or voltage protection triggered
            if (temp_exceeded) {
                ESP_LOGW(TAG, "Skipping result (max temp: %.1f°C)", max_temp_seen);
                lock();
                g_autotune.status.tests_completed++;
                g_autotune.status.progress_percent =
                    (g_autotune.status.tests_completed * 100) / g_autotune.status.tests_total;
                unlock();
                continue;
            }

            // Calculate results
            float avg_hashrate, avg_power, avg_temp;
            get_average_measurements(&avg_hashrate, &avg_power, &avg_temp);
            float efficiency = calculate_efficiency(avg_hashrate, avg_power);

            ESP_LOGI(TAG, "Results: %.2f GH/s, %.2f W, %.2f J/TH, avg temp %.1f°C (max %.1f°C)",
                     avg_hashrate, avg_power, efficiency, avg_temp, max_temp_seen);

            // Check if this is better based on mode
            bool is_better = false;

            if (g_autotune.status.mode == AUTOTUNE_MODE_EFFICIENCY) {
                // Best efficiency (lowest J/TH) while temp <= 65°C
                is_better = (efficiency < best_efficiency) && (avg_hashrate > 0) && (avg_temp <= TEMP_TARGET_C);
            }
            else if (g_autotune.status.mode == AUTOTUNE_MODE_HASHRATE) {
                // Highest hashrate while temp <= 65°C
                // For hashrate mode, we push closer to temp limit
                is_better = (avg_hashrate > best_hashrate) && (avg_temp <= TEMP_TARGET_C);
            }
            else {
                // Balanced: best hashrate/efficiency score while temp <= 65°C
                float score = (best_efficiency > 0) ? (avg_hashrate / efficiency) : 0;
                float best_score = (best_efficiency > 0 && best_efficiency < 999999.0f) ?
                                   (best_hashrate / best_efficiency) : 0;
                is_better = (score > best_score) && (avg_temp <= TEMP_TARGET_C);
            }

            if (is_better) {
                best_efficiency = efficiency;
                best_freq = test_freq;
                best_voltage = test_voltage;
                best_hashrate = avg_hashrate;
                best_temp = avg_temp;

                lock();
                g_autotune.status.best_frequency = best_freq;
                g_autotune.status.best_voltage = best_voltage;
                g_autotune.status.best_efficiency = best_efficiency;
                g_autotune.status.best_hashrate = best_hashrate;
                unlock();

                ESP_LOGI(TAG, "*** NEW BEST: %d MHz, %d mV, %.2f J/TH, %.2f GH/s @ %.1f°C ***",
                         best_freq, best_voltage, best_efficiency, best_hashrate, best_temp);
            }

            // Update progress
            lock();
            g_autotune.status.tests_completed++;
            g_autotune.status.progress_percent =
                (g_autotune.status.tests_completed * 100) / g_autotune.status.tests_total;
            unlock();
        }
    }

    // Apply best settings found
    if (g_autotune.task_running && best_freq > 0 && best_voltage > 0) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "AUTOTUNE COMPLETE!");
        ESP_LOGI(TAG, "Best settings: %d MHz, %d mV", best_freq, best_voltage);
        ESP_LOGI(TAG, "Performance: %.2f GH/s, %.2f J/TH @ %.1f°C",
                 best_hashrate, best_efficiency, best_temp);
        ESP_LOGI(TAG, "========================================");

        cluster_autotune_apply_settings(best_freq, best_voltage);

        lock();
        g_autotune.status.state = AUTOTUNE_STATE_LOCKED;
        unlock();
    } else {
        ESP_LOGW(TAG, "Autotune did not find valid settings");
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
