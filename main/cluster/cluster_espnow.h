/**
 * @file cluster_espnow.h
 * @brief ESP-NOW transport implementation for ClusterAxe
 *
 * Enables wireless cluster communication using Espressif's ESP-NOW protocol.
 * Provides peer discovery, encrypted communication, and signal strength monitoring.
 *
 * @author ClusterAxe Project
 * @license GPL-3.0
 */

#ifndef CLUSTER_ESPNOW_H
#define CLUSTER_ESPNOW_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_now.h"
#include "cluster_transport.h"
#include "cluster_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration (can be overridden by Kconfig)
// ============================================================================

#ifndef CONFIG_CLUSTER_ESPNOW_CHANNEL
#define CONFIG_CLUSTER_ESPNOW_CHANNEL           1
#endif

#ifndef CONFIG_CLUSTER_ESPNOW_DISCOVERY_INTERVAL_MS
#define CONFIG_CLUSTER_ESPNOW_DISCOVERY_INTERVAL_MS   1000
#endif

#ifndef CONFIG_CLUSTER_ESPNOW_ENCRYPT
#define CONFIG_CLUSTER_ESPNOW_ENCRYPT           0
#endif

#ifndef CONFIG_CLUSTER_ESPNOW_PMK
#define CONFIG_CLUSTER_ESPNOW_PMK               "ClusterAxe_PMK!"
#endif

// ============================================================================
// Constants
// ============================================================================

#define ESPNOW_MAX_PEERS            20      // ESP-NOW hardware limit
#define ESPNOW_MAX_ENCRYPTED_PEERS  17      // ESP-NOW hardware limit
#define ESPNOW_PMK_LEN              16      // Primary Master Key length
#define ESPNOW_LMK_LEN              16      // Local Master Key length
#define ESPNOW_MAC_LEN              6       // MAC address length

// ESP-NOW v2.0 max payload
#define ESPNOW_MAX_DATA_LEN         250     // Use v1.0 limit for compatibility

// Discovery message identifiers
#define ESPNOW_MSG_DISCOVERY        "CLDSC"  // Discovery beacon
#define ESPNOW_MSG_DISCOVER_RESP    "CLDRP"  // Discovery response

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief ESP-NOW peer information
 */
typedef struct {
    uint8_t     mac_addr[ESPNOW_MAC_LEN];   // Peer MAC address
    uint8_t     slave_id;                   // Assigned slave ID
    bool        active;                     // Peer slot in use
    bool        encrypted;                  // Using encryption
    uint8_t     lmk[ESPNOW_LMK_LEN];        // Local Master Key
    int8_t      rssi;                       // Last known signal strength
    int64_t     last_seen;                  // Last packet timestamp (ms)
    uint32_t    tx_count;                   // Packets sent
    uint32_t    rx_count;                   // Packets received
    uint32_t    tx_failures;                // Send failures
} cluster_espnow_peer_t;

/**
 * @brief ESP-NOW module state
 */
typedef struct {
    bool                    initialized;
    uint8_t                 self_mac[ESPNOW_MAC_LEN];
    uint8_t                 channel;
    bool                    encrypt_enabled;
    uint8_t                 pmk[ESPNOW_PMK_LEN];

    // Master's MAC (for slaves)
    uint8_t                 master_mac[ESPNOW_MAC_LEN];
    bool                    master_known;

    // Peer management
    cluster_espnow_peer_t   peers[CLUSTER_MAX_SLAVES];
    uint8_t                 peer_count;
    SemaphoreHandle_t       peers_mutex;

    // Discovery
    bool                    discovery_active;
    TaskHandle_t            discovery_task;
    char                    cluster_id[16];

    // Message handling
    QueueHandle_t           rx_queue;
    TaskHandle_t            rx_task;

    // Callbacks
    cluster_transport_rx_cb_t   rx_callback;
    void                       *rx_callback_ctx;
    cluster_transport_tx_cb_t   tx_callback;
    cluster_transport_peer_cb_t peer_callback;

    // Statistics
    uint32_t                total_tx;
    uint32_t                total_rx;
    uint32_t                total_tx_fail;
} cluster_espnow_state_t;

/**
 * @brief Received message (queued for processing)
 */
typedef struct {
    uint8_t     src_mac[ESPNOW_MAC_LEN];
    uint8_t     data[ESPNOW_MAX_DATA_LEN];
    size_t      data_len;
    int8_t      rssi;
    int64_t     timestamp;
} cluster_espnow_rx_msg_t;

// ============================================================================
// Initialization API
// ============================================================================

/**
 * @brief Initialize ESP-NOW transport
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_init(void);

/**
 * @brief Deinitialize ESP-NOW transport
 */
void cluster_espnow_deinit(void);

/**
 * @brief Check if ESP-NOW is initialized
 * @return true if initialized
 */
bool cluster_espnow_is_initialized(void);

// ============================================================================
// Send/Receive API
// ============================================================================

/**
 * @brief Send data to a specific peer
 * @param slave_id Target slave ID
 * @param data Data to send
 * @param len Data length
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_send(uint8_t slave_id, const uint8_t *data, size_t len);

/**
 * @brief Send data to master (for slaves)
 * @param data Data to send
 * @param len Data length
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_send_to_master(const uint8_t *data, size_t len);

/**
 * @brief Broadcast data to all peers
 * @param data Data to send
 * @param len Data length
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_broadcast(const uint8_t *data, size_t len);

/**
 * @brief Register receive callback
 * @param cb Callback function
 * @param ctx User context
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_register_rx_callback(cluster_transport_rx_cb_t cb, void *ctx);

/**
 * @brief Register send complete callback
 * @param cb Callback function
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_register_tx_callback(cluster_transport_tx_cb_t cb);

// ============================================================================
// Discovery API
// ============================================================================

/**
 * @brief Start broadcasting discovery beacons (master)
 * @param cluster_id Cluster identifier string
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_start_discovery(const char *cluster_id);

/**
 * @brief Stop discovery broadcasting
 */
void cluster_espnow_stop_discovery(void);

/**
 * @brief Check if discovery is active
 * @return true if broadcasting
 */
bool cluster_espnow_is_discovering(void);

/**
 * @brief Start listening for discovery beacons (slave)
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_start_listen(void);

/**
 * @brief Stop listening for discovery
 */
void cluster_espnow_stop_listen(void);

/**
 * @brief Register peer discovery callback
 * @param cb Callback function
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_register_peer_callback(cluster_transport_peer_cb_t cb);

// ============================================================================
// Peer Management API
// ============================================================================

/**
 * @brief Add a peer
 * @param slave_id Slave ID to assign
 * @param mac_addr Peer MAC address
 * @param encrypted Use encryption for this peer
 * @param lmk Local Master Key (if encrypted, can be NULL to use default)
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_add_peer(uint8_t slave_id,
                                   const uint8_t *mac_addr,
                                   bool encrypted,
                                   const uint8_t *lmk);

/**
 * @brief Remove a peer
 * @param slave_id Slave ID
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_remove_peer(uint8_t slave_id);

/**
 * @brief Get peer by slave ID
 * @param slave_id Slave ID
 * @param peer Output: peer information
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_get_peer(uint8_t slave_id, cluster_espnow_peer_t *peer);

/**
 * @brief Find peer by MAC address
 * @param mac_addr MAC address to search
 * @return Slave ID, or 0xFF if not found
 */
uint8_t cluster_espnow_find_peer_by_mac(const uint8_t *mac_addr);

/**
 * @brief Set master MAC address (for slaves)
 * @param mac_addr Master's MAC address
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_set_master_mac(const uint8_t *mac_addr);

/**
 * @brief Get master MAC address
 * @param mac_addr Output: MAC address
 * @return ESP_OK if master is known
 */
esp_err_t cluster_espnow_get_master_mac(uint8_t *mac_addr);

/**
 * @brief Get number of active peers
 * @return Number of peers
 */
uint8_t cluster_espnow_get_peer_count(void);

// ============================================================================
// Status/Info API
// ============================================================================

/**
 * @brief Get signal strength for a peer
 * @param slave_id Slave ID
 * @return RSSI in dBm, or 0 if unknown
 */
int8_t cluster_espnow_get_rssi(uint8_t slave_id);

/**
 * @brief Get our MAC address
 * @param mac_addr Output: MAC address (6 bytes)
 */
void cluster_espnow_get_mac(uint8_t *mac_addr);

/**
 * @brief Get WiFi channel
 * @return Channel number (1-14)
 */
uint8_t cluster_espnow_get_channel(void);

/**
 * @brief Get ESP-NOW statistics
 * @param tx_count Output: total packets sent
 * @param rx_count Output: total packets received
 * @param tx_fail Output: total send failures
 */
void cluster_espnow_get_stats(uint32_t *tx_count, uint32_t *rx_count, uint32_t *tx_fail);

#ifdef __cplusplus
}
#endif

#endif // CLUSTER_ESPNOW_H
