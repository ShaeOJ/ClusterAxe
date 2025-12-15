/**
 * @file cluster_espnow.h
 * @brief ESP-NOW transport implementation for ClusterAxe
 *
 * Enables wireless cluster communication using Espressif's ESP-NOW protocol.
 * Uses native ESP-IDF ESP-NOW API for ESP-IDF 5.5.x compatibility.
 *
 * @author ClusterAxe Project
 * @license GPL-3.0
 */

#ifndef CLUSTER_ESPNOW_H
#define CLUSTER_ESPNOW_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
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

// ============================================================================
// Transport Callback Type
// ============================================================================

/**
 * @brief Receive callback function type
 * @param msg_type Message type string
 * @param payload Message payload
 * @param len Payload length
 * @param src_mac Source MAC address (6 bytes, may be NULL for non-ESP-NOW)
 * @param ctx User context
 */
typedef void (*cluster_transport_rx_cb_t)(const char *msg_type,
                                          const char *payload,
                                          size_t len,
                                          const uint8_t *src_mac,
                                          void *ctx);

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
 * @brief Send data to a specific peer by MAC address
 * @param dest_mac Destination MAC address (NULL for broadcast)
 * @param data Data to send
 * @param len Data length (max 250 bytes)
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_send(const uint8_t *dest_mac, const char *data, size_t len);

/**
 * @brief Broadcast data to all peers
 * @param data Data to send
 * @param len Data length
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_broadcast(const char *data, size_t len);

/**
 * @brief Set receive callback
 * @param callback Callback function
 * @param ctx User context
 */
void cluster_espnow_set_rx_callback(cluster_transport_rx_cb_t callback, void *ctx);

// ============================================================================
// Discovery API (Master only)
// ============================================================================

/**
 * @brief Start broadcasting discovery beacons (master)
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_start_discovery(void);

/**
 * @brief Stop discovery broadcasting
 */
void cluster_espnow_stop_discovery(void);

// ============================================================================
// Peer Management API
// ============================================================================

/**
 * @brief Add a peer by MAC address
 * @param mac Peer MAC address (6 bytes)
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_add_peer(const uint8_t *mac);

/**
 * @brief Remove a peer by MAC address
 * @param mac Peer MAC address (6 bytes)
 * @return ESP_OK on success
 */
esp_err_t cluster_espnow_remove_peer(const uint8_t *mac);

// ============================================================================
// Status/Info API
// ============================================================================

/**
 * @brief Get our MAC address
 * @param mac Output: MAC address (6 bytes)
 */
void cluster_espnow_get_self_mac(uint8_t *mac);

/**
 * @brief Get master MAC address (slaves only)
 * @param mac Output: Master MAC address (6 bytes)
 * @return true if master MAC is known, false otherwise
 */
bool cluster_espnow_get_master_mac(uint8_t *mac);

/**
 * @brief Get WiFi channel
 * @return Channel number
 */
uint8_t cluster_espnow_get_channel(void);

/**
 * @brief Handle WiFi reconnection - update channel and reset registration
 *
 * Call this when WiFi reconnects to ensure ESP-NOW uses the correct channel
 * and slaves can re-register with the master.
 */
void cluster_espnow_on_wifi_reconnect(void);

/**
 * @brief Reset registration state (for slaves)
 *
 * Forces re-registration with master on next beacon.
 */
void cluster_espnow_reset_registration(void);

#ifdef __cplusplus
}
#endif

#endif // CLUSTER_ESPNOW_H
