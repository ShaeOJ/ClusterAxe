/**
 * @file cluster.h
 * @brief Bitaxe Cluster - Master/Slave coordination over BAP
 *
 * This module enables multiple Bitaxe units to operate as a coordinated
 * cluster, sharing a single stratum connection and distributing work
 * efficiently to avoid duplicate hashing.
 *
 * Architecture:
 *   - MASTER: Maintains stratum connection, distributes work to slaves
 *   - SLAVE: Receives work via BAP, reports shares back to master
 *
 * @author Clusteraxe Project
 * @license GPL-3.0
 */

#ifndef CLUSTER_H
#define CLUSTER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "cluster_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration for protocol types (defined in cluster_protocol.h)
typedef struct cluster_heartbeat_data cluster_heartbeat_data_t;

// ============================================================================
// Configuration Constants (using config values)
// ============================================================================

#define CLUSTER_MAX_SLAVES          CONFIG_CLUSTER_MAX_SLAVES
#define CLUSTER_WORK_QUEUE_SIZE     CONFIG_CLUSTER_WORK_QUEUE_SIZE
#define CLUSTER_SHARE_QUEUE_SIZE    CONFIG_CLUSTER_SHARE_QUEUE_SIZE
#define CLUSTER_HEARTBEAT_MS        CONFIG_CLUSTER_HEARTBEAT_MS
#define CLUSTER_TIMEOUT_MS          CONFIG_CLUSTER_TIMEOUT_MS
#define CLUSTER_NONCE_RANGE_BITS    28

// BAP Message Types (NMEA-style sentence identifiers)
#define BAP_MSG_WORK        "CLWRK"     // Work distribution: master -> slave
#define BAP_MSG_SHARE       "CLSHR"     // Share found: slave -> master
#define BAP_MSG_SYNC        "CLSYN"     // Sync/difficulty update
#define BAP_MSG_HEARTBEAT   "CLHBT"     // Keepalive ping/pong
#define BAP_MSG_CONFIG      "CLCFG"     // Cluster configuration
#define BAP_MSG_STATUS      "CLSTS"     // Status report
#define BAP_MSG_ACK         "CLACK"     // Acknowledgment
#define BAP_MSG_REGISTER    "CLREG"     // Slave registration
#define BAP_MSG_TIMING      "CLTIM"     // Timing sync: master -> slave (auto-timing interval)

// Protocol constants
#define CLUSTER_MSG_START       '$'
#define CLUSTER_MSG_CHECKSUM    '*'
#define CLUSTER_MSG_TERMINATOR  "\r\n"
#define CLUSTER_MSG_MAX_LEN     512

// ============================================================================
// Type Definitions
// ============================================================================

/**
 * @brief Cluster operating mode
 */
typedef enum {
    CLUSTER_MODE_DISABLED = 0,  // Normal standalone operation
    CLUSTER_MODE_MASTER,        // Act as cluster master
    CLUSTER_MODE_SLAVE          // Act as cluster slave
} cluster_mode_t;

/**
 * @brief Slave connection state
 */
typedef enum {
    SLAVE_STATE_DISCONNECTED = 0,
    SLAVE_STATE_REGISTERING,
    SLAVE_STATE_ACTIVE,
    SLAVE_STATE_STALE          // Missed heartbeats
} slave_state_t;

/**
 * @brief Mining work unit distributed to slaves
 */
typedef struct {
    uint8_t  target_slave_id;           // Which slave this work is for (for broadcast filtering)
    uint32_t job_id;                    // Unique job identifier
    uint8_t  prev_block_hash[32];       // Previous block hash
    uint8_t  merkle_root[32];           // Merkle root (or coinbase for construction)
    uint32_t version;                   // Block version
    uint32_t version_mask;              // Version rolling mask (for AsicBoost)
    uint32_t nbits;                     // Difficulty target (compact)
    uint32_t ntime;                     // Block timestamp
    uint32_t nonce_start;               // Start of assigned nonce range
    uint32_t nonce_end;                 // End of assigned nonce range
    uint8_t  extranonce2[8];            // Assigned extranonce2 value
    uint8_t  extranonce2_len;           // Length of extranonce2
    bool     clean_jobs;                // Clear pending work flag
    int64_t  timestamp;                 // When work was distributed
    uint32_t pool_diff;                 // Pool difficulty requirement
    uint8_t  pool_id;                   // Pool ID: 0=primary, 1=secondary (for dual pool mode)
    // Display info for slave UI
    uint32_t block_height;              // Current block height
    char     scriptsig[32];             // Pool tag from coinbase (truncated)
    char     network_diff_str[16];      // Network difficulty string for display
} cluster_work_t;

/**
 * @brief Share found by slave
 */
typedef struct {
    uint32_t job_id;                    // Job this share belongs to
    uint32_t nonce;                     // Winning nonce
    uint8_t  extranonce2[8];            // Extranonce2 used
    uint8_t  extranonce2_len;           // Length of extranonce2
    uint32_t ntime;                     // Timestamp (may be rolled)
    uint32_t version;                   // Version (may be rolled for AsicBoost)
    uint8_t  slave_id;                  // Which slave found it
    int64_t  timestamp;                 // When share was found
    uint8_t  pool_id;                   // Pool ID: 0=primary, 1=secondary (for dual pool mode)
} cluster_share_t;

/**
 * @brief Slave node information (tracked by master)
 */
typedef struct {
    uint8_t         slave_id;           // Slave ID (0-7)
    slave_state_t   state;              // Connection state
    char            hostname[32];       // Slave hostname/identifier
    char            ip_addr[16];        // Slave IP address (xxx.xxx.xxx.xxx)
    uint8_t         mac_addr[6];        // ESP-NOW MAC address
    uint32_t        hashrate;           // Last reported hashrate (GH/s * 100)
    uint32_t        shares_submitted;   // Total shares submitted by slave
    uint32_t        shares_accepted;    // Shares accepted by pool
    uint32_t        shares_rejected;    // Shares rejected by pool
    // Per-pool stats for dual pool mode
    uint32_t        shares_accepted_primary;
    uint32_t        shares_rejected_primary;
    uint32_t        shares_accepted_secondary;
    uint32_t        shares_rejected_secondary;
    int64_t         last_heartbeat;     // Last heartbeat timestamp
    int64_t         last_seen;          // Last activity timestamp (ms since boot)
    int64_t         last_work_sent;     // Last work distribution time
    uint32_t        nonce_range_start;  // Assigned nonce range start
    uint32_t        nonce_range_size;   // Size of nonce range
    float           temperature;        // Last reported temperature
    uint16_t        fan_rpm;            // Last reported fan RPM
    uint16_t        frequency;          // ASIC frequency (MHz)
    uint16_t        core_voltage;       // Core voltage (mV)
    float           power;              // Power consumption (W)
    float           voltage_in;         // Input voltage (V)
} cluster_slave_t;

/**
 * @brief Master state structure
 */
typedef struct {
    bool                initialized;
    uint8_t             slave_count;
    cluster_slave_t     slaves[CLUSTER_MAX_SLAVES];
    SemaphoreHandle_t   slaves_mutex;

    // Work management
    cluster_work_t      current_work;
    bool                work_valid;
    SemaphoreHandle_t   work_mutex;

    // Share submission queue
    QueueHandle_t       share_queue;

    // Statistics
    uint32_t            total_hashrate;     // Combined cluster hashrate
    uint32_t            total_shares;
    uint32_t            work_distributed;

    // Tasks
    TaskHandle_t        coordinator_task;
    TaskHandle_t        share_submitter_task;
} cluster_master_state_t;

/**
 * @brief Slave state structure
 */
typedef struct {
    bool                initialized;
    bool                registered;
    uint8_t             my_id;              // Assigned slave ID
    char                master_hostname[32];

    // Current work
    cluster_work_t      current_work;
    bool                work_valid;
    SemaphoreHandle_t   work_mutex;

    // Share queue (to send to master)
    QueueHandle_t       share_queue;

    // Statistics
    uint32_t            shares_found;
    uint32_t            shares_submitted;
    int64_t             last_work_received;

    // Tasks
    TaskHandle_t        worker_task;
    TaskHandle_t        heartbeat_task;
} cluster_slave_state_t;

/**
 * @brief Global cluster state
 */
typedef struct {
    cluster_mode_t      mode;
    union {
        cluster_master_state_t  master;
        cluster_slave_state_t   slave;
    };
} cluster_state_t;

/**
 * @brief Cluster statistics (for API)
 */
typedef struct {
    uint32_t total_hashrate;        // Combined cluster hashrate (GH/s * 100)
    uint32_t total_shares;          // Total shares found by cluster
    uint32_t total_shares_accepted; // Total shares accepted by pool
    uint32_t total_shares_rejected; // Total shares rejected by pool
    // Per-pool stats for dual pool mode
    uint32_t primary_shares_accepted;
    uint32_t primary_shares_rejected;
    uint32_t secondary_shares_accepted;
    uint32_t secondary_shares_rejected;
} cluster_stats_t;

// ============================================================================
// Public API - Initialization
// ============================================================================

/**
 * @brief Initialize the cluster subsystem
 * @param mode Operating mode (MASTER, SLAVE, or DISABLED)
 * @return ESP_OK on success
 */
esp_err_t cluster_init(cluster_mode_t mode);

/**
 * @brief Deinitialize and clean up cluster resources
 */
void cluster_deinit(void);

/**
 * @brief Get current cluster mode
 */
cluster_mode_t cluster_get_mode(void);

/**
 * @brief Check if cluster is active
 */
bool cluster_is_active(void);

/**
 * @brief Set cluster mode (requires reboot to take effect)
 */
esp_err_t cluster_set_mode(cluster_mode_t mode);

/**
 * @brief Get cluster status as JSON string for web API
 */
void cluster_get_status(char *status_json, size_t max_len);

// ============================================================================
// Public API - Master Functions
// ============================================================================

#if CLUSTER_ENABLED

/**
 * @brief Distribute new work to all active slaves
 * Called by stratum client when mining.notify received
 * @param work New work unit from stratum
 * @return ESP_OK on success
 */
esp_err_t cluster_master_distribute_work(const cluster_work_t *work);

/**
 * @brief Handle incoming share from slave
 * @param share Share data received via BAP
 * @return ESP_OK on success
 */
esp_err_t cluster_master_receive_share(const cluster_share_t *share);

/**
 * @brief Get combined cluster statistics
 * @param stats Output: cluster statistics (can be NULL)
 * @param active_slaves Output: number of active slaves (can be NULL)
 */
void cluster_master_get_stats(cluster_stats_t *stats, uint8_t *active_slaves);

/**
 * @brief Get slave information by ID
 * @param slave_id Slave ID (0-7)
 * @param slave Output: slave information
 * @return ESP_OK if slave exists
 */
esp_err_t cluster_master_get_slave(uint8_t slave_id, cluster_slave_t *slave);

/**
 * @brief Get slave information by slot index (for API enumeration)
 * @param slot_index Slot index (0 to CLUSTER_MAX_SLAVES-1)
 * @param slave Output: slave information with additional fields for API
 * @return ESP_OK if slot contains a slave
 */
esp_err_t cluster_master_get_slave_info(uint8_t slot_index, cluster_slave_t *slave);

/**
 * @brief Handle slave registration request (called by BAP message handler)
 * @param hostname Slave hostname
 * @param ip_addr Slave IP address (can be NULL or empty)
 */
esp_err_t cluster_master_handle_registration(const char *hostname,
                                              const char *ip_addr);

/**
 * @brief Handle slave heartbeat with extended data (called by BAP message handler)
 * @param data Extended heartbeat data including frequency, voltage, power
 */
esp_err_t cluster_master_handle_heartbeat_ex(const cluster_heartbeat_data_t *data);

/**
 * @brief Handle slave heartbeat (legacy, for backwards compatibility)
 */
esp_err_t cluster_master_handle_heartbeat(uint8_t slave_id,
                                           uint32_t hashrate,
                                           float temp,
                                           uint16_t fan_rpm);

/**
 * @brief Broadcast timing interval to all slaves (auto-timing sync)
 * @param interval_ms Job interval in milliseconds (500-800)
 */
void cluster_master_broadcast_timing(uint16_t interval_ms);

// ============================================================================
// Public API - Slave Functions
// ============================================================================

/**
 * @brief Register with master node
 * @param hostname Our hostname/identifier
 * @return ESP_OK on success
 */
esp_err_t cluster_slave_register(const char *hostname);

/**
 * @brief Get current work from master
 * @param work Output: current work unit
 * @return ESP_OK if valid work available
 */
esp_err_t cluster_slave_get_work(cluster_work_t *work);

/**
 * @brief Submit share to master
 * @param share Share data to submit
 * @return ESP_OK on success
 */
esp_err_t cluster_slave_submit_share(const cluster_share_t *share);

/**
 * @brief Check if we have valid work
 */
bool cluster_slave_has_work(void);

/**
 * @brief Process work received from master (called by BAP message handler)
 */
esp_err_t cluster_slave_receive_work(const cluster_work_t *work);

/**
 * @brief Handle registration acknowledgment from master
 */
esp_err_t cluster_slave_handle_ack(uint8_t assigned_id, const char *hostname);

/**
 * @brief Called by ASIC driver when a share is found in slave mode
 * @param nonce The winning nonce
 * @param job_id Numeric job ID
 * @param version The actual rolled version bits from ASIC
 * @param ntime The ntime value (may be rolled)
 * @param extranonce2_hex The extranonce2 as hex string from the ASIC job
 */
void cluster_slave_on_share_found(uint32_t nonce, uint32_t job_id, uint32_t version, uint32_t ntime, const char *extranonce2_hex);

/**
 * @brief Get slave share statistics
 * @param shares_found Output: number of shares found locally
 * @param shares_submitted Output: number of shares submitted to master
 */
void cluster_slave_get_shares(uint32_t *shares_found, uint32_t *shares_submitted);

#endif // CLUSTER_ENABLED

// ============================================================================
// Public API - BAP Message Handling
// ============================================================================

/**
 * @brief Process incoming BAP cluster message
 * Called by BAP UART handler when cluster message received
 * @param msg_type Message type identifier
 * @param payload Message payload (after type, before checksum)
 * @param len Payload length
 * @return ESP_OK on success
 */
esp_err_t cluster_handle_bap_message(const char *msg_type,
                                      const char *payload,
                                      size_t len);

/**
 * @brief Send cluster message via BAP
 * @param msg_type Message type identifier
 * @param payload Message payload
 * @return ESP_OK on success
 */
esp_err_t cluster_send_bap_message(const char *msg_type,
                                    const char *payload);

/**
 * @brief Called by BAP UART handler when a cluster message is received
 * This function should be registered with the BAP subsystem
 * @param message Complete received message (including $ and checksum)
 */
void cluster_on_bap_message_received(const char *message);

/**
 * @brief Check if a BAP message is a cluster message (starts with $CL)
 */
bool cluster_is_cluster_message(const char *message);

#ifdef __cplusplus
}
#endif

#endif // CLUSTER_H
