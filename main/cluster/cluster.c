/**
 * @file cluster.c
 * @brief Bitaxe Cluster Main Module
 *
 * Handles initialization, mode management, and BAP message routing
 * between master and slave implementations.
 *
 * @author Clusteraxe Project
 * @license GPL-3.0
 */

#include "cluster.h"
#include "cluster_protocol.h"
#include "cluster_config.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "string.h"

static const char *TAG = "cluster";

// ============================================================================
// Private State
// ============================================================================

static cluster_state_t g_cluster_state = {
    .mode = CLUSTER_MODE_DISABLED
};

#if CLUSTER_ENABLED

// External BAP UART functions (from existing ESP-Miner BAP implementation)
extern esp_err_t BAP_uart_send_raw(const char *data, size_t len);

// Forward declarations for master/slave init (implemented in cluster_master.c and cluster_slave.c)
extern esp_err_t cluster_master_init(cluster_master_state_t *state);
extern void cluster_master_deinit(void);
extern esp_err_t cluster_slave_init(cluster_slave_state_t *state);
extern void cluster_slave_deinit(void);

// ============================================================================
// NVS Configuration
// ============================================================================

#define NVS_NAMESPACE "cluster"
#define NVS_KEY_MODE  "mode"

/**
 * @brief Load cluster mode from NVS
 */
static cluster_mode_t load_mode_from_nvs(void)
{
    nvs_handle_t handle;
    cluster_mode_t mode = CLUSTER_MODE_DEFAULT;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        uint8_t mode_val = 0;
        if (nvs_get_u8(handle, NVS_KEY_MODE, &mode_val) == ESP_OK) {
            if (mode_val <= CLUSTER_MODE_SLAVE) {
                mode = (cluster_mode_t)mode_val;
            }
        }
        nvs_close(handle);
    }

    return mode;
}

/**
 * @brief Save cluster mode to NVS
 */
static esp_err_t save_mode_to_nvs(cluster_mode_t mode)
{
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(handle, NVS_KEY_MODE, (uint8_t)mode);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

// ============================================================================
// BAP UART Send Function
// ============================================================================

/**
 * @brief Send raw data via BAP (UART or ESP-NOW)
 * This wraps the existing BAP infrastructure
 */
esp_err_t BAP_uart_send_raw(const char *data, size_t len)
{
#if defined(CONFIG_CLUSTER_TRANSPORT_ESPNOW) || defined(CONFIG_CLUSTER_TRANSPORT_BOTH)
    // Forward declaration
    extern esp_err_t cluster_espnow_broadcast(const char *data, size_t len);
    
    // For now, we broadcast everything. In the future, we could parse the message
    // to find a destination address, but broadcast works for both master (discovery)
    // and slave (updates).
    return cluster_espnow_broadcast(data, len);

#else
    // This will be implemented to use the existing BAP UART send mechanism
    // For now, we'll use the external uart_write_bytes function
    extern int uart_write_bytes(int uart_num, const void *src, size_t size);
    #define BAP_UART_NUM 2  // UART_NUM_2

    int bytes_sent = uart_write_bytes(BAP_UART_NUM, data, len);
    return (bytes_sent == len) ? ESP_OK : ESP_FAIL;
#endif
}

#endif // CLUSTER_ENABLED

// ============================================================================
// Public API - Initialization
// ============================================================================

esp_err_t cluster_init(cluster_mode_t mode)
{
#if !CLUSTER_ENABLED
    ESP_LOGI(TAG, "Cluster support not compiled in");
    g_cluster_state.mode = CLUSTER_MODE_DISABLED;
    return ESP_OK;
#else
    ESP_LOGW(TAG, "=== CLUSTER_INIT CALLED: requested mode=%d ===", mode);

    if (g_cluster_state.mode != CLUSTER_MODE_DISABLED) {
        ESP_LOGW(TAG, "Cluster already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // If mode is disabled, check NVS for stored mode (or use compile-time default)
    if (mode == CLUSTER_MODE_DISABLED) {
        mode = load_mode_from_nvs();
        ESP_LOGW(TAG, "Loaded mode from NVS: %d", mode);
    }

    if (mode == CLUSTER_MODE_DISABLED) {
        ESP_LOGI(TAG, "Cluster mode disabled");
        return ESP_OK;
    }

    esp_err_t ret;

#if CLUSTER_IS_MASTER
    if (mode == CLUSTER_MODE_MASTER) {
        ESP_LOGW(TAG, "Initializing cluster MASTER mode");
        memset(&g_cluster_state.master, 0, sizeof(cluster_master_state_t));
        ret = cluster_master_init(&g_cluster_state.master);
        ESP_LOGW(TAG, "cluster_master_init returned: %s", esp_err_to_name(ret));
    }
    else {
        ESP_LOGE(TAG, "This firmware is built as MASTER only");
        return ESP_ERR_NOT_SUPPORTED;
    }
#elif CLUSTER_IS_SLAVE
    if (mode == CLUSTER_MODE_SLAVE) {
        ESP_LOGW(TAG, "Initializing cluster SLAVE mode");
        memset(&g_cluster_state.slave, 0, sizeof(cluster_slave_state_t));
        ret = cluster_slave_init(&g_cluster_state.slave);
        ESP_LOGW(TAG, "cluster_slave_init returned: %s", esp_err_to_name(ret));
    }
    else {
        ESP_LOGE(TAG, "This firmware is built as SLAVE only");
        return ESP_ERR_NOT_SUPPORTED;
    }
#else
    ESP_LOGE(TAG, "Cluster not enabled in this build");
    return ESP_ERR_NOT_SUPPORTED;
#endif

    if (ret == ESP_OK) {
        g_cluster_state.mode = mode;
        save_mode_to_nvs(mode);
        ESP_LOGW(TAG, "=== CLUSTER INIT COMPLETE: %s ===", CLUSTERAXE_VERSION_STRING);
    }

    return ret;
#endif
}

void cluster_deinit(void)
{
#if CLUSTER_IS_MASTER
    if (g_cluster_state.mode == CLUSTER_MODE_MASTER) {
        cluster_master_deinit();
    }
#elif CLUSTER_IS_SLAVE
    if (g_cluster_state.mode == CLUSTER_MODE_SLAVE) {
        cluster_slave_deinit();
    }
#endif

    g_cluster_state.mode = CLUSTER_MODE_DISABLED;
    ESP_LOGI(TAG, "Cluster deinitialized");
}

cluster_mode_t cluster_get_mode(void)
{
    return g_cluster_state.mode;
}

bool cluster_is_active(void)
{
    return g_cluster_state.mode != CLUSTER_MODE_DISABLED;
}

// ============================================================================
// BAP Message Handling
// ============================================================================

#if CLUSTER_ENABLED

esp_err_t cluster_send_bap_message(const char *msg_type,
                                    const char *payload)
{
    char buffer[CLUSTER_MSG_MAX_LEN];

    // Build message with type and payload
    int len = snprintf(buffer, sizeof(buffer), "$%s,%s", msg_type, payload);
    if (len < 0 || (size_t)len >= sizeof(buffer) - 10) {
        ESP_LOGE(TAG, "Message too long");
        return ESP_ERR_NO_MEM;
    }

    // Calculate and append checksum
    uint8_t checksum = cluster_protocol_calc_checksum(buffer + 1);
    len += snprintf(buffer + len, sizeof(buffer) - len, "*%02X\r\n", checksum);

    // Send via BAP UART
    ESP_LOGD(TAG, "Sending cluster message: %s", buffer);
    return BAP_uart_send_raw(buffer, len);
}

/**
 * @brief Route incoming BAP message to appropriate handler
 */
esp_err_t cluster_handle_bap_message(const char *msg_type,
                                      const char *payload,
                                      size_t len)
{
    if (!msg_type || !payload) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Received BAP message: type=%s, payload_len=%d", msg_type, (int)len);

    // ========================================================================
    // Master-side message handling
    // ========================================================================

#if CLUSTER_IS_MASTER
    if (g_cluster_state.mode == CLUSTER_MODE_MASTER) {

        // Slave registration request
        if (strcmp(msg_type, BAP_MSG_REGISTER) == 0) {
            char hostname[32];
            char ip_addr[16];
            esp_err_t ret = cluster_protocol_decode_register_ex(payload,
                                                                 hostname,
                                                                 sizeof(hostname),
                                                                 ip_addr,
                                                                 sizeof(ip_addr));
            if (ret == ESP_OK) {
                return cluster_master_handle_registration(hostname, ip_addr);
            }
            return ret;
        }

        // Slave heartbeat
        if (strcmp(msg_type, BAP_MSG_HEARTBEAT) == 0) {
            cluster_heartbeat_data_t hb_data;

            esp_err_t ret = cluster_protocol_decode_heartbeat_ex(payload, &hb_data);
            if (ret == ESP_OK) {
                return cluster_master_handle_heartbeat_ex(&hb_data);
            }
            return ret;
        }

        // Share from slave
        if (strcmp(msg_type, BAP_MSG_SHARE) == 0) {
            ESP_LOGW(TAG, "SHARE RX: Decoding share message from slave");
            cluster_share_t share;
            esp_err_t ret = cluster_protocol_decode_share(payload, &share);
            if (ret == ESP_OK) {
                ESP_LOGW(TAG, "SHARE RX: Decoded share - slave=%d, job=%lu, nonce=0x%08lX",
                         share.slave_id, (unsigned long)share.job_id, (unsigned long)share.nonce);
                return cluster_master_receive_share(&share);
            } else {
                ESP_LOGE(TAG, "SHARE RX: Failed to decode share: %s", esp_err_to_name(ret));
            }
            return ret;
        }
    }
#endif

    // ========================================================================
    // Slave-side message handling
    // ========================================================================

#if CLUSTER_IS_SLAVE
    if (g_cluster_state.mode == CLUSTER_MODE_SLAVE) {

        // Work from master
        if (strcmp(msg_type, BAP_MSG_WORK) == 0) {
            cluster_work_t work;
            esp_err_t ret = cluster_protocol_decode_work(payload, &work);
            if (ret == ESP_OK) {
                return cluster_slave_receive_work(&work);
            }
            return ret;
        }

        // Registration acknowledgment
        if (strcmp(msg_type, BAP_MSG_ACK) == 0) {
            uint8_t slave_id;
            char status[32];
            esp_err_t ret = cluster_protocol_decode_ack(payload,
                                                         &slave_id,
                                                         status,
                                                         sizeof(status));
            if (ret == ESP_OK) {
                return cluster_slave_handle_ack(slave_id, status);
            }
            return ret;
        }

        // Heartbeat response (just a keepalive confirmation)
        if (strcmp(msg_type, BAP_MSG_HEARTBEAT) == 0) {
            // Master echoed back our heartbeat - connection is alive
            ESP_LOGD(TAG, "Heartbeat acknowledged");
            return ESP_OK;
        }

        // Sync message (difficulty update, etc.)
        if (strcmp(msg_type, BAP_MSG_SYNC) == 0) {
            // TODO: Handle difficulty updates
            ESP_LOGD(TAG, "Received sync message");
            return ESP_OK;
        }
    }
#endif

    ESP_LOGW(TAG, "Unhandled message type: %s", msg_type);
    return ESP_ERR_NOT_SUPPORTED;
}

// ============================================================================
// Integration Hook for BAP UART
// ============================================================================

bool cluster_is_cluster_message(const char *message)
{
    if (!message || message[0] != '$') {
        return false;
    }
    // Check for "CL" prefix after the $
    return (strncmp(message + 1, "CL", 2) == 0);
}

void cluster_on_bap_message_received(const char *message)
{
    if (!message) return;

    // Verify this is a cluster message
    if (!cluster_is_cluster_message(message)) {
        return;  // Not a cluster message
    }

    // Parse and route the message
    char msg_type[6];
    const char *payload;

    if (cluster_protocol_parse_message(message, msg_type, &payload) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to parse cluster message");
        return;
    }

    // Find payload length (up to checksum marker)
    size_t payload_len = 0;
    const char *end = strchr(payload, '*');
    if (end) {
        payload_len = end - payload;
    } else {
        payload_len = strlen(payload);
    }

    cluster_handle_bap_message(msg_type, payload, payload_len);
}

// ============================================================================
// ESP-NOW Message Handler (with MAC address support)
// ============================================================================

#if defined(CONFIG_CLUSTER_TRANSPORT_ESPNOW) || defined(CONFIG_CLUSTER_TRANSPORT_BOTH)

// Forward declare extended registration handler
extern esp_err_t cluster_master_handle_registration_with_mac(const char *hostname,
                                                              const char *ip_addr,
                                                              const uint8_t *mac_addr);

/**
 * @brief Handle ESP-NOW message with source MAC address
 * This allows the master to track slave MAC addresses for direct communication
 */
esp_err_t cluster_handle_espnow_message(const char *msg_type,
                                         const char *payload,
                                         size_t len,
                                         const uint8_t *src_mac)
{
    if (!msg_type || !payload) {
        return ESP_ERR_INVALID_ARG;
    }

    if (src_mac) {
        ESP_LOGD(TAG, "Received ESP-NOW message: type=%s from %02X:%02X:%02X:%02X:%02X:%02X",
                 msg_type, src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5]);
    } else {
        ESP_LOGD(TAG, "Received ESP-NOW message: type=%s", msg_type);
    }

#if CLUSTER_IS_MASTER
    if (g_cluster_state.mode == CLUSTER_MODE_MASTER) {
        // Handle registration with MAC address
        if (strcmp(msg_type, "REGISTER") == 0 || strcmp(msg_type, BAP_MSG_REGISTER) == 0) {
            char hostname[32] = {0};
            char ip_addr[16] = {0};

            // Parse hostname,ip_addr from payload
            const char *comma = strchr(payload, ',');
            if (comma) {
                size_t hostname_len = comma - payload;
                if (hostname_len >= sizeof(hostname)) hostname_len = sizeof(hostname) - 1;
                strncpy(hostname, payload, hostname_len);

                // Find next comma or end (there might be checksum)
                const char *ip_start = comma + 1;
                const char *ip_end = strchr(ip_start, ',');
                if (!ip_end) ip_end = strchr(ip_start, '*');
                if (!ip_end) ip_end = ip_start + strlen(ip_start);

                size_t ip_len = ip_end - ip_start;
                if (ip_len >= sizeof(ip_addr)) ip_len = sizeof(ip_addr) - 1;
                strncpy(ip_addr, ip_start, ip_len);
            } else {
                strncpy(hostname, payload, sizeof(hostname) - 1);
            }

            if (src_mac) {
                ESP_LOGI(TAG, "ESP-NOW registration: %s (%s) from %02X:%02X:%02X:%02X:%02X:%02X",
                         hostname, ip_addr, src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5]);
            } else {
                ESP_LOGI(TAG, "ESP-NOW registration: %s (%s)", hostname, ip_addr);
            }

            return cluster_master_handle_registration_with_mac(hostname, ip_addr, src_mac);
        }
    }
#endif

    // Fall back to standard handler for other messages
    return cluster_handle_bap_message(msg_type, payload, len);
}

#endif // ESP-NOW transport

#else // !CLUSTER_ENABLED

// Stub implementations when cluster is disabled
esp_err_t cluster_send_bap_message(const char *msg_type, const char *payload)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t cluster_handle_bap_message(const char *msg_type, const char *payload, size_t len)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool cluster_is_cluster_message(const char *message)
{
    return false;
}

void cluster_on_bap_message_received(const char *message)
{
    // Do nothing when cluster is disabled
}

#endif // CLUSTER_ENABLED

// ============================================================================
// Mode Configuration API (for AxeOS settings)
// ============================================================================

esp_err_t cluster_set_mode(cluster_mode_t mode)
{
#if CLUSTER_ENABLED
    if (mode == g_cluster_state.mode) {
        return ESP_OK;  // No change
    }

    // Deinitialize current mode
    cluster_deinit();

    // Initialize new mode
    return cluster_init(mode);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void cluster_get_status(char *status_json, size_t max_len)
{
    if (!status_json || max_len < 100) return;

#if CLUSTER_ENABLED
    if (g_cluster_state.mode == CLUSTER_MODE_DISABLED) {
        snprintf(status_json, max_len,
                 "{\"mode\":\"disabled\",\"active\":false,\"version\":\"%s\"}",
                 CLUSTERAXE_VERSION_STRING);
    }
    else if (g_cluster_state.mode == CLUSTER_MODE_MASTER) {
        cluster_stats_t stats;
        uint8_t slaves;
        cluster_master_get_stats(&stats, &slaves);

        snprintf(status_json, max_len,
                 "{\"mode\":\"master\",\"active\":true,"
                 "\"slaves\":%u,\"total_hashrate\":%lu,"
                 "\"shares\":%lu,\"work_distributed\":%lu,"
                 "\"version\":\"%s\"}",
                 slaves,
                 (unsigned long)stats.total_hashrate,
                 (unsigned long)stats.total_shares,
                 (unsigned long)g_cluster_state.master.work_distributed,
                 CLUSTERAXE_VERSION_STRING);
    }
    else if (g_cluster_state.mode == CLUSTER_MODE_SLAVE) {
        snprintf(status_json, max_len,
                 "{\"mode\":\"slave\",\"active\":true,"
                 "\"registered\":%s,\"my_id\":%u,"
                 "\"shares_found\":%lu,\"shares_submitted\":%lu,"
                 "\"has_work\":%s,\"version\":\"%s\"}",
                 g_cluster_state.slave.registered ? "true" : "false",
                 g_cluster_state.slave.my_id,
                 (unsigned long)g_cluster_state.slave.shares_found,
                 (unsigned long)g_cluster_state.slave.shares_submitted,
                 g_cluster_state.slave.work_valid ? "true" : "false",
                 CLUSTERAXE_VERSION_STRING);
    }
#else
    snprintf(status_json, max_len,
             "{\"mode\":\"disabled\",\"active\":false,\"compiled\":false}");
#endif
}
