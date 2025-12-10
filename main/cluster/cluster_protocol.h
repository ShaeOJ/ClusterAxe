/**
 * @file cluster_protocol.h
 * @brief Bitaxe Cluster BAP Protocol Encoding/Decoding
 *
 * Message format follows NMEA-style sentences:
 *   $MSGTYPE,field1,field2,...*XX\r\n
 *
 * Where XX is a two-digit hex checksum (XOR of all chars between $ and *)
 *
 * @author Clusteraxe Project
 * @license GPL-3.0
 */

#ifndef CLUSTER_PROTOCOL_H
#define CLUSTER_PROTOCOL_H

#include "cluster.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Encoding Functions
// ============================================================================

/**
 * @brief Encode work unit for transmission to slave
 *
 * Format: $CLWRK,job_id,prevhash,merkle,version,nbits,ntime,nonce_start,nonce_end,en2,en2_len,clean*XX
 *
 * @param work Work unit to encode
 * @param buffer Output buffer
 * @param buffer_len Buffer size
 * @return Length of encoded message, or -1 on error
 */
int cluster_protocol_encode_work(const cluster_work_t *work,
                                  char *buffer,
                                  size_t buffer_len);

/**
 * @brief Encode share for transmission to master
 *
 * Format: $CLSHR,slave_id,job_id,nonce,ntime,version,en2,en2_len*XX
 *
 * @param share Share to encode
 * @param buffer Output buffer
 * @param buffer_len Buffer size
 * @return Length of encoded message, or -1 on error
 */
int cluster_protocol_encode_share(const cluster_share_t *share,
                                   char *buffer,
                                   size_t buffer_len);

/**
 * @brief Extended slave stats for heartbeat
 */
struct cluster_heartbeat_data {
    uint8_t     slave_id;
    uint32_t    hashrate;       // GH/s * 100
    float       temp;           // Celsius
    uint16_t    fan_rpm;
    uint32_t    shares;
    uint16_t    frequency;      // MHz
    uint16_t    core_voltage;   // mV
    float       power;          // Watts
    float       voltage_in;     // Input voltage
};

/**
 * @brief Encode heartbeat message with extended stats
 *
 * Format: $CLHBT,slave_id,hashrate,temp,fan_rpm,shares,freq,voltage,power,vin*XX
 *
 * @param data Heartbeat data structure
 * @param buffer Output buffer
 * @param buffer_len Buffer size
 * @return Length of encoded message, or -1 on error
 */
int cluster_protocol_encode_heartbeat_ex(const cluster_heartbeat_data_t *data,
                                          char *buffer,
                                          size_t buffer_len);

/**
 * @brief Encode heartbeat message (legacy, for backwards compatibility)
 *
 * Format: $CLHBT,slave_id,hashrate,temp,fan_rpm,shares*XX
 */
int cluster_protocol_encode_heartbeat(uint8_t slave_id,
                                       uint32_t hashrate,
                                       float temp,
                                       uint16_t fan_rpm,
                                       uint32_t shares,
                                       char *buffer,
                                       size_t buffer_len);

/**
 * @brief Encode registration message with IP address
 *
 * Format: $CLREG,hostname,ip_addr*XX
 */
int cluster_protocol_encode_register_ex(const char *hostname,
                                         const char *ip_addr,
                                         char *buffer,
                                         size_t buffer_len);

/**
 * @brief Encode registration message (legacy)
 *
 * Format: $CLREG,hostname*XX
 */
int cluster_protocol_encode_register(const char *hostname,
                                      char *buffer,
                                      size_t buffer_len);

/**
 * @brief Encode acknowledgment message
 *
 * Format: $CLACK,slave_id,status*XX
 */
int cluster_protocol_encode_ack(uint8_t slave_id,
                                 const char *status,
                                 char *buffer,
                                 size_t buffer_len);

// ============================================================================
// Decoding Functions
// ============================================================================

/**
 * @brief Decode work unit from received message
 *
 * @param payload Message payload (between type and checksum)
 * @param work Output work structure
 * @return ESP_OK on success
 */
esp_err_t cluster_protocol_decode_work(const char *payload,
                                        cluster_work_t *work);

/**
 * @brief Decode share from received message
 *
 * @param payload Message payload
 * @param share Output share structure
 * @return ESP_OK on success
 */
esp_err_t cluster_protocol_decode_share(const char *payload,
                                         cluster_share_t *share);

/**
 * @brief Decode heartbeat from received message (extended)
 *
 * @param payload Message payload
 * @param data Output: heartbeat data structure
 * @return ESP_OK on success
 */
esp_err_t cluster_protocol_decode_heartbeat_ex(const char *payload,
                                                cluster_heartbeat_data_t *data);

/**
 * @brief Decode heartbeat from received message (legacy)
 *
 * @param payload Message payload
 * @param slave_id Output: slave ID
 * @param hashrate Output: hashrate
 * @param temp Output: temperature
 * @param fan_rpm Output: fan RPM
 * @param shares Output: shares found
 * @return ESP_OK on success
 */
esp_err_t cluster_protocol_decode_heartbeat(const char *payload,
                                             uint8_t *slave_id,
                                             uint32_t *hashrate,
                                             float *temp,
                                             uint16_t *fan_rpm,
                                             uint32_t *shares);

/**
 * @brief Decode registration from received message (extended)
 */
esp_err_t cluster_protocol_decode_register_ex(const char *payload,
                                               char *hostname,
                                               size_t hostname_len,
                                               char *ip_addr,
                                               size_t ip_addr_len);

/**
 * @brief Decode registration from received message (legacy)
 */
esp_err_t cluster_protocol_decode_register(const char *payload,
                                            char *hostname,
                                            size_t hostname_len);

/**
 * @brief Decode acknowledgment from received message
 */
esp_err_t cluster_protocol_decode_ack(const char *payload,
                                       uint8_t *slave_id,
                                       char *status,
                                       size_t status_len);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Calculate NMEA-style checksum
 *
 * XOR of all characters in the string
 *
 * @param str String to checksum (excluding $ and *)
 * @return Checksum value (0-255)
 */
uint8_t cluster_protocol_calc_checksum(const char *str);

/**
 * @brief Verify message checksum
 *
 * @param message Complete message including $, *, and checksum
 * @return true if checksum is valid
 */
bool cluster_protocol_verify_checksum(const char *message);

/**
 * @brief Parse message type from received message
 *
 * @param message Complete message
 * @param msg_type Output: message type (5 chars + null)
 * @param payload Output: pointer to payload start
 * @return ESP_OK on success
 */
esp_err_t cluster_protocol_parse_message(const char *message,
                                          char *msg_type,
                                          const char **payload);

/**
 * @brief Convert byte array to hex string
 */
void cluster_protocol_bytes_to_hex(const uint8_t *bytes,
                                    size_t len,
                                    char *hex_str);

/**
 * @brief Convert hex string to byte array
 */
int cluster_protocol_hex_to_bytes(const char *hex_str,
                                   uint8_t *bytes,
                                   size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // CLUSTER_PROTOCOL_H
