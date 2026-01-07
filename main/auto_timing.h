/**
 * @file auto_timing.h
 * @brief Auto-Timing - Dynamic ASIC job interval adjustment
 *
 * This module monitors share rejection rate and automatically adjusts
 * the ASIC job interval to find the optimal timing for current network conditions.
 *
 * Features:
 *   - Startup calibration: Tests intervals 500-800ms to find optimal
 *   - Runtime monitoring: Adjusts based on rejection rate
 *   - Master→Slave sync: Broadcasts optimal timing to cluster slaves
 *
 * @author ClusterAxe Project
 * @license GPL-3.0
 */

#ifndef AUTO_TIMING_H
#define AUTO_TIMING_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "global_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration Constants
// ============================================================================

#define AUTO_TIMING_MIN_INTERVAL_MS     500     // Minimum allowed interval
#define AUTO_TIMING_MAX_INTERVAL_MS     800     // Maximum allowed interval
#define AUTO_TIMING_DEFAULT_INTERVAL_MS 700     // Default for BM1370

// Calibration settings
#define AUTO_TIMING_CALIBRATION_STEPS   7       // Number of intervals to test
#define AUTO_TIMING_CALIBRATION_TIME_MS 90000   // Time per interval test (90 seconds)
#define AUTO_TIMING_MIN_SHARES_FOR_TEST 20      // Minimum shares needed for valid test

// Monitoring settings
#define AUTO_TIMING_WINDOW_MS           300000  // 5-minute monitoring window
#define AUTO_TIMING_STABILIZE_MS        120000  // 2 minutes between adjustments

// Thresholds
#define AUTO_TIMING_REJECT_HIGH         5.0f    // Increase interval if > 5%
#define AUTO_TIMING_REJECT_LOW          1.0f    // Decrease interval if < 1%

// Adjustment steps
#define AUTO_TIMING_STEP_UP_MS          50      // Step up when rejections high
#define AUTO_TIMING_STEP_DOWN_MS        25      // Step down when rejections low

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize the auto-timing module
 * @param GLOBAL_STATE Global state pointer
 * @return ESP_OK on success
 */
esp_err_t auto_timing_init(GlobalState *GLOBAL_STATE);

/**
 * @brief Start the auto-timing task
 * @param GLOBAL_STATE Global state pointer
 * @return ESP_OK on success
 */
esp_err_t auto_timing_start(GlobalState *GLOBAL_STATE);

/**
 * @brief Stop the auto-timing task
 */
void auto_timing_stop(void);

/**
 * @brief Enable or disable auto-timing
 * @param enabled true to enable, false to disable
 */
void auto_timing_set_enabled(bool enabled);

/**
 * @brief Check if auto-timing is enabled
 * @return true if enabled
 */
bool auto_timing_is_enabled(void);

/**
 * @brief Get current auto-timing state
 * @return Current state (disabled, calibrating, monitoring, locked)
 */
auto_timing_state_t auto_timing_get_state(void);

/**
 * @brief Get current job interval in milliseconds
 * @return Current interval (500-800ms)
 */
uint16_t auto_timing_get_interval(void);

/**
 * @brief Manually set the job interval (disables auto-adjustment)
 * @param interval_ms Interval in milliseconds (500-800)
 * @return ESP_OK on success
 */
esp_err_t auto_timing_set_interval(uint16_t interval_ms);

/**
 * @brief Force start calibration phase
 */
void auto_timing_start_calibration(void);

/**
 * @brief Get current rejection rate from monitoring window
 * @return Rejection rate as percentage (0-100)
 */
float auto_timing_get_rejection_rate(void);

/**
 * @brief Get status as JSON string for API
 * @param buffer Output buffer
 * @param buffer_size Size of buffer
 */
void auto_timing_get_status_json(char *buffer, size_t buffer_size);

/**
 * @brief Notify auto-timing of a share acceptance
 * Called by system.c when a share is accepted
 */
void auto_timing_notify_share_accepted(void);

/**
 * @brief Notify auto-timing of a share rejection
 * Called by system.c when a share is rejected
 */
void auto_timing_notify_share_rejected(void);

#ifdef __cplusplus
}
#endif

#endif // AUTO_TIMING_H
