/**
 * @file cluster_watchdog.h
 * @brief Cluster Safety Watchdog - Temperature and Voltage Protection
 *
 * Monitors temperature and input voltage on master and all slaves.
 * Takes protective action (reduces frequency/voltage) when thresholds exceeded.
 *
 * Thresholds:
 *   - Temperature >= 68°C: Throttle device
 *   - Input Voltage <= 4.9V: Throttle device
 *
 * @author ClusterAxe Project
 * @license GPL-3.0
 */

#ifndef CLUSTER_WATCHDOG_H
#define CLUSTER_WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cluster_config.h"
#include "global_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration
// ============================================================================

#define WATCHDOG_CHECK_INTERVAL_MS      5000    // Check every 5 seconds
#define WATCHDOG_TEMP_THRESHOLD         68.0f   // Throttle if temp >= 68°C
#define WATCHDOG_VIN_THRESHOLD          4.75f   // Throttle if Vin <= 4.75V (loosened from 4.9V to tolerate ADC variance)
#define WATCHDOG_FREQ_STEP              50      // Reduce frequency by 50 MHz per step
#define WATCHDOG_VOLTAGE_STEP           50      // Reduce voltage by 50 mV per step
#define WATCHDOG_MIN_FREQUENCY          400     // Minimum frequency (MHz)
#define WATCHDOG_MIN_VOLTAGE            1100    // Minimum voltage (mV)

// Recovery thresholds (with hysteresis to prevent oscillation)
#define WATCHDOG_TEMP_RECOVERY_THRESHOLD    62.0f   // Recover when temp <= 62°C
#define WATCHDOG_VIN_RECOVERY_THRESHOLD     4.9f    // Recover when Vin >= 4.9V
#define WATCHDOG_RECOVERY_STABILITY_MS      30000   // Must be safe for 30s before recovery
#define WATCHDOG_RECOVERY_INTERVAL_MS       10000   // Time between recovery steps

// ============================================================================
// Types
// ============================================================================

/**
 * @brief Throttle reason flags
 */
typedef enum {
    THROTTLE_NONE           = 0,
    THROTTLE_TEMP_HIGH      = (1 << 0),
    THROTTLE_VIN_LOW        = (1 << 1),
} watchdog_throttle_reason_t;

/**
 * @brief Per-device watchdog status
 */
typedef struct {
    bool        is_throttled;           // Currently throttled
    bool        is_recovering;          // Currently in recovery process
    bool        was_recovered;          // Completed at least one recovery cycle (used to detect re-throttle cycling)
    uint8_t     throttle_reason;        // Bitmask of throttle reasons
    float       last_temp;              // Last measured temperature
    float       last_vin;               // Last measured input voltage
    uint16_t    original_frequency;     // Frequency at time of first throttle
    uint16_t    original_voltage;       // Voltage at time of first throttle
    uint16_t    recovery_frequency;     // Target frequency for recovery (adapts down on re-throttle)
    uint16_t    recovery_voltage;       // Target voltage for recovery (adapts down on re-throttle)
    uint16_t    current_frequency;      // Current frequency (watchdog-tracked, not overwritten from telemetry while throttled)
    uint16_t    current_voltage;        // Current voltage (watchdog-tracked, not overwritten from telemetry while throttled)
    uint32_t    throttle_count;         // Number of times throttled
    int64_t     last_throttle_time;     // When last throttled (ms since boot)
    int64_t     safe_since;             // When conditions became safe (0 if unsafe)
    int64_t     last_recovery_time;     // When last recovery step occurred
} watchdog_device_status_t;

/**
 * @brief Overall watchdog status
 */
typedef struct {
    bool        enabled;                            // Watchdog enabled
    bool        running;                            // Task is running
    watchdog_device_status_t master;                // Master device status
    watchdog_device_status_t slaves[CONFIG_CLUSTER_MAX_SLAVES];  // Slave statuses
    uint8_t     throttled_count;                    // Number of currently throttled devices
} watchdog_status_t;

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize the watchdog module
 * @param gs GlobalState pointer (used for sensor reads in standalone mode)
 * @return ESP_OK on success
 */
esp_err_t cluster_watchdog_init(GlobalState *gs);

/**
 * @brief Enable or disable the watchdog
 * @param enable true to enable, false to disable
 * @return ESP_OK on success
 */
esp_err_t cluster_watchdog_enable(bool enable);

/**
 * @brief Check if watchdog is enabled
 * @return true if enabled
 */
bool cluster_watchdog_is_enabled(void);

/**
 * @brief Check if watchdog task is running
 * @return true if running
 */
bool cluster_watchdog_is_running(void);

/**
 * @brief Get current watchdog status
 * @param status Output: watchdog status
 */
void cluster_watchdog_get_status(watchdog_status_t *status);

/**
 * @brief Get number of currently throttled devices
 * @return Number of throttled devices
 */
uint8_t cluster_watchdog_get_throttled_count(void);

/**
 * @brief Get the board-appropriate Vin throttle threshold (95% of nominal voltage)
 * @return Threshold in volts
 */
float cluster_watchdog_get_vin_throttle_threshold(void);

/**
 * @brief Get the board-appropriate Vin recovery threshold (98% of nominal voltage)
 * @return Threshold in volts
 */
float cluster_watchdog_get_vin_recovery_threshold(void);

#ifdef __cplusplus
}
#endif

#endif // CLUSTER_WATCHDOG_H
