/**
 * @file cluster_transport.h
 * @brief Transport abstraction layer for ClusterAxe
 *
 * Provides a unified API for cluster communication over different transports:
 * - BAP (UART cable) - Original wired transport
 * - ESP-NOW (Wireless) - New wireless transport
 *
 * @author ClusterAxe Project
 * @license GPL-3.0
 */

#ifndef CLUSTER_TRANSPORT_H
#define CLUSTER_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cluster_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Transport Types
// ============================================================================

typedef enum {
    CLUSTER_TRANSPORT_NONE = 0,
    CLUSTER_TRANSPORT_BAP,          // UART/BAP cable
    CLUSTER_TRANSPORT_ESPNOW        // ESP-NOW wireless
} cluster_transport_type_t;

// Special slave_id values
#define CLUSTER_TRANSPORT_BROADCAST     0xFF    // Send to all slaves
#define CLUSTER_TRANSPORT_MASTER        0xFE    // Send to master

// ============================================================================
// Callback Types
// ============================================================================

/**
 * @brief Receive callback function type
 * @param data Received message data
 * @param len Message length
 * @param src_id Source slave ID (or 0xFE for master, 0xFF for unknown)
 * @param ctx User context pointer
 */
typedef void (*cluster_transport_rx_cb_t)(const uint8_t *data,
                                           size_t len,
                                           uint8_t src_id,
                                           void *ctx);

/**
 * @brief Send complete callback function type
 * @param dst_id Destination slave ID
 * @param success True if send was successful
 */
typedef void (*cluster_transport_tx_cb_t)(uint8_t dst_id, bool success);

/**
 * @brief Peer discovered callback (ESP-NOW only)
 * @param mac_addr Peer MAC address
 * @param rssi Signal strength
 */
typedef void (*cluster_transport_peer_cb_t)(const uint8_t *mac_addr, int8_t rssi);

// ============================================================================
// Transport Info Structure
// ============================================================================

typedef struct {
    cluster_transport_type_t type;
    bool initialized;
    bool discovery_active;

    // ESP-NOW specific
    uint8_t self_mac[6];
    uint8_t channel;
    bool encrypted;
    uint8_t peer_count;
} cluster_transport_info_t;

// ============================================================================
// Core Transport API
// ============================================================================

/**
 * @brief Initialize the transport layer
 * @param type Transport type to use
 * @return ESP_OK on success
 */
esp_err_t cluster_transport_init(cluster_transport_type_t type);

/**
 * @brief Deinitialize transport and free resources
 */
void cluster_transport_deinit(void);

/**
 * @brief Get current transport type
 * @return Active transport type
 */
cluster_transport_type_t cluster_transport_get_type(void);

/**
 * @brief Get transport info
 * @param info Output: transport information
 * @return ESP_OK on success
 */
esp_err_t cluster_transport_get_info(cluster_transport_info_t *info);

/**
 * @brief Check if transport is ready
 * @return true if initialized and ready
 */
bool cluster_transport_is_ready(void);

// ============================================================================
// Send/Receive API
// ============================================================================

/**
 * @brief Send message to specific slave (master -> slave)
 * @param slave_id Target slave ID (0-15) or CLUSTER_TRANSPORT_BROADCAST
 * @param data Message data
 * @param len Message length
 * @return ESP_OK on success
 */
esp_err_t cluster_transport_send(uint8_t slave_id, const uint8_t *data, size_t len);

/**
 * @brief Send message to master (slave -> master)
 * @param data Message data
 * @param len Message length
 * @return ESP_OK on success
 */
esp_err_t cluster_transport_send_to_master(const uint8_t *data, size_t len);

/**
 * @brief Broadcast message to all peers
 * @param data Message data
 * @param len Message length
 * @return ESP_OK on success
 */
esp_err_t cluster_transport_broadcast(const uint8_t *data, size_t len);

/**
 * @brief Register receive callback
 * @param cb Callback function
 * @param ctx User context (passed to callback)
 * @return ESP_OK on success
 */
esp_err_t cluster_transport_register_rx_callback(cluster_transport_rx_cb_t cb, void *ctx);

/**
 * @brief Register send complete callback
 * @param cb Callback function
 * @return ESP_OK on success
 */
esp_err_t cluster_transport_register_tx_callback(cluster_transport_tx_cb_t cb);

// ============================================================================
// Discovery API (ESP-NOW)
// ============================================================================

/**
 * @brief Start peer discovery (master mode)
 * Broadcasts discovery beacons so slaves can find the master.
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if BAP transport
 */
esp_err_t cluster_transport_start_discovery(void);

/**
 * @brief Stop peer discovery
 */
void cluster_transport_stop_discovery(void);

/**
 * @brief Check if discovery is active
 * @return true if broadcasting discovery beacons
 */
bool cluster_transport_is_discovering(void);

/**
 * @brief Register peer discovery callback
 * Called when a new peer is discovered (ESP-NOW only).
 * @param cb Callback function
 * @return ESP_OK on success
 */
esp_err_t cluster_transport_register_peer_callback(cluster_transport_peer_cb_t cb);

// ============================================================================
// Peer Management API
// ============================================================================

/**
 * @brief Add a peer (for directed communication)
 * @param slave_id Slave ID to assign
 * @param mac_addr Peer MAC address (6 bytes) - NULL for BAP
 * @return ESP_OK on success
 */
esp_err_t cluster_transport_add_peer(uint8_t slave_id, const uint8_t *mac_addr);

/**
 * @brief Remove a peer
 * @param slave_id Slave ID to remove
 * @return ESP_OK on success
 */
esp_err_t cluster_transport_remove_peer(uint8_t slave_id);

/**
 * @brief Get peer MAC address
 * @param slave_id Slave ID
 * @param mac_addr Output: MAC address (6 bytes)
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if peer doesn't exist
 */
esp_err_t cluster_transport_get_peer_mac(uint8_t slave_id, uint8_t *mac_addr);

/**
 * @brief Get peer signal strength (ESP-NOW only)
 * @param slave_id Slave ID
 * @return RSSI in dBm, or 0 if not available
 */
int8_t cluster_transport_get_rssi(uint8_t slave_id);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Get maximum message size for current transport
 * @return Maximum payload size in bytes
 */
size_t cluster_transport_get_max_msg_size(void);

/**
 * @brief Convert MAC address to string
 * @param mac MAC address (6 bytes)
 * @param str Output string buffer (at least 18 bytes)
 */
void cluster_transport_mac_to_str(const uint8_t *mac, char *str);

/**
 * @brief Parse MAC address from string
 * @param str MAC address string (XX:XX:XX:XX:XX:XX)
 * @param mac Output MAC address (6 bytes)
 * @return ESP_OK on success
 */
esp_err_t cluster_transport_str_to_mac(const char *str, uint8_t *mac);

#ifdef __cplusplus
}
#endif

#endif // CLUSTER_TRANSPORT_H
