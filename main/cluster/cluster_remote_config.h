/**
 * @file cluster_remote_config.h
 * @brief Remote configuration protocol for ClusterAxe
 *
 * Enables master to read/write slave settings over the cluster transport.
 * Essential for ESP-NOW mode where slaves may not have direct web access.
 *
 * Protocol Messages:
 *   $CLCFG - Configuration request/response
 *   $CLCMD - Remote command execution
 *   $CLGET - Get setting value
 *   $CLSET - Set setting value
 *
 * @author ClusterAxe Project
 * @license GPL-3.0
 */

#ifndef CLUSTER_REMOTE_CONFIG_H
#define CLUSTER_REMOTE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cluster_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration Categories
// ============================================================================

typedef enum {
    CLUSTER_CFG_CAT_SYSTEM = 0,     // Hostname, device info
    CLUSTER_CFG_CAT_MINING,         // Frequency, voltage, fan
    CLUSTER_CFG_CAT_NETWORK,        // WiFi settings (if applicable)
    CLUSTER_CFG_CAT_CLUSTER,        // Cluster-specific settings
    CLUSTER_CFG_CAT_ALL             // Request all categories
} cluster_config_category_t;

// ============================================================================
// Setting IDs (for GET/SET operations)
// ============================================================================

// System settings (0x00 - 0x1F)
#define CLUSTER_SETTING_HOSTNAME        0x00
#define CLUSTER_SETTING_DEVICE_MODEL    0x01
#define CLUSTER_SETTING_FW_VERSION      0x02
#define CLUSTER_SETTING_UPTIME          0x03
#define CLUSTER_SETTING_FREE_HEAP       0x04
#define CLUSTER_SETTING_CHIP_TEMP       0x05

// Mining settings (0x20 - 0x3F)
#define CLUSTER_SETTING_FREQUENCY       0x20
#define CLUSTER_SETTING_CORE_VOLTAGE    0x21
#define CLUSTER_SETTING_FAN_SPEED       0x22    // Target fan speed %
#define CLUSTER_SETTING_FAN_MODE        0x23    // Auto/Manual
#define CLUSTER_SETTING_TARGET_TEMP     0x24    // Target temperature
#define CLUSTER_SETTING_HASHRATE        0x25    // Read-only
#define CLUSTER_SETTING_POWER           0x26    // Read-only
#define CLUSTER_SETTING_EFFICIENCY      0x27    // Read-only (J/TH)
#define CLUSTER_SETTING_ASIC_COUNT      0x28    // Read-only

// Network settings (0x40 - 0x5F)
#define CLUSTER_SETTING_WIFI_SSID       0x40
#define CLUSTER_SETTING_WIFI_PASS       0x41
#define CLUSTER_SETTING_IP_ADDR         0x42    // Read-only (current IP)
#define CLUSTER_SETTING_WIFI_STATUS     0x43    // Read-only

// Cluster settings (0x60 - 0x7F)
#define CLUSTER_SETTING_SLAVE_ID        0x60    // Read-only
#define CLUSTER_SETTING_MASTER_MAC      0x61    // Read-only (ESP-NOW)
#define CLUSTER_SETTING_TRANSPORT       0x62    // BAP/ESP-NOW
#define CLUSTER_SETTING_RSSI            0x63    // Read-only (ESP-NOW signal)

// ============================================================================
// Remote Commands
// ============================================================================

typedef enum {
    CLUSTER_CMD_RESTART = 0,        // Restart device
    CLUSTER_CMD_FACTORY_RESET,      // Factory reset
    CLUSTER_CMD_OTA_START,          // Start OTA update
    CLUSTER_CMD_SAVE_SETTINGS,      // Save current settings to NVS
    CLUSTER_CMD_LOAD_DEFAULTS,      // Load default settings
    CLUSTER_CMD_IDENTIFY,           // Flash LED for identification
    CLUSTER_CMD_START_MINING,       // Start/resume mining
    CLUSTER_CMD_STOP_MINING,        // Stop mining
    CLUSTER_CMD_CALIBRATE,          // Run ASIC calibration
} cluster_remote_cmd_t;

// ============================================================================
// Response Status Codes
// ============================================================================

typedef enum {
    CLUSTER_RESP_OK = 0,            // Success
    CLUSTER_RESP_ERROR,             // General error
    CLUSTER_RESP_INVALID_SETTING,   // Unknown setting ID
    CLUSTER_RESP_READ_ONLY,         // Cannot write this setting
    CLUSTER_RESP_INVALID_VALUE,     // Value out of range
    CLUSTER_RESP_NOT_SUPPORTED,     // Feature not supported
    CLUSTER_RESP_BUSY,              // Device busy, try later
    CLUSTER_RESP_AUTH_REQUIRED,     // Authentication needed
} cluster_response_status_t;

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Setting value (union for different types)
 */
typedef struct {
    uint8_t setting_id;
    uint8_t data_type;      // 0=uint32, 1=int32, 2=float, 3=string, 4=bool
    union {
        uint32_t    u32;
        int32_t     i32;
        float       f32;
        char        str[64];
        bool        b;
    } value;
} cluster_setting_value_t;

/**
 * @brief Configuration snapshot (all settings)
 */
typedef struct {
    // System
    char        hostname[32];
    char        device_model[16];
    char        fw_version[16];
    uint32_t    uptime_seconds;
    uint32_t    free_heap;

    // Mining
    uint16_t    frequency;          // MHz
    uint16_t    core_voltage;       // mV
    uint8_t     fan_speed;          // % (0-100)
    uint8_t     fan_mode;           // 0=auto, 1=manual
    uint8_t     target_temp;        // Celsius
    uint32_t    hashrate;           // GH/s * 100
    float       power;              // Watts
    float       efficiency;         // J/TH
    float       chip_temp;          // Celsius

    // Network
    char        wifi_ssid[32];
    char        ip_addr[16];
    uint8_t     wifi_status;        // 0=disconnected, 1=connected

    // Cluster
    uint8_t     slave_id;
    int8_t      rssi;               // dBm (ESP-NOW only)
} cluster_config_snapshot_t;

/**
 * @brief Remote command request
 */
typedef struct {
    uint8_t     slave_id;           // Target slave (0xFF = all)
    cluster_remote_cmd_t cmd;
    uint8_t     params[32];         // Command-specific parameters
    uint8_t     params_len;
} cluster_remote_cmd_request_t;

/**
 * @brief Remote command response
 */
typedef struct {
    uint8_t     slave_id;
    cluster_remote_cmd_t cmd;
    cluster_response_status_t status;
    char        message[64];        // Status message
} cluster_remote_cmd_response_t;

// ============================================================================
// Master API - Send configuration requests to slaves
// ============================================================================

/**
 * @brief Request full configuration from a slave
 * @param slave_id Target slave ID
 * @param callback Function to call with result
 * @param ctx User context
 * @return ESP_OK if request sent
 */
typedef void (*cluster_config_callback_t)(uint8_t slave_id,
                                           const cluster_config_snapshot_t *config,
                                           cluster_response_status_t status,
                                           void *ctx);

esp_err_t cluster_master_get_slave_config(uint8_t slave_id,
                                           cluster_config_callback_t callback,
                                           void *ctx);

/**
 * @brief Get a specific setting from a slave
 * @param slave_id Target slave ID
 * @param setting_id Setting to read
 * @param callback Function to call with result
 * @param ctx User context
 * @return ESP_OK if request sent
 */
typedef void (*cluster_setting_callback_t)(uint8_t slave_id,
                                            const cluster_setting_value_t *value,
                                            cluster_response_status_t status,
                                            void *ctx);

esp_err_t cluster_master_get_slave_setting(uint8_t slave_id,
                                            uint8_t setting_id,
                                            cluster_setting_callback_t callback,
                                            void *ctx);

/**
 * @brief Set a setting on a slave
 * @param slave_id Target slave ID (0xFF = all slaves)
 * @param value Setting ID and value to set
 * @param callback Function to call with result
 * @param ctx User context
 * @return ESP_OK if request sent
 */
esp_err_t cluster_master_set_slave_setting(uint8_t slave_id,
                                            const cluster_setting_value_t *value,
                                            cluster_setting_callback_t callback,
                                            void *ctx);

/**
 * @brief Execute remote command on a slave
 * @param request Command request
 * @param callback Function to call with result
 * @param ctx User context
 * @return ESP_OK if request sent
 */
typedef void (*cluster_cmd_callback_t)(const cluster_remote_cmd_response_t *response,
                                        void *ctx);

esp_err_t cluster_master_send_command(const cluster_remote_cmd_request_t *request,
                                       cluster_cmd_callback_t callback,
                                       void *ctx);

/**
 * @brief Convenience: Set frequency on slave
 */
esp_err_t cluster_master_set_slave_frequency(uint8_t slave_id, uint16_t freq_mhz);

/**
 * @brief Convenience: Set voltage on slave
 */
esp_err_t cluster_master_set_slave_voltage(uint8_t slave_id, uint16_t voltage_mv);

/**
 * @brief Convenience: Set fan speed on slave
 */
esp_err_t cluster_master_set_slave_fan(uint8_t slave_id, uint8_t speed_percent);

/**
 * @brief Convenience: Restart a slave
 */
esp_err_t cluster_master_restart_slave(uint8_t slave_id);

/**
 * @brief Convenience: Restart all slaves
 */
esp_err_t cluster_master_restart_all_slaves(void);

// ============================================================================
// Slave API - Handle incoming configuration requests
// ============================================================================

/**
 * @brief Initialize remote config handler on slave
 * @return ESP_OK on success
 */
esp_err_t cluster_slave_remote_config_init(void);

/**
 * @brief Handle incoming config request (called by message handler)
 * @param msg_type Message type (CLCFG, CLGET, CLSET, CLCMD)
 * @param payload Message payload
 * @param len Payload length
 * @return ESP_OK on success
 */
esp_err_t cluster_slave_handle_config_message(const char *msg_type,
                                               const char *payload,
                                               size_t len);

/**
 * @brief Get current config snapshot (for sending to master)
 * @param config Output: configuration snapshot
 * @return ESP_OK on success
 */
esp_err_t cluster_slave_get_config_snapshot(cluster_config_snapshot_t *config);

/**
 * @brief Apply a setting value
 * @param value Setting to apply
 * @return Response status
 */
cluster_response_status_t cluster_slave_apply_setting(const cluster_setting_value_t *value);

// ============================================================================
// Protocol Message Encoding/Decoding
// ============================================================================

/**
 * @brief Encode GET request message
 * Format: $CLGET,slave_id,setting_id*XX
 */
int cluster_encode_get_request(char *buf, size_t buf_len,
                                uint8_t slave_id, uint8_t setting_id);

/**
 * @brief Encode SET request message
 * Format: $CLSET,slave_id,setting_id,type,value*XX
 */
int cluster_encode_set_request(char *buf, size_t buf_len,
                                uint8_t slave_id,
                                const cluster_setting_value_t *value);

/**
 * @brief Encode config request message
 * Format: $CLCFG,slave_id,category*XX
 */
int cluster_encode_config_request(char *buf, size_t buf_len,
                                   uint8_t slave_id,
                                   cluster_config_category_t category);

/**
 * @brief Encode command request message
 * Format: $CLCMD,slave_id,cmd_id,params...*XX
 */
int cluster_encode_cmd_request(char *buf, size_t buf_len,
                                const cluster_remote_cmd_request_t *request);

/**
 * @brief Encode config response message
 * Format: $CLCFR,slave_id,status,data...*XX
 */
int cluster_encode_config_response(char *buf, size_t buf_len,
                                    uint8_t slave_id,
                                    cluster_response_status_t status,
                                    const cluster_config_snapshot_t *config);

/**
 * @brief Encode setting response message
 * Format: $CLSTR,slave_id,status,setting_id,type,value*XX
 */
int cluster_encode_setting_response(char *buf, size_t buf_len,
                                     uint8_t slave_id,
                                     cluster_response_status_t status,
                                     const cluster_setting_value_t *value);

/**
 * @brief Decode incoming config/setting messages
 */
esp_err_t cluster_decode_get_request(const char *payload, size_t len,
                                      uint8_t *slave_id, uint8_t *setting_id);

esp_err_t cluster_decode_set_request(const char *payload, size_t len,
                                      uint8_t *slave_id,
                                      cluster_setting_value_t *value);

esp_err_t cluster_decode_config_response(const char *payload, size_t len,
                                          uint8_t *slave_id,
                                          cluster_response_status_t *status,
                                          cluster_config_snapshot_t *config);

#ifdef __cplusplus
}
#endif

#endif // CLUSTER_REMOTE_CONFIG_H
