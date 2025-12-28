/**
 * @file cluster_integration.h
 * @brief Clusteraxe Integration with ESP-Miner
 *
 * This file provides the integration layer between the cluster module
 * and the ESP-Miner stratum/ASIC subsystems.
 *
 * @author Clusteraxe Project
 * @license GPL-3.0
 */

#ifndef CLUSTER_INTEGRATION_H
#define CLUSTER_INTEGRATION_H

#include "cluster.h"
#include "cluster_config.h"
#include "global_state.h"
#include "stratum_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CLUSTER_ENABLED

// ============================================================================
// Master Integration Functions
// ============================================================================

#if CLUSTER_IS_MASTER

/**
 * @brief Convert mining_notify to cluster_work_t and distribute to slaves
 *
 * Called from stratum_task.c when new mining.notify is received
 *
 * @param GLOBAL_STATE Global state pointer
 * @param notification Mining notification from stratum
 * @param extranonce_str Pool extranonce string
 * @param extranonce_2_len Length of extranonce2
 */
void cluster_master_on_mining_notify(GlobalState *GLOBAL_STATE,
                                      const mining_notify *notification,
                                      const char *extranonce_str,
                                      int extranonce_2_len);

/**
 * @brief Submit a share received from a slave to the pool
 *
 * This is called by the cluster share submitter task
 *
 * @param job_id Job ID
 * @param nonce Found nonce
 * @param extranonce2 Extranonce2 value
 * @param en2_len Length of extranonce2
 * @param ntime Block timestamp
 * @param version Block version
 * @param slave_id Which slave sent this share (for counter updates)
 */
void stratum_submit_share_from_cluster(uint32_t job_id, uint32_t nonce,
                                        uint8_t *extranonce2, uint8_t en2_len,
                                        uint32_t ntime, uint32_t version,
                                        uint8_t slave_id);

/**
 * @brief Notify cluster module of share result from pool
 *
 * Called by stratum_task.c when pool responds to a share submission.
 * This updates the slave's shares_accepted/shares_rejected counter.
 *
 * @param message_id The stratum message ID of the share submission
 * @param accepted true if pool accepted, false if rejected
 */
void cluster_notify_share_result(int message_id, bool accepted);

/**
 * @brief Get the master's nonce range (slot 0)
 *
 * @param nonce_start Output: start of master's nonce range
 * @param nonce_end Output: end of master's nonce range
 */
void cluster_master_get_local_nonce_range(uint32_t *nonce_start, uint32_t *nonce_end);

#endif // CLUSTER_IS_MASTER

// ============================================================================
// Slave Integration Functions
// ============================================================================

#if CLUSTER_IS_SLAVE

/**
 * @brief Convert cluster_work_t to bm_job and submit to ASIC queue
 *
 * Called when slave receives work from master
 *
 * @param GLOBAL_STATE Global state pointer
 * @param work Cluster work from master
 */
void cluster_slave_submit_to_asic(GlobalState *GLOBAL_STATE,
                                   const cluster_work_t *work);

/**
 * @brief Called when ASIC finds a valid share in slave mode
 *
 * Intercepts share submission to route to master instead of pool
 *
 * @param GLOBAL_STATE Global state pointer
 * @param job_id Job ID
 * @param nonce Found nonce
 * @param ntime Block timestamp
 * @param version Block version
 * @param extranonce2 Extranonce2 string
 */
void cluster_slave_intercept_share(GlobalState *GLOBAL_STATE,
                                    uint8_t job_id,
                                    uint32_t nonce,
                                    uint32_t ntime,
                                    uint32_t version,
                                    const char *extranonce2);

/**
 * @brief Check if slave should process work from stratum
 *
 * In slave mode, work comes from master, not stratum
 *
 * @return true if slave mode is active and should skip stratum work
 */
bool cluster_slave_should_skip_stratum(void);

#endif // CLUSTER_IS_SLAVE

// ============================================================================
// Common Integration Functions
// ============================================================================

/**
 * @brief Initialize cluster integration with ESP-Miner
 *
 * Called during system startup
 *
 * @param GLOBAL_STATE Global state pointer
 * @return ESP_OK on success
 */
esp_err_t cluster_integration_init(GlobalState *GLOBAL_STATE);

/**
 * @brief Get ASIC hashrate for cluster reporting
 * @return Hashrate in GH/s * 100
 */
uint32_t cluster_get_asic_hashrate(void);

/**
 * @brief Get chip temperature for cluster reporting
 * @return Temperature in Celsius
 */
float cluster_get_chip_temp(void);

/**
 * @brief Get fan RPM for cluster reporting
 * @return Fan speed in RPM
 */
uint16_t cluster_get_fan_rpm(void);

/**
 * @brief Get device hostname for cluster registration
 * @return Hostname string (static buffer)
 */
const char* cluster_get_hostname(void);

/**
 * @brief Get device IP address for cluster registration
 * @return IP address string (e.g., "192.168.1.100")
 */
const char* cluster_get_ip_addr(void);

/**
 * @brief Get ASIC frequency for cluster reporting
 * @return Frequency in MHz
 */
uint16_t cluster_get_asic_frequency(void);

/**
 * @brief Get core voltage for cluster reporting
 * @return Voltage in mV
 */
uint16_t cluster_get_core_voltage(void);

/**
 * @brief Get power consumption for cluster reporting
 * @return Power in Watts
 */
float cluster_get_power(void);

/**
 * @brief Get input voltage for cluster reporting
 * @return Input voltage in Volts
 */
float cluster_get_voltage_in(void);

/**
 * @brief Submit work to ASIC (for slave mode)
 */
void cluster_submit_work_to_asic(const cluster_work_t *work);

/**
 * @brief Handle WiFi reconnection event
 *
 * Call this when WiFi successfully reconnects to update ESP-NOW channel
 * and reset registration state so slaves can re-register with master.
 *
 * This should be called from the WiFi event handler when IP is obtained.
 */
void cluster_on_wifi_reconnect(void);

/**
 * @brief Get the stored GlobalState pointer
 * @return GlobalState pointer, or NULL if not initialized
 */
GlobalState* cluster_get_global_state(void);

#endif // CLUSTER_ENABLED

#ifdef __cplusplus
}
#endif

#endif // CLUSTER_INTEGRATION_H
