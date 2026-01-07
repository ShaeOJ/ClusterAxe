/**
 * @file auto_timing.c
 * @brief Auto-Timing - Dynamic ASIC job interval adjustment
 */

#include "auto_timing.h"
#include "nvs_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#if CLUSTER_ENABLED && CLUSTER_IS_MASTER
#include "cluster.h"
#endif

static const char *TAG = "auto_timing";

// Calibration intervals to test (in ms)
static const uint16_t CALIBRATION_INTERVALS[AUTO_TIMING_CALIBRATION_STEPS] = {
    500, 550, 600, 650, 700, 750, 800
};

// Module state
static GlobalState *g_global_state = NULL;
static TaskHandle_t g_task_handle = NULL;
static bool g_task_running = false;

// ============================================================================
// Internal Helper Functions
// ============================================================================

static int64_t get_time_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void set_interval(uint16_t interval_ms)
{
    if (!g_global_state) return;

    AutoTimingModule *at = &g_global_state->AUTO_TIMING_MODULE;

    // Clamp to valid range
    if (interval_ms < at->min_interval_ms) interval_ms = at->min_interval_ms;
    if (interval_ms > at->max_interval_ms) interval_ms = at->max_interval_ms;

    if (at->current_interval_ms != interval_ms) {
        at->current_interval_ms = interval_ms;
        at->interval_changed = true;
        ESP_LOGI(TAG, "Set interval to %u ms", interval_ms);
    }
}

static float calculate_rejection_rate(uint32_t accepted, uint32_t rejected)
{
    uint32_t total = accepted + rejected;
    if (total == 0) return 0.0f;
    return (float)rejected / (float)total * 100.0f;
}

static void reset_window_stats(void)
{
    if (!g_global_state) return;

    AutoTimingModule *at = &g_global_state->AUTO_TIMING_MODULE;
    at->window_shares_accepted = 0;
    at->window_shares_rejected = 0;
    at->window_start_time = get_time_ms();
}

static void broadcast_timing_to_slaves(uint16_t interval_ms)
{
#if CLUSTER_ENABLED && CLUSTER_IS_MASTER
    // Send timing update to all slaves via cluster master
    cluster_master_broadcast_timing(interval_ms);
#else
    (void)interval_ms;  // Unused on non-master builds
#endif
}

// ============================================================================
// Calibration Phase
// ============================================================================

static void run_calibration_step(void)
{
    if (!g_global_state) return;

    AutoTimingModule *at = &g_global_state->AUTO_TIMING_MODULE;
    int64_t now = get_time_ms();
    int64_t step_duration = now - at->calibration_start_time;

    // Check if current step is complete
    if (step_duration >= AUTO_TIMING_CALIBRATION_TIME_MS) {
        // Calculate rejection rate for this interval
        uint32_t step_accepted = at->window_shares_accepted;
        uint32_t step_rejected = at->window_shares_rejected;
        float rejection_rate = calculate_rejection_rate(step_accepted, step_rejected);

        // Store result
        at->calibration_results[at->calibration_step] = rejection_rate;

        ESP_LOGI(TAG, "Calibration step %d: %u ms -> %.2f%% rejection (%lu accepted, %lu rejected)",
                 at->calibration_step,
                 at->calibration_intervals[at->calibration_step],
                 rejection_rate,
                 (unsigned long)step_accepted,
                 (unsigned long)step_rejected);

        // Track best result
        if (step_accepted + step_rejected >= AUTO_TIMING_MIN_SHARES_FOR_TEST) {
            if (rejection_rate < at->best_rejection_rate ||
                at->best_rejection_rate < 0.0f) {
                at->best_rejection_rate = rejection_rate;
                at->best_interval = at->calibration_intervals[at->calibration_step];
                ESP_LOGI(TAG, "New best: %u ms @ %.2f%% rejection",
                         at->best_interval, at->best_rejection_rate);
            }
        }

        // Move to next step
        at->calibration_step++;

        if (at->calibration_step >= AUTO_TIMING_CALIBRATION_STEPS) {
            // Calibration complete
            ESP_LOGI(TAG, "Calibration complete! Best interval: %u ms @ %.2f%% rejection",
                     at->best_interval, at->best_rejection_rate);

            // Apply best interval
            set_interval(at->best_interval);
            at->optimal_interval_ms = at->best_interval;

            // Save to NVS
            nvs_config_set_u16(NVS_CONFIG_JOB_INTERVAL_MS, at->optimal_interval_ms);

            // Broadcast to slaves
            broadcast_timing_to_slaves(at->optimal_interval_ms);

            // Transition to monitoring state
            at->state = AUTO_TIMING_MONITORING;
            reset_window_stats();
            at->last_adjustment_time = now;
        } else {
            // Start next calibration step
            set_interval(at->calibration_intervals[at->calibration_step]);
            broadcast_timing_to_slaves(at->calibration_intervals[at->calibration_step]);
            reset_window_stats();
            at->calibration_start_time = now;
        }
    }
}

static void start_calibration(void)
{
    if (!g_global_state) return;

    AutoTimingModule *at = &g_global_state->AUTO_TIMING_MODULE;

    ESP_LOGI(TAG, "Starting calibration phase...");

    at->state = AUTO_TIMING_CALIBRATING;
    at->calibration_step = 0;
    at->best_rejection_rate = 100.0f;  // Start with worst case
    at->best_interval = AUTO_TIMING_DEFAULT_INTERVAL_MS;

    // Initialize calibration intervals
    memcpy(at->calibration_intervals, CALIBRATION_INTERVALS, sizeof(CALIBRATION_INTERVALS));

    // Clear results
    for (int i = 0; i < AUTO_TIMING_CALIBRATION_STEPS; i++) {
        at->calibration_results[i] = -1.0f;
    }

    // Start first interval
    set_interval(at->calibration_intervals[0]);
    broadcast_timing_to_slaves(at->calibration_intervals[0]);
    reset_window_stats();
    at->calibration_start_time = get_time_ms();
}

// ============================================================================
// Monitoring Phase
// ============================================================================

static void check_and_adjust(void)
{
    if (!g_global_state) return;

    AutoTimingModule *at = &g_global_state->AUTO_TIMING_MODULE;
    int64_t now = get_time_ms();

    // Calculate window duration
    int64_t window_duration = now - at->window_start_time;

    // Don't adjust too frequently
    int64_t since_last_adjustment = now - at->last_adjustment_time;
    if (since_last_adjustment < AUTO_TIMING_STABILIZE_MS) {
        return;
    }

    // Check window duration
    if (window_duration < AUTO_TIMING_WINDOW_MS) {
        return;
    }

    // Calculate rejection rate
    uint32_t total = at->window_shares_accepted + at->window_shares_rejected;
    if (total < AUTO_TIMING_MIN_SHARES_FOR_TEST) {
        // Not enough shares, extend window
        return;
    }

    float rejection_rate = calculate_rejection_rate(
        at->window_shares_accepted, at->window_shares_rejected);
    at->current_rejection_rate = rejection_rate;

    ESP_LOGI(TAG, "Window stats: %.2f%% rejection (%lu/%lu), interval=%u ms",
             rejection_rate,
             (unsigned long)at->window_shares_rejected,
             (unsigned long)total,
             at->current_interval_ms);

    bool adjusted = false;

    // Check if adjustment needed
    if (rejection_rate > AUTO_TIMING_REJECT_HIGH &&
        at->current_interval_ms < at->max_interval_ms) {
        // High rejections - increase interval
        uint16_t new_interval = at->current_interval_ms + AUTO_TIMING_STEP_UP_MS;
        if (new_interval > at->max_interval_ms) new_interval = at->max_interval_ms;

        ESP_LOGW(TAG, "High rejection rate (%.2f%%), increasing interval: %u -> %u ms",
                 rejection_rate, at->current_interval_ms, new_interval);

        set_interval(new_interval);
        broadcast_timing_to_slaves(new_interval);
        adjusted = true;
    }
    else if (rejection_rate < AUTO_TIMING_REJECT_LOW &&
             at->current_interval_ms > at->min_interval_ms) {
        // Low rejections - try decreasing (optimize)
        uint16_t new_interval = at->current_interval_ms - AUTO_TIMING_STEP_DOWN_MS;
        if (new_interval < at->min_interval_ms) new_interval = at->min_interval_ms;

        ESP_LOGI(TAG, "Low rejection rate (%.2f%%), optimizing interval: %u -> %u ms",
                 rejection_rate, at->current_interval_ms, new_interval);

        set_interval(new_interval);
        broadcast_timing_to_slaves(new_interval);
        adjusted = true;
    }

    // Track best settings
    if (rejection_rate < at->best_rejection_rate) {
        at->best_rejection_rate = rejection_rate;
        at->best_interval = at->current_interval_ms;
        at->optimal_interval_ms = at->current_interval_ms;

        // Save optimal to NVS
        nvs_config_set_u16(NVS_CONFIG_JOB_INTERVAL_MS, at->optimal_interval_ms);
        ESP_LOGI(TAG, "New optimal: %u ms @ %.2f%% rejection",
                 at->optimal_interval_ms, at->best_rejection_rate);
    }

    // Reset window
    reset_window_stats();
    if (adjusted) {
        at->last_adjustment_time = now;
    }
}

// ============================================================================
// Main Task
// ============================================================================

static void auto_timing_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    g_global_state = GLOBAL_STATE;
    AutoTimingModule *at = &GLOBAL_STATE->AUTO_TIMING_MODULE;

    ESP_LOGI(TAG, "Auto-timing task started");

    // Check if we have a saved optimal interval
    uint16_t saved_interval = nvs_config_get_u16(NVS_CONFIG_JOB_INTERVAL_MS);
    bool has_saved_optimal = (saved_interval >= AUTO_TIMING_MIN_INTERVAL_MS &&
                              saved_interval <= AUTO_TIMING_MAX_INTERVAL_MS);

    if (has_saved_optimal && at->enabled) {
        // Use saved optimal, skip calibration
        ESP_LOGI(TAG, "Loaded saved optimal interval: %u ms", saved_interval);
        at->current_interval_ms = saved_interval;
        at->optimal_interval_ms = saved_interval;
        at->interval_changed = true;
        at->state = AUTO_TIMING_MONITORING;
        reset_window_stats();
        at->last_adjustment_time = get_time_ms();

        // Broadcast to slaves
        broadcast_timing_to_slaves(saved_interval);
    }

    while (g_task_running) {
        if (!at->enabled) {
            at->state = AUTO_TIMING_DISABLED;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        switch (at->state) {
            case AUTO_TIMING_DISABLED:
                // Just enabled, start calibration
                start_calibration();
                break;

            case AUTO_TIMING_CALIBRATING:
                run_calibration_step();
                break;

            case AUTO_TIMING_MONITORING:
                check_and_adjust();
                break;

            case AUTO_TIMING_LOCKED:
                // Locked - don't adjust, but monitor for severe degradation
                // If rejection rate exceeds 10%, trigger recalibration
                if (at->current_rejection_rate > 10.0f) {
                    ESP_LOGW(TAG, "Rejection rate degraded, triggering recalibration");
                    start_calibration();
                }
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Auto-timing task stopped");
    g_task_handle = NULL;
    vTaskDelete(NULL);
}

// ============================================================================
// Public API Implementation
// ============================================================================

esp_err_t auto_timing_init(GlobalState *GLOBAL_STATE)
{
    if (!GLOBAL_STATE) return ESP_ERR_INVALID_ARG;

    g_global_state = GLOBAL_STATE;
    AutoTimingModule *at = &GLOBAL_STATE->AUTO_TIMING_MODULE;

    // Initialize from NVS
    at->enabled = nvs_config_get_bool(NVS_CONFIG_AUTO_TIMING_ENABLED);
    at->min_interval_ms = nvs_config_get_u16(NVS_CONFIG_AUTO_TIMING_MIN);
    at->max_interval_ms = nvs_config_get_u16(NVS_CONFIG_AUTO_TIMING_MAX);

    // Set defaults if NVS values are invalid
    if (at->min_interval_ms < 400 || at->min_interval_ms > 800) {
        at->min_interval_ms = AUTO_TIMING_MIN_INTERVAL_MS;
    }
    if (at->max_interval_ms < 500 || at->max_interval_ms > 1000) {
        at->max_interval_ms = AUTO_TIMING_MAX_INTERVAL_MS;
    }

    // Load saved interval
    at->optimal_interval_ms = nvs_config_get_u16(NVS_CONFIG_JOB_INTERVAL_MS);
    if (at->optimal_interval_ms < at->min_interval_ms ||
        at->optimal_interval_ms > at->max_interval_ms) {
        at->optimal_interval_ms = AUTO_TIMING_DEFAULT_INTERVAL_MS;
    }
    at->current_interval_ms = at->optimal_interval_ms;

    // Initialize state
    at->state = AUTO_TIMING_DISABLED;
    at->interval_changed = false;
    at->window_shares_accepted = 0;
    at->window_shares_rejected = 0;
    at->window_start_time = 0;
    at->last_adjustment_time = 0;
    at->calibration_step = 0;
    at->best_rejection_rate = 100.0f;
    at->best_interval = at->optimal_interval_ms;
    at->current_rejection_rate = 0.0f;

    ESP_LOGI(TAG, "Initialized: enabled=%d, interval=%u ms, range=[%u-%u]",
             at->enabled, at->current_interval_ms,
             at->min_interval_ms, at->max_interval_ms);

    return ESP_OK;
}

esp_err_t auto_timing_start(GlobalState *GLOBAL_STATE)
{
    if (g_task_running) {
        return ESP_OK;  // Already running
    }

    g_global_state = GLOBAL_STATE;
    g_task_running = true;

    BaseType_t result = xTaskCreate(
        auto_timing_task,
        "auto_timing",
        4096,
        GLOBAL_STATE,
        5,  // Priority
        &g_task_handle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create auto-timing task");
        g_task_running = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

void auto_timing_stop(void)
{
    g_task_running = false;
    // Task will clean up on next iteration
}

void auto_timing_set_enabled(bool enabled)
{
    if (!g_global_state) return;

    g_global_state->AUTO_TIMING_MODULE.enabled = enabled;
    nvs_config_set_bool(NVS_CONFIG_AUTO_TIMING_ENABLED, enabled);

    ESP_LOGI(TAG, "Auto-timing %s", enabled ? "enabled" : "disabled");

    if (!enabled) {
        g_global_state->AUTO_TIMING_MODULE.state = AUTO_TIMING_DISABLED;
    }
}

bool auto_timing_is_enabled(void)
{
    return g_global_state ? g_global_state->AUTO_TIMING_MODULE.enabled : false;
}

auto_timing_state_t auto_timing_get_state(void)
{
    return g_global_state ? g_global_state->AUTO_TIMING_MODULE.state : AUTO_TIMING_DISABLED;
}

uint16_t auto_timing_get_interval(void)
{
    return g_global_state ? g_global_state->AUTO_TIMING_MODULE.current_interval_ms : 700;
}

esp_err_t auto_timing_set_interval(uint16_t interval_ms)
{
    if (!g_global_state) return ESP_ERR_INVALID_STATE;

    AutoTimingModule *at = &g_global_state->AUTO_TIMING_MODULE;

    if (interval_ms < at->min_interval_ms || interval_ms > at->max_interval_ms) {
        return ESP_ERR_INVALID_ARG;
    }

    set_interval(interval_ms);

    // Save to NVS
    nvs_config_set_u16(NVS_CONFIG_JOB_INTERVAL_MS, interval_ms);

    // Broadcast to slaves
    broadcast_timing_to_slaves(interval_ms);

    // Lock to manual setting
    at->state = AUTO_TIMING_LOCKED;
    at->optimal_interval_ms = interval_ms;

    ESP_LOGI(TAG, "Manual interval set: %u ms (locked)", interval_ms);

    return ESP_OK;
}

void auto_timing_start_calibration(void)
{
    if (!g_global_state) return;

    if (g_global_state->AUTO_TIMING_MODULE.enabled) {
        start_calibration();
    }
}

float auto_timing_get_rejection_rate(void)
{
    if (!g_global_state) return 0.0f;

    AutoTimingModule *at = &g_global_state->AUTO_TIMING_MODULE;
    return calculate_rejection_rate(at->window_shares_accepted, at->window_shares_rejected);
}

void auto_timing_get_status_json(char *buffer, size_t buffer_size)
{
    if (!g_global_state || !buffer || buffer_size == 0) {
        if (buffer && buffer_size > 0) buffer[0] = '\0';
        return;
    }

    AutoTimingModule *at = &g_global_state->AUTO_TIMING_MODULE;

    const char *state_str;
    switch (at->state) {
        case AUTO_TIMING_DISABLED:    state_str = "disabled"; break;
        case AUTO_TIMING_CALIBRATING: state_str = "calibrating"; break;
        case AUTO_TIMING_MONITORING:  state_str = "monitoring"; break;
        case AUTO_TIMING_LOCKED:      state_str = "locked"; break;
        default:                      state_str = "unknown"; break;
    }

    snprintf(buffer, buffer_size,
        "{"
        "\"enabled\":%s,"
        "\"state\":\"%s\","
        "\"stateCode\":%d,"
        "\"currentInterval\":%u,"
        "\"optimalInterval\":%u,"
        "\"minInterval\":%u,"
        "\"maxInterval\":%u,"
        "\"windowAccepted\":%lu,"
        "\"windowRejected\":%lu,"
        "\"rejectionRate\":%.2f,"
        "\"bestInterval\":%u,"
        "\"bestRejectionRate\":%.2f,"
        "\"calibrationStep\":%u,"
        "\"calibrationTotal\":%d"
        "}",
        at->enabled ? "true" : "false",
        state_str,
        (int)at->state,
        at->current_interval_ms,
        at->optimal_interval_ms,
        at->min_interval_ms,
        at->max_interval_ms,
        (unsigned long)at->window_shares_accepted,
        (unsigned long)at->window_shares_rejected,
        at->current_rejection_rate,
        at->best_interval,
        at->best_rejection_rate,
        at->calibration_step,
        AUTO_TIMING_CALIBRATION_STEPS
    );
}

void auto_timing_notify_share_accepted(void)
{
    if (!g_global_state) return;
    g_global_state->AUTO_TIMING_MODULE.window_shares_accepted++;
}

void auto_timing_notify_share_rejected(void)
{
    if (!g_global_state) return;
    g_global_state->AUTO_TIMING_MODULE.window_shares_rejected++;
}
