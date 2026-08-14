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
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "auto_timing";

// ============================================================================
// Module State
// ============================================================================

static GlobalState *s_global_state = NULL;
static TaskHandle_t s_task_handle = NULL;
static SemaphoreHandle_t s_mutex = NULL;

// Calibration interval ladder
static const uint16_t s_cal_intervals[AUTO_TIMING_CALIBRATION_STEPS] = {
    500, 550, 600, 650, 700, 750, 800
};

// ============================================================================
// Internal Helpers
// ============================================================================

static AutoTimingModule *mod(void)
{
    return &s_global_state->AUTO_TIMING_MODULE;
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000LL;
}

static void apply_interval(uint16_t interval_ms)
{
    AutoTimingModule *m = mod();
    m->current_interval_ms = interval_ms;
    m->interval_changed = true;
    nvs_config_set_u16(NVS_CONFIG_JOB_INTERVAL_MS, interval_ms);
}

static void reset_window(void)
{
    AutoTimingModule *m = mod();
    m->window_shares_accepted = 0;
    m->window_shares_rejected = 0;
    m->window_start_time = now_ms();
}

static float window_rejection_rate(void)
{
    AutoTimingModule *m = mod();
    uint32_t total = m->window_shares_accepted + m->window_shares_rejected;
    if (total == 0) return 0.0f;
    return (float)m->window_shares_rejected / (float)total * 100.0f;
}

// ============================================================================
// Calibration Phase
// ============================================================================

static void calibration_begin_step(uint8_t step)
{
    AutoTimingModule *m = mod();
    m->calibration_step = step;
    m->calibration_start_time = now_ms();
    m->calibration_shares_start = m->window_shares_accepted + m->window_shares_rejected;
    apply_interval(s_cal_intervals[step]);
    ESP_LOGI(TAG, "Calibration step %d/%d: interval=%dms",
             step + 1, AUTO_TIMING_CALIBRATION_STEPS, s_cal_intervals[step]);
}

static bool calibration_step_ready(void)
{
    AutoTimingModule *m = mod();
    int64_t elapsed = now_ms() - m->calibration_start_time;
    uint32_t shares = (m->window_shares_accepted + m->window_shares_rejected)
                      - m->calibration_shares_start;
    return (elapsed >= AUTO_TIMING_CALIBRATION_TIME_MS)
           && (shares >= AUTO_TIMING_MIN_SHARES_FOR_TEST);
}

static void calibration_record_step(void)
{
    AutoTimingModule *m = mod();
    uint8_t step = m->calibration_step;
    float rate = window_rejection_rate();
    m->calibration_intervals[step] = s_cal_intervals[step];
    m->calibration_results[step] = rate;
    ESP_LOGI(TAG, "Step %d result: %.2f%% rejection at %dms",
             step + 1, rate, s_cal_intervals[step]);

    if (step == 0 || rate < m->best_rejection_rate) {
        m->best_rejection_rate = rate;
        m->best_interval = s_cal_intervals[step];
    }
}

static void calibration_finish(void)
{
    AutoTimingModule *m = mod();
    m->optimal_interval_ms = m->best_interval;
    apply_interval(m->best_interval);
    m->state = AUTO_TIMING_MONITORING;
    reset_window();
    m->last_adjustment_time = now_ms();
    // Mark calibration done so the next reboot resumes monitoring instead of recalibrating
    nvs_config_set_bool(NVS_CONFIG_AUTO_TIMING_CALIBRATED, true);
    ESP_LOGI(TAG, "Calibration done: best=%dms (%.2f%% rejection)",
             m->best_interval, m->best_rejection_rate);
}

// ============================================================================
// Monitoring Phase
// ============================================================================

static void monitoring_tick(void)
{
    AutoTimingModule *m = mod();
    int64_t n = now_ms();

    // Roll window if expired
    if ((n - m->window_start_time) >= AUTO_TIMING_WINDOW_MS) {
        m->current_rejection_rate = window_rejection_rate();
        reset_window();
    } else {
        m->current_rejection_rate = window_rejection_rate();
    }

    // Don't adjust too frequently
    if ((n - m->last_adjustment_time) < AUTO_TIMING_STABILIZE_MS) return;

    uint32_t total = m->window_shares_accepted + m->window_shares_rejected;
    if (total < AUTO_TIMING_MIN_SHARES_FOR_TEST) return;

    float rate = m->current_rejection_rate;

    if (rate > AUTO_TIMING_REJECT_HIGH) {
        uint16_t new_interval = m->current_interval_ms + AUTO_TIMING_STEP_UP_MS;
        if (new_interval > m->max_interval_ms) new_interval = m->max_interval_ms;
        if (new_interval != m->current_interval_ms) {
            ESP_LOGI(TAG, "High rejection %.2f%% — interval %d->%dms",
                     rate, m->current_interval_ms, new_interval);
            apply_interval(new_interval);
            m->last_adjustment_time = n;
            reset_window();
        }
    } else if (rate < AUTO_TIMING_REJECT_LOW && m->current_interval_ms > m->min_interval_ms) {
        uint16_t new_interval = m->current_interval_ms - AUTO_TIMING_STEP_DOWN_MS;
        if (new_interval < m->min_interval_ms) new_interval = m->min_interval_ms;
        if (new_interval != m->current_interval_ms) {
            ESP_LOGI(TAG, "Low rejection %.2f%% — interval %d->%dms",
                     rate, m->current_interval_ms, new_interval);
            apply_interval(new_interval);
            m->last_adjustment_time = n;
            reset_window();
        }
    }
}

// ============================================================================
// Task
// ============================================================================

static void auto_timing_task(void *arg)
{
    ESP_LOGI(TAG, "Task started");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) continue;

        AutoTimingModule *m = mod();

        if (!m->enabled) {
            m->state = AUTO_TIMING_DISABLED;
            xSemaphoreGive(s_mutex);
            continue;
        }

        switch (m->state) {
            case AUTO_TIMING_DISABLED:
                // Enabled flag just turned on
                m->state = AUTO_TIMING_CALIBRATING;
                m->best_rejection_rate = 100.0f;
                m->best_interval = AUTO_TIMING_DEFAULT_INTERVAL_MS;
                reset_window();
                calibration_begin_step(0);
                break;

            case AUTO_TIMING_CALIBRATING:
                if (calibration_step_ready()) {
                    calibration_record_step();
                    uint8_t next = m->calibration_step + 1;
                    if (next < AUTO_TIMING_CALIBRATION_STEPS) {
                        calibration_begin_step(next);
                    } else {
                        calibration_finish();
                    }
                }
                break;

            case AUTO_TIMING_MONITORING:
                monitoring_tick();
                break;

            case AUTO_TIMING_LOCKED:
                break;
        }

        xSemaphoreGive(s_mutex);
    }
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t auto_timing_init(GlobalState *GLOBAL_STATE)
{
    s_global_state = GLOBAL_STATE;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }

    AutoTimingModule *m = mod();
    memset(m, 0, sizeof(AutoTimingModule));

    m->enabled             = nvs_config_get_bool(NVS_CONFIG_AUTO_TIMING_ENABLED);
    m->current_interval_ms = nvs_config_get_u16(NVS_CONFIG_JOB_INTERVAL_MS);
    m->min_interval_ms     = nvs_config_get_u16(NVS_CONFIG_AUTO_TIMING_MIN);
    m->max_interval_ms     = nvs_config_get_u16(NVS_CONFIG_AUTO_TIMING_MAX);
    m->optimal_interval_ms = m->current_interval_ms;
    m->best_interval       = m->current_interval_ms;
    m->best_rejection_rate = 100.0f;
    m->window_start_time   = now_ms();
    m->last_adjustment_time = now_ms();

    bool calibrated = nvs_config_get_bool(NVS_CONFIG_AUTO_TIMING_CALIBRATED);
    if (!m->enabled) {
        m->state = AUTO_TIMING_DISABLED;
    } else if (calibrated
               && m->current_interval_ms >= AUTO_TIMING_MIN_INTERVAL_MS
               && m->current_interval_ms <= AUTO_TIMING_MAX_INTERVAL_MS) {
        // Previous calibration found an optimal interval — resume monitoring immediately.
        // User can click Recalibrate in the UI to force a fresh calibration sweep.
        m->state = AUTO_TIMING_MONITORING;
    } else {
        m->state = AUTO_TIMING_CALIBRATING;
    }

    ESP_LOGI(TAG, "Init: enabled=%d calibrated=%d interval=%dms state=%d",
             m->enabled, calibrated, m->current_interval_ms, m->state);

    return ESP_OK;
}

esp_err_t auto_timing_start(GlobalState *GLOBAL_STATE)
{
    if (!s_global_state) s_global_state = GLOBAL_STATE;

    BaseType_t ret = xTaskCreate(auto_timing_task, "auto_timing",
                                 4096, NULL, 3, &s_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        return ESP_FAIL;
    }

    AutoTimingModule *m = mod();
    if (m->enabled) {
        reset_window();
        if (m->state == AUTO_TIMING_CALIBRATING) {
            m->best_rejection_rate = 100.0f;
            calibration_begin_step(0);
        } else if (m->state == AUTO_TIMING_MONITORING) {
            ESP_LOGI(TAG, "Resuming monitoring at %dms (skipping calibration)", m->current_interval_ms);
        }
    }

    return ESP_OK;
}

void auto_timing_stop(void)
{
    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }
}

void auto_timing_set_enabled(bool enabled)
{
    if (!s_global_state) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;

    AutoTimingModule *m = mod();
    bool was_enabled = m->enabled;
    m->enabled = enabled;
    nvs_config_set_bool(NVS_CONFIG_AUTO_TIMING_ENABLED, enabled);

    if (enabled && !was_enabled) {
        m->state = AUTO_TIMING_CALIBRATING;
        m->best_rejection_rate = 100.0f;
        m->best_interval = m->current_interval_ms;
        reset_window();
        calibration_begin_step(0);
        ESP_LOGI(TAG, "Enabled — starting calibration");
    } else if (!enabled) {
        m->state = AUTO_TIMING_DISABLED;
        ESP_LOGI(TAG, "Disabled");
    }

    xSemaphoreGive(s_mutex);
}

bool auto_timing_is_enabled(void)
{
    if (!s_global_state) return false;
    return mod()->enabled;
}

auto_timing_state_t auto_timing_get_state(void)
{
    if (!s_global_state) return AUTO_TIMING_DISABLED;
    return mod()->state;
}

uint16_t auto_timing_get_interval(void)
{
    if (!s_global_state) return AUTO_TIMING_DEFAULT_INTERVAL_MS;
    return mod()->current_interval_ms;
}

esp_err_t auto_timing_set_interval(uint16_t interval_ms)
{
    if (!s_global_state) return ESP_FAIL;
    if (interval_ms < AUTO_TIMING_MIN_INTERVAL_MS || interval_ms > AUTO_TIMING_MAX_INTERVAL_MS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return ESP_FAIL;

    AutoTimingModule *m = mod();
    apply_interval(interval_ms);
    m->optimal_interval_ms = interval_ms;
    m->state = AUTO_TIMING_LOCKED;
    m->enabled = false;
    nvs_config_set_bool(NVS_CONFIG_AUTO_TIMING_ENABLED, false);

    ESP_LOGI(TAG, "Manual interval set to %dms (locked)", interval_ms);
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

void auto_timing_start_calibration(void)
{
    if (!s_global_state) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;

    AutoTimingModule *m = mod();
    m->enabled = true;
    m->state = AUTO_TIMING_CALIBRATING;
    m->best_rejection_rate = 100.0f;
    m->best_interval = m->current_interval_ms;
    reset_window();
    calibration_begin_step(0);
    nvs_config_set_bool(NVS_CONFIG_AUTO_TIMING_ENABLED, true);

    ESP_LOGI(TAG, "Calibration manually started");
    xSemaphoreGive(s_mutex);
}

float auto_timing_get_rejection_rate(void)
{
    if (!s_global_state) return 0.0f;
    return mod()->current_rejection_rate;
}

void auto_timing_get_status_json(char *buffer, size_t buffer_size)
{
    if (!s_global_state || !buffer) return;

    AutoTimingModule *m = mod();
    const char *state_str = "disabled";
    switch (m->state) {
        case AUTO_TIMING_CALIBRATING: state_str = "calibrating"; break;
        case AUTO_TIMING_MONITORING:  state_str = "monitoring";  break;
        case AUTO_TIMING_LOCKED:      state_str = "locked";      break;
        default: break;
    }

    snprintf(buffer, buffer_size,
        "{\"enabled\":%s,\"state\":\"%s\",\"currentInterval\":%d,"
        "\"optimalInterval\":%d,\"rejectionRate\":%.2f,"
        "\"windowAccepted\":%lu,\"windowRejected\":%lu,"
        "\"calibrationStep\":%d,\"bestInterval\":%d}",
        m->enabled ? "true" : "false",
        state_str,
        (int)m->current_interval_ms,
        (int)m->optimal_interval_ms,
        m->current_rejection_rate,
        (unsigned long)m->window_shares_accepted,
        (unsigned long)m->window_shares_rejected,
        (int)m->calibration_step,
        (int)m->best_interval
    );
}

void auto_timing_notify_share_accepted(void)
{
    if (!s_global_state || !s_mutex) return;
    if (xSemaphoreTake(s_mutex, 0) != pdTRUE) return;
    mod()->window_shares_accepted++;
    xSemaphoreGive(s_mutex);
}

void auto_timing_notify_share_rejected(void)
{
    if (!s_global_state || !s_mutex) return;
    if (xSemaphoreTake(s_mutex, 0) != pdTRUE) return;
    mod()->window_shares_rejected++;
    xSemaphoreGive(s_mutex);
}
