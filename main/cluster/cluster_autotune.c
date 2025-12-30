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

#if CLUSTER_IS_MASTER
#include "esp_http_client.h"
#include "cluster.h"
#endif

static const char *TAG = "autotune";

// ============================================================================
// Configuration
// ============================================================================

#define AUTOTUNE_STABILIZE_TIME_MS    20000   // Wait 20s for hashrate to stabilize
#define AUTOTUNE_TEST_TIME_MS         45000   // Test each setting for 45s
#define AUTOTUNE_TASK_STACK_SIZE      4096
#define AUTOTUNE_TASK_PRIORITY        5

// Watchdog configuration
#define WATCHDOG_CHECK_INTERVAL_MS    5000    // Check every 5 seconds
#define WATCHDOG_TASK_STACK_SIZE      3072
#define WATCHDOG_TASK_PRIORITY        6       // Higher priority than autotune

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

    // Slave autotune tracking (master only)
    bool include_master;
    uint8_t slave_include_mask;  // Bitmask of slaves to include
    int8_t current_device;       // -1 = master, 0-7 = slave index

    // Watchdog state
    bool watchdog_enabled;
    bool watchdog_running;
    TaskHandle_t watchdog_task_handle;
    uint16_t watchdog_last_freq;     // Track for gradual reduction
    uint16_t watchdog_last_voltage;  // Track for gradual reduction
} g_autotune = {0};

#if CLUSTER_IS_MASTER
// Per-slave best results
typedef struct {
    uint16_t best_frequency;
    uint16_t best_voltage;
    float best_efficiency;
    float best_hashrate;
    bool valid;
} slave_autotune_result_t;

static slave_autotune_result_t g_slave_results[CONFIG_CLUSTER_MAX_SLAVES] = {0};

// HTTP response buffer for slave communication
static char *http_response_buffer = NULL;
static int http_response_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (http_response_buffer == NULL) {
                    http_response_buffer = malloc(evt->data_len + 1);
                    http_response_len = 0;
                } else {
                    http_response_buffer = realloc(http_response_buffer, http_response_len + evt->data_len + 1);
                }
                if (http_response_buffer) {
                    memcpy(http_response_buffer + http_response_len, evt->data, evt->data_len);
                    http_response_len += evt->data_len;
                    http_response_buffer[http_response_len] = '\0';
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief Apply frequency/voltage settings to a slave via HTTP PATCH
 */
static esp_err_t apply_settings_to_slave(const char *ip_addr, uint16_t freq_mhz, uint16_t voltage_mv)
{
    if (!ip_addr || strlen(ip_addr) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char url[64];
    snprintf(url, sizeof(url), "http://%s/api/system", ip_addr);

    char post_data[128];
    snprintf(post_data, sizeof(post_data),
             "{\"frequency\":%d,\"coreVoltage\":%d}", freq_mhz, voltage_mv);

    ESP_LOGI(TAG, "Applying to slave %s: %d MHz, %d mV", ip_addr, freq_mhz, voltage_mv);

    // Free previous response
    if (http_response_buffer) {
        free(http_response_buffer);
        http_response_buffer = NULL;
    }
    http_response_len = 0;

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PATCH,
        .timeout_ms = 5000,
        .event_handler = http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Slave response: status=%d", status);
        if (status >= 400) {
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP request to slave failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    if (http_response_buffer) {
        free(http_response_buffer);
        http_response_buffer = NULL;
    }

    return err;
}

/**
 * @brief Get slave stats from cluster status
 */
static bool get_slave_stats(int slave_id, float *hashrate, float *power, float *temp)
{
    cluster_slave_t slave_info;
    if (cluster_master_get_slave_info(slave_id, &slave_info) != ESP_OK) {
        return false;
    }

    if (slave_info.state != SLAVE_STATE_ACTIVE) {
        return false;
    }

    *hashrate = (float)slave_info.hashrate / 100.0f;  // Convert from GH/s * 100
    *power = slave_info.power;
    *temp = slave_info.temperature;

    return true;
}

/**
 * @brief Get slave IP address
 */
static const char* get_slave_ip(int slave_id)
{
    static cluster_slave_t slave_info;
    if (cluster_master_get_slave_info(slave_id, &slave_info) != ESP_OK) {
        return NULL;
    }
    if (strlen(slave_info.ip_addr) == 0) {
        return NULL;
    }
    return slave_info.ip_addr;
}
#endif // CLUSTER_IS_MASTER

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

#if CLUSTER_IS_MASTER
/**
 * @brief Autotune a single slave device via HTTP
 * @param slave_id Slave ID (0-7)
 * @param mode Autotune mode
 * @return ESP_OK on success
 */
static esp_err_t autotune_slave_device(int slave_id, autotune_mode_t mode)
{
    const char *ip_addr = get_slave_ip(slave_id);
    if (!ip_addr) {
        ESP_LOGW(TAG, "Slave %d has no IP address - skipping", slave_id);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Starting autotune for SLAVE %d (%s)", slave_id, ip_addr);
    ESP_LOGI(TAG, "========================================");

    uint16_t freq_max = get_max_freq_for_mode(mode);
    uint16_t voltage_max = get_max_voltage_for_mode(mode);

    // Best found values for this slave
    float best_efficiency = 999999.0f;
    uint16_t best_freq = FREQ_BASE_MHZ;
    uint16_t best_voltage = VOLTAGE_BASE_MV;
    float best_hashrate = 0;

    // Apply base settings to slave
    if (apply_settings_to_slave(ip_addr, FREQ_BASE_MHZ, VOLTAGE_BASE_MV) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply base settings to slave %d", slave_id);
        return ESP_FAIL;
    }

    // Initial stabilization
    ESP_LOGI(TAG, "Slave %d: Waiting for stabilization...", slave_id);
    vTaskDelay(pdMS_TO_TICKS(AUTOTUNE_STABILIZE_TIME_MS));

    // Test frequency/voltage combinations
    int test_num = 0;
    int total_tests = get_freq_step_count(mode) * get_voltage_step_count(mode);

    for (int fi = 0; fi < NUM_FREQ_STEPS && g_autotune.task_running; fi++) {
        uint16_t test_freq = FREQ_STEPS[fi];
        if (test_freq > freq_max) continue;

        for (int vi = 0; vi < NUM_VOLTAGE_STEPS && g_autotune.task_running; vi++) {
            uint16_t test_voltage = VOLTAGE_STEPS[vi];
            if (test_voltage > voltage_max) continue;

            test_num++;
            ESP_LOGI(TAG, "Slave %d: Testing %d MHz, %d mV (%d/%d)",
                     slave_id, test_freq, test_voltage, test_num, total_tests);

            // Apply settings to slave
            if (apply_settings_to_slave(ip_addr, test_freq, test_voltage) != ESP_OK) {
                ESP_LOGW(TAG, "Failed to apply settings to slave %d - skipping test", slave_id);
                continue;
            }

            // Stabilization delay
            vTaskDelay(pdMS_TO_TICKS(AUTOTUNE_STABILIZE_TIME_MS / 2));

            // Collect samples from cluster status
            float hashrate_sum = 0, power_sum = 0, temp_sum = 0;
            int sample_count = 0;
            bool temp_exceeded = false;

            for (int i = 0; i < AUTOTUNE_TEST_TIME_MS / 1000 && g_autotune.task_running; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));

                float h, p, t;
                if (get_slave_stats(slave_id, &h, &p, &t)) {
                    hashrate_sum += h;
                    power_sum += p;
                    temp_sum += t;
                    sample_count++;

                    if (t > TEMP_TARGET_C) {
                        ESP_LOGW(TAG, "Slave %d: Temp %.1f°C exceeded target", slave_id, t);
                        temp_exceeded = true;
                        break;
                    }
                }
            }

            if (!g_autotune.task_running) break;
            if (temp_exceeded || sample_count == 0) continue;

            // Calculate results
            float avg_hashrate = hashrate_sum / sample_count;
            float avg_power = power_sum / sample_count;
            float avg_temp = temp_sum / sample_count;
            float efficiency = calculate_efficiency(avg_hashrate, avg_power);

            ESP_LOGI(TAG, "Slave %d: %.2f GH/s, %.2f W, %.2f J/TH, %.1f°C",
                     slave_id, avg_hashrate, avg_power, efficiency, avg_temp);

            // Check if better based on mode
            bool is_better = false;
            if (mode == AUTOTUNE_MODE_EFFICIENCY) {
                is_better = (efficiency < best_efficiency) && (avg_hashrate > 0) && (avg_temp <= TEMP_TARGET_C);
            } else if (mode == AUTOTUNE_MODE_HASHRATE) {
                is_better = (avg_hashrate > best_hashrate) && (avg_temp <= TEMP_TARGET_C);
            } else {
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
                ESP_LOGI(TAG, "Slave %d: *** NEW BEST: %d MHz, %d mV ***", slave_id, best_freq, best_voltage);
            }
        }
    }

    // Apply best settings to slave
    if (g_autotune.task_running && best_freq > 0 && best_voltage > 0) {
        ESP_LOGI(TAG, "Slave %d: Applying best: %d MHz, %d mV (%.2f J/TH)",
                 slave_id, best_freq, best_voltage, best_efficiency);
        apply_settings_to_slave(ip_addr, best_freq, best_voltage);

        // Store results
        g_slave_results[slave_id].best_frequency = best_freq;
        g_slave_results[slave_id].best_voltage = best_voltage;
        g_slave_results[slave_id].best_efficiency = best_efficiency;
        g_slave_results[slave_id].best_hashrate = best_hashrate;
        g_slave_results[slave_id].valid = true;
    }

    return ESP_OK;
}
#endif // CLUSTER_IS_MASTER

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

    // Default: include master and all slaves
    g_autotune.include_master = true;
    g_autotune.slave_include_mask = 0xFF;  // All slaves
    g_autotune.current_device = -1;

#if CLUSTER_IS_MASTER
    // Clear slave results
    memset(g_slave_results, 0, sizeof(g_slave_results));
#endif

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

    // Apply best settings found for master
    if (g_autotune.task_running && g_autotune.include_master && best_freq > 0 && best_voltage > 0) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "MASTER AUTOTUNE COMPLETE!");
        ESP_LOGI(TAG, "Best settings: %d MHz, %d mV", best_freq, best_voltage);
        ESP_LOGI(TAG, "Performance: %.2f GH/s, %.2f J/TH @ %.1f°C",
                 best_hashrate, best_efficiency, best_temp);
        ESP_LOGI(TAG, "========================================");

        cluster_autotune_apply_settings(best_freq, best_voltage);
    }

#if CLUSTER_IS_MASTER
    // Now autotune slaves with IP addresses
    if (g_autotune.task_running && g_autotune.slave_include_mask != 0) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "Starting SLAVE autotune (mask: 0x%02X)", g_autotune.slave_include_mask);
        ESP_LOGI(TAG, "========================================");

        for (int i = 0; i < CONFIG_CLUSTER_MAX_SLAVES && g_autotune.task_running; i++) {
            // Check if this slave is included in the mask
            if (!(g_autotune.slave_include_mask & (1 << i))) {
                continue;
            }

            // Check if slave exists and has IP
            const char *ip = get_slave_ip(i);
            if (!ip) {
                ESP_LOGD(TAG, "Slave %d: No IP address, skipping", i);
                continue;
            }

            lock();
            g_autotune.current_device = i;
            unlock();

            // Autotune this slave
            autotune_slave_device(i, g_autotune.status.mode);
        }

        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "ALL SLAVE AUTOTUNE COMPLETE!");
        ESP_LOGI(TAG, "========================================");
    }
#endif // CLUSTER_IS_MASTER

    // Final state
    lock();
    if (g_autotune.task_running) {
        g_autotune.status.state = AUTOTUNE_STATE_LOCKED;
    } else {
        g_autotune.status.state = AUTOTUNE_STATE_IDLE;
    }
    g_autotune.current_device = -1;
    unlock();

    g_autotune.task_running = false;
    g_autotune.task_handle = NULL;

    ESP_LOGI(TAG, "Autotune task finished");
    vTaskDelete(NULL);
}

// ============================================================================
// Device Selection API
// ============================================================================

/**
 * @brief Set whether to include master in cluster autotune
 */
void cluster_autotune_set_include_master(bool include)
{
    g_autotune.include_master = include;
    ESP_LOGI(TAG, "Master %s in autotune", include ? "included" : "excluded");
}

/**
 * @brief Set which slaves to include in cluster autotune (bitmask)
 */
void cluster_autotune_set_slave_mask(uint8_t mask)
{
    g_autotune.slave_include_mask = mask;
    ESP_LOGI(TAG, "Slave autotune mask set to 0x%02X", mask);
}

/**
 * @brief Include/exclude a specific slave from autotune
 */
void cluster_autotune_set_slave_include(uint8_t slave_id, bool include)
{
    if (slave_id >= CONFIG_CLUSTER_MAX_SLAVES) return;

    if (include) {
        g_autotune.slave_include_mask |= (1 << slave_id);
    } else {
        g_autotune.slave_include_mask &= ~(1 << slave_id);
    }
    ESP_LOGI(TAG, "Slave %d %s in autotune (mask: 0x%02X)",
             slave_id, include ? "included" : "excluded", g_autotune.slave_include_mask);
}

/**
 * @brief Get current device being autotuned (-1 = master, 0-7 = slave)
 */
int8_t cluster_autotune_get_current_device(void)
{
    return g_autotune.current_device;
}

// ============================================================================
// Master Remote Autotune
// ============================================================================

#if CLUSTER_IS_MASTER

esp_err_t cluster_autotune_slave_enable(uint8_t slave_id, bool enable)
{
    if (slave_id >= CONFIG_CLUSTER_MAX_SLAVES) {
        return ESP_ERR_INVALID_ARG;
    }
    cluster_autotune_set_slave_include(slave_id, enable);
    return ESP_OK;
}

esp_err_t cluster_autotune_all_slaves_enable(bool enable)
{
    cluster_autotune_set_slave_mask(enable ? 0xFF : 0x00);
    return ESP_OK;
}

esp_err_t cluster_autotune_slave_get_status(uint8_t slave_id, autotune_status_t *status)
{
    if (slave_id >= CONFIG_CLUSTER_MAX_SLAVES || !status) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(autotune_status_t));

    // Return cached results if available
    if (g_slave_results[slave_id].valid) {
        status->state = AUTOTUNE_STATE_LOCKED;
        status->best_frequency = g_slave_results[slave_id].best_frequency;
        status->best_voltage = g_slave_results[slave_id].best_voltage;
        status->best_efficiency = g_slave_results[slave_id].best_efficiency;
        status->best_hashrate = g_slave_results[slave_id].best_hashrate;
    }

    return ESP_OK;
}

#endif // CLUSTER_IS_MASTER

// ============================================================================
// Safety Watchdog
// ============================================================================

/**
 * @brief Find next lower frequency step
 */
static uint16_t get_lower_freq_step(uint16_t current_freq)
{
    for (int i = NUM_FREQ_STEPS - 1; i >= 0; i--) {
        if (FREQ_STEPS[i] < current_freq) {
            return FREQ_STEPS[i];
        }
    }
    return FREQ_STEPS[0];  // Return minimum
}

/**
 * @brief Find next lower voltage step
 */
static uint16_t get_lower_voltage_step(uint16_t current_voltage)
{
    for (int i = NUM_VOLTAGE_STEPS - 1; i >= 0; i--) {
        if (VOLTAGE_STEPS[i] < current_voltage) {
            return VOLTAGE_STEPS[i];
        }
    }
    return VOLTAGE_STEPS[0];  // Return minimum
}

/**
 * @brief Watchdog task - monitors temp and voltage, takes protective action
 */
static void cluster_watchdog_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Safety watchdog started");

    while (g_autotune.watchdog_running) {
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_CHECK_INTERVAL_MS));

        if (!g_autotune.watchdog_enabled) {
            continue;
        }

        GlobalState *GLOBAL_STATE = cluster_get_global_state();
        if (!GLOBAL_STATE) continue;

        // Get current settings
        uint16_t current_freq = cluster_get_asic_frequency();
        uint16_t current_voltage = cluster_get_core_voltage();
        float current_temp = get_current_temp();
        float current_vin = get_input_voltage();

        bool need_action = false;
        uint16_t new_freq = current_freq;
        uint16_t new_voltage = current_voltage;

        // Check temperature - if over 65°C, drop voltage
        if (current_temp > TEMP_TARGET_C) {
            ESP_LOGW(TAG, "WATCHDOG: Temp %.1f°C > %d°C - reducing voltage",
                     current_temp, TEMP_TARGET_C);
            new_voltage = get_lower_voltage_step(current_voltage);
            need_action = true;
        }

        // Check input voltage - if below 4.9V, drop both freq and voltage
        if (current_vin < VIN_MIN_SAFE) {
            ESP_LOGW(TAG, "WATCHDOG: Vin %.2fV < %.2fV - reducing freq & voltage",
                     current_vin, VIN_MIN_SAFE);
            new_freq = get_lower_freq_step(current_freq);
            new_voltage = get_lower_voltage_step(current_voltage);
            need_action = true;
        }

        // Apply changes if needed
        if (need_action && (new_freq != current_freq || new_voltage != current_voltage)) {
            ESP_LOGW(TAG, "WATCHDOG: Applying protective settings: %d MHz, %d mV (was %d MHz, %d mV)",
                     new_freq, new_voltage, current_freq, current_voltage);

            // Apply voltage first (safer)
            VCORE_set_voltage(GLOBAL_STATE, new_voltage / 1000.0f);
            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, new_voltage);

            vTaskDelay(pdMS_TO_TICKS(100));

            // Then frequency
            if (new_freq != current_freq) {
                ASIC_set_frequency(GLOBAL_STATE, (float)new_freq);
                nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, (float)new_freq);
                GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value = (float)new_freq;
            }

            // Track what we set
            g_autotune.watchdog_last_freq = new_freq;
            g_autotune.watchdog_last_voltage = new_voltage;
        }

        // If Vin is recovering (>= 5.0V) and we previously reduced settings, log it
        if (current_vin >= VIN_OK_MIN && g_autotune.watchdog_last_freq > 0 &&
            (current_freq < g_autotune.watchdog_last_freq || current_voltage < g_autotune.watchdog_last_voltage)) {
            ESP_LOGI(TAG, "WATCHDOG: Vin recovered to %.2fV - settings stable at %d MHz, %d mV",
                     current_vin, current_freq, current_voltage);
        }

#if CLUSTER_IS_MASTER
        // Also check slaves
        for (int i = 0; i < CONFIG_CLUSTER_MAX_SLAVES; i++) {
            float slave_hashrate, slave_power, slave_temp;
            if (!get_slave_stats(i, &slave_hashrate, &slave_power, &slave_temp)) {
                continue;  // Slave not active
            }

            // Check slave temperature
            if (slave_temp > TEMP_TARGET_C) {
                const char *ip = get_slave_ip(i);
                if (ip) {
                    ESP_LOGW(TAG, "WATCHDOG: Slave %d temp %.1f°C > %d°C - reducing voltage",
                             i, slave_temp, TEMP_TARGET_C);

                    // Get slave's current voltage from cluster info and reduce it
                    cluster_slave_t slave_info;
                    if (cluster_master_get_slave_info(i, &slave_info) == ESP_OK) {
                        uint16_t slave_voltage = slave_info.core_voltage;
                        uint16_t new_slave_voltage = get_lower_voltage_step(slave_voltage);
                        if (new_slave_voltage != slave_voltage) {
                            apply_settings_to_slave(ip, slave_info.frequency, new_slave_voltage);
                        }
                    }
                }
            }
        }
#endif // CLUSTER_IS_MASTER
    }

    ESP_LOGI(TAG, "Safety watchdog stopped");
    g_autotune.watchdog_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Enable/disable the safety watchdog
 */
esp_err_t cluster_autotune_watchdog_enable(bool enable)
{
    if (!g_autotune.initialized) {
        cluster_autotune_init();
    }

    if (enable && !g_autotune.watchdog_running) {
        // Start watchdog task
        g_autotune.watchdog_enabled = true;
        g_autotune.watchdog_running = true;
        g_autotune.watchdog_last_freq = 0;
        g_autotune.watchdog_last_voltage = 0;

        BaseType_t ret = xTaskCreate(
            cluster_watchdog_task,
            "watchdog",
            WATCHDOG_TASK_STACK_SIZE,
            NULL,
            WATCHDOG_TASK_PRIORITY,
            &g_autotune.watchdog_task_handle
        );

        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create watchdog task");
            g_autotune.watchdog_running = false;
            return ESP_ERR_NO_MEM;
        }

        ESP_LOGI(TAG, "Safety watchdog enabled");
    }
    else if (!enable && g_autotune.watchdog_running) {
        // Stop watchdog task
        g_autotune.watchdog_enabled = false;
        g_autotune.watchdog_running = false;

        // Wait for task to exit
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_CHECK_INTERVAL_MS + 100));

        ESP_LOGI(TAG, "Safety watchdog disabled");
    }
    else {
        // Just update enabled flag
        g_autotune.watchdog_enabled = enable;
    }

    return ESP_OK;
}

/**
 * @brief Check if watchdog is enabled
 */
bool cluster_autotune_watchdog_is_enabled(void)
{
    return g_autotune.watchdog_enabled;
}

/**
 * @brief Check if watchdog task is running
 */
bool cluster_autotune_watchdog_is_running(void)
{
    return g_autotune.watchdog_running;
}
