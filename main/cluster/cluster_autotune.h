/**
 * @file cluster_autotune.h
 * @brief ClusterAxe Auto-Tuning Module
 *
 * Provides automatic frequency and voltage optimization for maximum efficiency.
 * Supports both local (master) and remote (slave) auto-tuning.
 *
 * @author ClusterAxe Project
 * @license GPL-3.0
 */

#ifndef CLUSTER_AUTOTUNE_H
#define CLUSTER_AUTOTUNE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Autotune States
// ============================================================================

typedef enum {
    AUTOTUNE_STATE_IDLE = 0,      // Not running
    AUTOTUNE_STATE_STARTING,       // Initializing
    AUTOTUNE_STATE_TESTING,        // Testing current settings
    AUTOTUNE_STATE_ADJUSTING,      // Adjusting frequency/voltage
    AUTOTUNE_STATE_STABILIZING,    // Waiting for hashrate to stabilize
    AUTOTUNE_STATE_LOCKED,         // Found optimal settings
    AUTOTUNE_STATE_ERROR           // Error occurred
} autotune_state_t;

typedef enum {
    AUTOTUNE_MODE_EFFICIENCY = 0,  // Optimize for J/TH (default)
    AUTOTUNE_MODE_HASHRATE,        // Optimize for maximum hashrate
    AUTOTUNE_MODE_BALANCED         // Balance between efficiency and hashrate
} autotune_mode_t;

// ============================================================================
// Autotune Status Structure
// ============================================================================

typedef struct {
    autotune_state_t state;
    autotune_mode_t mode;

    // Current test values
    uint16_t current_frequency;     // MHz
    uint16_t current_voltage;       // mV

    // Best found values
    uint16_t best_frequency;        // MHz
    uint16_t best_voltage;          // mV
    float best_efficiency;          // J/TH
    float best_hashrate;            // GH/s

    // Progress tracking
    uint8_t progress_percent;       // 0-100
    uint32_t test_duration_ms;      // How long current test has run
    uint32_t total_duration_ms;     // Total autotune duration

    // Stats
    uint16_t tests_completed;
    uint16_t tests_total;

    // Error info
    char error_msg[64];
} autotune_status_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Initialize the autotune module
 * @return ESP_OK on success
 */
esp_err_t cluster_autotune_init(void);

/**
 * @brief Start auto-tuning on this device
 * @param mode Optimization mode (efficiency, hashrate, or balanced)
 * @return ESP_OK if started successfully
 */
esp_err_t cluster_autotune_start(autotune_mode_t mode);

/**
 * @brief Stop auto-tuning
 * @param apply_best If true, apply the best found settings
 * @return ESP_OK on success
 */
esp_err_t cluster_autotune_stop(bool apply_best);

/**
 * @brief Check if autotune is currently running
 * @return true if autotune is active
 */
bool cluster_autotune_is_running(void);

/**
 * @brief Get current autotune status
 * @param status Output structure for status
 * @return ESP_OK on success
 */
esp_err_t cluster_autotune_get_status(autotune_status_t *status);

/**
 * @brief Enable/disable autotune (persists to NVS)
 * @param enable true to enable, false to disable
 * @return ESP_OK on success
 */
esp_err_t cluster_autotune_set_enabled(bool enable);

/**
 * @brief Check if autotune is enabled in NVS
 * @return true if enabled
 */
bool cluster_autotune_is_enabled(void);

/**
 * @brief Apply specific frequency and voltage settings
 * @param frequency_mhz Frequency in MHz
 * @param voltage_mv Voltage in millivolts
 * @return ESP_OK on success
 */
esp_err_t cluster_autotune_apply_settings(uint16_t frequency_mhz, uint16_t voltage_mv);

// ============================================================================
// Master Remote Autotune (for controlling slaves)
// ============================================================================

#if CLUSTER_IS_MASTER

/**
 * @brief Enable autotune on a specific slave
 * @param slave_id Slave ID
 * @param enable true to enable
 * @return ESP_OK on success
 */
esp_err_t cluster_autotune_slave_enable(uint8_t slave_id, bool enable);

/**
 * @brief Enable autotune on all connected slaves
 * @param enable true to enable
 * @return ESP_OK on success
 */
esp_err_t cluster_autotune_all_slaves_enable(bool enable);

/**
 * @brief Get autotune status from a slave
 * @param slave_id Slave ID
 * @param status Output status structure
 * @return ESP_OK on success
 */
esp_err_t cluster_autotune_slave_get_status(uint8_t slave_id, autotune_status_t *status);

#endif // CLUSTER_IS_MASTER

// ============================================================================
// Internal Task
// ============================================================================

/**
 * @brief Main autotune task (runs in background)
 * @param pvParameters Task parameters (unused)
 */
void cluster_autotune_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // CLUSTER_AUTOTUNE_H
