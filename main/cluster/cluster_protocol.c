/**
 * @file cluster_protocol.c
 * @brief Bitaxe Cluster BAP Protocol Implementation
 *
 * @author Clusteraxe Project
 * @license GPL-3.0
 */

#include "cluster_protocol.h"
#include "cluster_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#if CLUSTER_ENABLED

static const char *TAG = "cluster_proto";

// ============================================================================
// Utility Functions
// ============================================================================

uint8_t cluster_protocol_calc_checksum(const char *str)
{
    uint8_t checksum = 0;
    while (*str) {
        checksum ^= (uint8_t)*str++;
    }
    return checksum;
}

bool cluster_protocol_verify_checksum(const char *message)
{
    if (!message || *message != CLUSTER_MSG_START) {
        return false;
    }

    // Find start and end of checksummed portion
    const char *start = message + 1;  // Skip $
    const char *end = strchr(start, CLUSTER_MSG_CHECKSUM);

    if (!end || strlen(end) < 3) {
        return false;
    }

    // Calculate checksum of payload
    uint8_t calc_checksum = 0;
    for (const char *p = start; p < end; p++) {
        calc_checksum ^= (uint8_t)*p;
    }

    // Parse expected checksum
    char hex[3] = {end[1], end[2], '\0'};
    uint8_t expected = (uint8_t)strtol(hex, NULL, 16);

    return calc_checksum == expected;
}

esp_err_t cluster_protocol_parse_message(const char *message,
                                          char *msg_type,
                                          const char **payload)
{
    if (!message || !msg_type || !payload) {
        return ESP_ERR_INVALID_ARG;
    }

    // Verify format
    if (*message != CLUSTER_MSG_START) {
        return ESP_ERR_INVALID_ARG;
    }

    // Verify checksum
    if (!cluster_protocol_verify_checksum(message)) {
        ESP_LOGW(TAG, "Checksum verification failed");
        return ESP_ERR_INVALID_CRC;
    }

    // Extract message type (5 characters after $)
    const char *type_start = message + 1;
    const char *comma = strchr(type_start, ',');

    if (!comma || (comma - type_start) != 5) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(msg_type, type_start, 5);
    msg_type[5] = '\0';

    // Payload starts after the comma
    *payload = comma + 1;

    return ESP_OK;
}

void cluster_protocol_bytes_to_hex(const uint8_t *bytes,
                                    size_t len,
                                    char *hex_str)
{
    for (size_t i = 0; i < len; i++) {
        sprintf(hex_str + (i * 2), "%02x", bytes[i]);
    }
    hex_str[len * 2] = '\0';
}

int cluster_protocol_hex_to_bytes(const char *hex_str,
                                   uint8_t *bytes,
                                   size_t max_len)
{
    size_t hex_len = strlen(hex_str);
    size_t byte_len = hex_len / 2;

    if (byte_len > max_len) {
        byte_len = max_len;
    }

    for (size_t i = 0; i < byte_len; i++) {
        char hex[3] = {hex_str[i*2], hex_str[i*2+1], '\0'};
        bytes[i] = (uint8_t)strtol(hex, NULL, 16);
    }

    return byte_len;
}

// ============================================================================
// Message Building Helper
// ============================================================================

/**
 * @brief Finalize message with checksum and terminator
 */
static int finalize_message(char *buffer, size_t buffer_len, size_t payload_len)
{
    // Calculate checksum (everything after $ up to current position)
    uint8_t checksum = cluster_protocol_calc_checksum(buffer + 1);

    // Append checksum and terminator
    int added = snprintf(buffer + payload_len, buffer_len - payload_len,
                         "*%02X\r\n", checksum);

    if (added < 0 || (size_t)added >= buffer_len - payload_len) {
        return -1;
    }

    return payload_len + added;
}

// ============================================================================
// Encoding Functions
// ============================================================================

int cluster_protocol_encode_work(const cluster_work_t *work,
                                  char *buffer,
                                  size_t buffer_len)
{
    if (!work || !buffer || buffer_len < 200) {
        return -1;
    }

    // Convert binary fields to hex strings
    char prev_hash_hex[65];
    char merkle_hex[65];
    char en2_hex[17];

    cluster_protocol_bytes_to_hex(work->prev_block_hash, 32, prev_hash_hex);
    cluster_protocol_bytes_to_hex(work->merkle_root, 32, merkle_hex);
    cluster_protocol_bytes_to_hex(work->extranonce2, work->extranonce2_len, en2_hex);

    // Build message
    // Format: $CLWRK,job_id,prevhash,merkle,version,nbits,ntime,nonce_start,nonce_end,en2,en2_len,clean
    int len = snprintf(buffer, buffer_len,
                       "$%s,%lu,%s,%s,%lu,%lu,%lu,%lu,%lu,%s,%u,%d",
                       BAP_MSG_WORK,
                       (unsigned long)work->job_id,
                       prev_hash_hex,
                       merkle_hex,
                       (unsigned long)work->version,
                       (unsigned long)work->nbits,
                       (unsigned long)work->ntime,
                       (unsigned long)work->nonce_start,
                       (unsigned long)work->nonce_end,
                       en2_hex,
                       work->extranonce2_len,
                       work->clean_jobs ? 1 : 0);

    if (len < 0 || (size_t)len >= buffer_len - 10) {
        return -1;
    }

    return finalize_message(buffer, buffer_len, len);
}

int cluster_protocol_encode_share(const cluster_share_t *share,
                                   char *buffer,
                                   size_t buffer_len)
{
    if (!share || !buffer || buffer_len < 100) {
        return -1;
    }

    char en2_hex[17];
    cluster_protocol_bytes_to_hex(share->extranonce2, share->extranonce2_len, en2_hex);

    // Format: $CLSHR,slave_id,job_id,nonce,ntime,version,en2,en2_len
    int len = snprintf(buffer, buffer_len,
                       "$%s,%u,%lu,%lu,%lu,%lu,%s,%u",
                       BAP_MSG_SHARE,
                       share->slave_id,
                       (unsigned long)share->job_id,
                       (unsigned long)share->nonce,
                       (unsigned long)share->ntime,
                       (unsigned long)share->version,
                       en2_hex,
                       share->extranonce2_len);

    if (len < 0 || (size_t)len >= buffer_len - 10) {
        return -1;
    }

    return finalize_message(buffer, buffer_len, len);
}

int cluster_protocol_encode_heartbeat(uint8_t slave_id,
                                       uint32_t hashrate,
                                       float temp,
                                       uint16_t fan_rpm,
                                       uint32_t shares,
                                       char *buffer,
                                       size_t buffer_len)
{
    if (!buffer || buffer_len < 64) {
        return -1;
    }

    // Format: $CLHBT,slave_id,hashrate,temp,fan_rpm,shares
    int len = snprintf(buffer, buffer_len,
                       "$%s,%u,%lu,%.1f,%u,%lu",
                       BAP_MSG_HEARTBEAT,
                       slave_id,
                       (unsigned long)hashrate,
                       temp,
                       fan_rpm,
                       (unsigned long)shares);

    if (len < 0 || (size_t)len >= buffer_len - 10) {
        return -1;
    }

    return finalize_message(buffer, buffer_len, len);
}

int cluster_protocol_encode_heartbeat_ex(const cluster_heartbeat_data_t *data,
                                          char *buffer,
                                          size_t buffer_len)
{
    if (!data || !buffer || buffer_len < 128) {
        return -1;
    }

    // Format: $CLHBT,slave_id,hashrate,temp,fan_rpm,shares,freq,voltage,power,vin
    int len = snprintf(buffer, buffer_len,
                       "$%s,%u,%lu,%.1f,%u,%lu,%u,%u,%.2f,%.2f",
                       BAP_MSG_HEARTBEAT,
                       data->slave_id,
                       (unsigned long)data->hashrate,
                       data->temp,
                       data->fan_rpm,
                       (unsigned long)data->shares,
                       data->frequency,
                       data->core_voltage,
                       data->power,
                       data->voltage_in);

    if (len < 0 || (size_t)len >= buffer_len - 10) {
        return -1;
    }

    return finalize_message(buffer, buffer_len, len);
}

int cluster_protocol_encode_register(const char *hostname,
                                      char *buffer,
                                      size_t buffer_len)
{
    if (!hostname || !buffer || buffer_len < 50) {
        return -1;
    }

    // Format: $CLREG,hostname
    int len = snprintf(buffer, buffer_len,
                       "$%s,%s",
                       BAP_MSG_REGISTER,
                       hostname);

    if (len < 0 || (size_t)len >= buffer_len - 10) {
        return -1;
    }

    return finalize_message(buffer, buffer_len, len);
}

int cluster_protocol_encode_register_ex(const char *hostname,
                                         const char *ip_addr,
                                         char *buffer,
                                         size_t buffer_len)
{
    if (!hostname || !ip_addr || !buffer || buffer_len < 64) {
        return -1;
    }

    // Format: $CLREG,hostname,ip_addr
    int len = snprintf(buffer, buffer_len,
                       "$%s,%s,%s",
                       BAP_MSG_REGISTER,
                       hostname,
                       ip_addr);

    if (len < 0 || (size_t)len >= buffer_len - 10) {
        return -1;
    }

    return finalize_message(buffer, buffer_len, len);
}

int cluster_protocol_encode_ack(uint8_t slave_id,
                                 const char *status,
                                 char *buffer,
                                 size_t buffer_len)
{
    if (!status || !buffer || buffer_len < 50) {
        return -1;
    }

    // Format: $CLACK,slave_id,status
    int len = snprintf(buffer, buffer_len,
                       "$%s,%u,%s",
                       BAP_MSG_ACK,
                       slave_id,
                       status);

    if (len < 0 || (size_t)len >= buffer_len - 10) {
        return -1;
    }

    return finalize_message(buffer, buffer_len, len);
}

// ============================================================================
// Decoding Functions
// ============================================================================

/**
 * @brief Helper to extract field from CSV payload
 */
static const char *get_next_field(const char *str, char *field, size_t max_len)
{
    if (!str || !field) return NULL;

    size_t i = 0;
    while (*str && *str != ',' && *str != '*' && i < max_len - 1) {
        field[i++] = *str++;
    }
    field[i] = '\0';

    // Skip comma if present
    if (*str == ',') str++;

    return (*str && *str != '*') ? str : NULL;
}

esp_err_t cluster_protocol_decode_work(const char *payload,
                                        cluster_work_t *work)
{
    if (!payload || !work) {
        return ESP_ERR_INVALID_ARG;
    }

    char field[128];
    const char *p = payload;

    memset(work, 0, sizeof(cluster_work_t));

    // job_id
    p = get_next_field(p, field, sizeof(field));
    if (!p && strlen(field) == 0) return ESP_ERR_INVALID_ARG;
    work->job_id = strtoul(field, NULL, 10);

    // prev_block_hash (hex)
    p = get_next_field(p, field, sizeof(field));
    if (strlen(field) != 64) return ESP_ERR_INVALID_ARG;
    cluster_protocol_hex_to_bytes(field, work->prev_block_hash, 32);

    // merkle_root (hex)
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    if (strlen(field) != 64) return ESP_ERR_INVALID_ARG;
    cluster_protocol_hex_to_bytes(field, work->merkle_root, 32);

    // version
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    work->version = strtoul(field, NULL, 10);

    // nbits
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    work->nbits = strtoul(field, NULL, 10);

    // ntime
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    work->ntime = strtoul(field, NULL, 10);

    // nonce_start
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    work->nonce_start = strtoul(field, NULL, 10);

    // nonce_end
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    work->nonce_end = strtoul(field, NULL, 10);

    // extranonce2 (hex)
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    work->extranonce2_len = cluster_protocol_hex_to_bytes(field, work->extranonce2, 8);

    // extranonce2_len (for verification)
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    uint8_t declared_len = (uint8_t)strtoul(field, NULL, 10);
    if (declared_len != work->extranonce2_len) {
        ESP_LOGW(TAG, "extranonce2 length mismatch: %d vs %d",
                 declared_len, work->extranonce2_len);
    }

    // clean_jobs
    get_next_field(p, field, sizeof(field));
    work->clean_jobs = (field[0] == '1');

    work->timestamp = esp_timer_get_time() / 1000;

    return ESP_OK;
}

esp_err_t cluster_protocol_decode_share(const char *payload,
                                         cluster_share_t *share)
{
    if (!payload || !share) {
        return ESP_ERR_INVALID_ARG;
    }

    char field[32];
    const char *p = payload;

    memset(share, 0, sizeof(cluster_share_t));

    // slave_id
    p = get_next_field(p, field, sizeof(field));
    share->slave_id = (uint8_t)strtoul(field, NULL, 10);

    // job_id
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    share->job_id = strtoul(field, NULL, 10);

    // nonce
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    share->nonce = strtoul(field, NULL, 10);

    // ntime
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    share->ntime = strtoul(field, NULL, 10);

    // version
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    share->version = strtoul(field, NULL, 10);

    // extranonce2 (hex)
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    share->extranonce2_len = cluster_protocol_hex_to_bytes(field, share->extranonce2, 8);

    // extranonce2_len
    get_next_field(p, field, sizeof(field));
    // Already parsed above

    share->timestamp = esp_timer_get_time() / 1000;

    return ESP_OK;
}

esp_err_t cluster_protocol_decode_heartbeat(const char *payload,
                                             uint8_t *slave_id,
                                             uint32_t *hashrate,
                                             float *temp,
                                             uint16_t *fan_rpm,
                                             uint32_t *shares)
{
    if (!payload) {
        return ESP_ERR_INVALID_ARG;
    }

    char field[32];
    const char *p = payload;

    // slave_id
    p = get_next_field(p, field, sizeof(field));
    if (slave_id) *slave_id = (uint8_t)strtoul(field, NULL, 10);

    // hashrate
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    if (hashrate) *hashrate = strtoul(field, NULL, 10);

    // temp
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    if (temp) *temp = strtof(field, NULL);

    // fan_rpm
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    if (fan_rpm) *fan_rpm = (uint16_t)strtoul(field, NULL, 10);

    // shares
    get_next_field(p, field, sizeof(field));
    if (shares) *shares = strtoul(field, NULL, 10);

    return ESP_OK;
}

esp_err_t cluster_protocol_decode_register(const char *payload,
                                            char *hostname,
                                            size_t hostname_len)
{
    if (!payload || !hostname || hostname_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Copy until comma, asterisk, or end
    size_t i = 0;
    while (payload[i] && payload[i] != ',' && payload[i] != '*' &&
           i < hostname_len - 1) {
        hostname[i] = payload[i];
        i++;
    }
    hostname[i] = '\0';

    return ESP_OK;
}

esp_err_t cluster_protocol_decode_register_ex(const char *payload,
                                               char *hostname,
                                               size_t hostname_len,
                                               char *ip_addr,
                                               size_t ip_addr_len)
{
    if (!payload || !hostname || hostname_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char field[64];
    const char *p = payload;

    // hostname
    p = get_next_field(p, field, sizeof(field));
    strncpy(hostname, field, hostname_len - 1);
    hostname[hostname_len - 1] = '\0';

    // ip_addr (optional - may not be present in legacy messages)
    if (p && ip_addr && ip_addr_len > 0) {
        get_next_field(p, field, sizeof(field));
        strncpy(ip_addr, field, ip_addr_len - 1);
        ip_addr[ip_addr_len - 1] = '\0';
    } else if (ip_addr && ip_addr_len > 0) {
        ip_addr[0] = '\0';  // No IP provided
    }

    return ESP_OK;
}

esp_err_t cluster_protocol_decode_heartbeat_ex(const char *payload,
                                                cluster_heartbeat_data_t *data)
{
    if (!payload || !data) {
        return ESP_ERR_INVALID_ARG;
    }

    char field[32];
    const char *p = payload;

    memset(data, 0, sizeof(cluster_heartbeat_data_t));

    // slave_id
    p = get_next_field(p, field, sizeof(field));
    data->slave_id = (uint8_t)strtoul(field, NULL, 10);

    // hashrate
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    data->hashrate = strtoul(field, NULL, 10);

    // temp
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    data->temp = strtof(field, NULL);

    // fan_rpm
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    data->fan_rpm = (uint16_t)strtoul(field, NULL, 10);

    // shares
    if (!p) return ESP_ERR_INVALID_ARG;
    p = get_next_field(p, field, sizeof(field));
    data->shares = strtoul(field, NULL, 10);

    // Extended fields (optional - may not be present in legacy messages)
    // frequency
    if (p) {
        p = get_next_field(p, field, sizeof(field));
        data->frequency = (uint16_t)strtoul(field, NULL, 10);
    }

    // core_voltage
    if (p) {
        p = get_next_field(p, field, sizeof(field));
        data->core_voltage = (uint16_t)strtoul(field, NULL, 10);
    }

    // power
    if (p) {
        p = get_next_field(p, field, sizeof(field));
        data->power = strtof(field, NULL);
    }

    // voltage_in
    if (p) {
        get_next_field(p, field, sizeof(field));
        data->voltage_in = strtof(field, NULL);
    }

    return ESP_OK;
}

esp_err_t cluster_protocol_decode_ack(const char *payload,
                                       uint8_t *slave_id,
                                       char *status,
                                       size_t status_len)
{
    if (!payload) {
        return ESP_ERR_INVALID_ARG;
    }

    char field[32];
    const char *p = payload;

    // slave_id
    p = get_next_field(p, field, sizeof(field));
    if (slave_id) *slave_id = (uint8_t)strtoul(field, NULL, 10);

    // status
    if (status && status_len > 0) {
        get_next_field(p, status, status_len);
    }

    return ESP_OK;
}

#endif // CLUSTER_ENABLED
