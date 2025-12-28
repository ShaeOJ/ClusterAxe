/**
 * @file cluster_master.c
 * @brief Bitaxe Cluster Master Implementation
 *
 * The master node maintains the stratum connection and coordinates
 * work distribution to slave nodes via BAP UART.
 *
 * @author Clusteraxe Project
 * @license GPL-3.0
 */

#include "cluster.h"
#include "cluster_protocol.h"
#include "cluster_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "string.h"
#include "stdio.h"

#if CLUSTER_ENABLED && CLUSTER_IS_MASTER

static const char *TAG = "cluster_master";

// ============================================================================
// External References (from ESP-Miner codebase)
// ============================================================================

// Forward declare the stratum submit function - will be linked from stratum code
// The actual implementation will call this when submitting shares
extern void stratum_submit_share_from_cluster(uint32_t job_id, uint32_t nonce,
                                               uint8_t *extranonce2, uint8_t en2_len,
                                               uint32_t ntime, uint32_t version,
                                               uint8_t slave_id);

// ============================================================================
// Private State
// ============================================================================

static cluster_master_state_t *g_master = NULL;

// ============================================================================
// Nonce Range Management
// ============================================================================

/**
 * @brief Calculate nonce range for a slave based on total slave count
 *
 * Divides the 32-bit nonce space evenly among all nodes (master + slaves)
 * Master gets slot 0, slaves get slots 1-N
 */
static void calculate_nonce_ranges(void)
{
    if (!g_master) return;

    xSemaphoreTake(g_master->slaves_mutex, portMAX_DELAY);

    // Count active slaves
    uint8_t active_count = 0;
    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (g_master->slaves[i].state == SLAVE_STATE_ACTIVE) {
            active_count++;
        }
    }

    // Total nodes = master + active slaves
    uint8_t total_nodes = 1 + active_count;

    // Calculate range size per node
    // Using upper bits for partitioning to allow nonce rolling within range
    uint32_t range_size = 0xFFFFFFFF / total_nodes;

    // Assign ranges to active slaves (master implicitly gets range 0)
    uint8_t slot = 1;
    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (g_master->slaves[i].state == SLAVE_STATE_ACTIVE) {
            g_master->slaves[i].nonce_range_start = slot * range_size;
            g_master->slaves[i].nonce_range_size = range_size;

            ESP_LOGI(TAG, "Slave %d: nonce range 0x%08lX - 0x%08lX",
                     i,
                     (unsigned long)g_master->slaves[i].nonce_range_start,
                     (unsigned long)(g_master->slaves[i].nonce_range_start + range_size - 1));

            slot++;
        }
    }

    xSemaphoreGive(g_master->slaves_mutex);
}

// ============================================================================
// Extranonce2 Management
// ============================================================================

/**
 * @brief Generate unique extranonce2 for a slave
 *
 * Uses slave ID to ensure unique values across cluster
 */
static void generate_extranonce2_for_slave(uint8_t slave_id,
                                            uint8_t *extranonce2,
                                            uint8_t len)
{
    // Clear extranonce2
    memset(extranonce2, 0, len);

    // Embed slave ID in first byte(s)
    // This ensures each slave produces unique coinbase transactions
    if (len >= 1) {
        extranonce2[0] = slave_id + 1;  // +1 because master uses 0
    }

    // Add timestamp-based component for additional uniqueness
    if (len >= 4) {
        uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000000);
        extranonce2[1] = (ts >> 16) & 0xFF;
        extranonce2[2] = (ts >> 8) & 0xFF;
        extranonce2[3] = ts & 0xFF;
    }
}

// ============================================================================
// Work Distribution
// ============================================================================

/**
 * @brief Send work to a specific slave via ESP-NOW broadcast
 *
 * Uses broadcast instead of unicast because unicast was unreliable.
 * The work message includes target_slave_id so slaves can filter.
 */
static esp_err_t send_work_to_slave(uint8_t slave_id, const cluster_work_t *work)
{
    cluster_slave_t *slave = &g_master->slaves[slave_id];

    if (slave->state != SLAVE_STATE_ACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }

    // Create work packet with slave-specific parameters
    cluster_work_t slave_work = *work;
    slave_work.target_slave_id = slave_id;  // Tag which slave this is for
    slave_work.nonce_start = slave->nonce_range_start;
    slave_work.nonce_end = slave->nonce_range_start + slave->nonce_range_size - 1;

    // Generate unique extranonce2 for this slave
    generate_extranonce2_for_slave(slave_id,
                                    slave_work.extranonce2,
                                    slave_work.extranonce2_len);

    // Compute the proper merkle root for this slave's extranonce2
    // This is essential - without the correct merkle root, the slave can't mine valid blocks
    extern bool cluster_master_compute_merkle_root(const uint8_t *extranonce2, uint8_t extranonce2_len,
                                                    uint8_t *merkle_root_out);
    if (!cluster_master_compute_merkle_root(slave_work.extranonce2, slave_work.extranonce2_len,
                                             slave_work.merkle_root)) {
        ESP_LOGW(TAG, "Failed to compute merkle root for slave %d - work may be invalid", slave_id);
    }

    // Use compact 250-byte buffer for ESP-NOW (skips optional display fields)
    char payload[250];
    int len = cluster_protocol_encode_work(&slave_work, payload, sizeof(payload));

    if (len < 0) {
        ESP_LOGE(TAG, "Failed to encode work for slave %d", slave_id);
        return ESP_FAIL;
    }

    esp_err_t ret;

#if defined(CONFIG_CLUSTER_TRANSPORT_ESPNOW) || defined(CONFIG_CLUSTER_TRANSPORT_BOTH)
    // Always use ESP-NOW broadcast - unicast was unreliable
    // Slaves filter by target_slave_id in the message
    extern esp_err_t cluster_espnow_broadcast(const char *data, size_t len);
    ESP_LOGI(TAG, "Broadcasting work for slave %d (%d bytes, job %lu)",
             slave_id, len, (unsigned long)work->job_id);

    // Send multiple times to improve reliability for larger messages
    // ESP-NOW broadcast has no ACK, so we retry to increase delivery probability
    ret = ESP_FAIL;
    for (int attempt = 0; attempt < 3; attempt++) {
        ret = cluster_espnow_broadcast(payload, len);
        if (ret == ESP_OK) {
            if (attempt == 0) {
                ESP_LOGI(TAG, "ESP-NOW broadcast SUCCESS for slave %d", slave_id);
            } else {
                ESP_LOGI(TAG, "ESP-NOW broadcast SUCCESS for slave %d (attempt %d)", slave_id, attempt + 1);
            }
        } else {
            ESP_LOGW(TAG, "ESP-NOW broadcast attempt %d FAILED for slave %d: %s",
                     attempt + 1, slave_id, esp_err_to_name(ret));
        }
        // Small delay between retries
        vTaskDelay(pdMS_TO_TICKS(20));
    }
#else
    // Fall back to BAP UART broadcast
    extern esp_err_t BAP_uart_send_raw(const char *data, size_t len);
    ret = BAP_uart_send_raw(payload, len);
    ESP_LOGI(TAG, "Sent work via UART broadcast to slave %d (job %lu)",
             slave_id, (unsigned long)work->job_id);
#endif

    if (ret == ESP_OK) {
        slave->last_work_sent = esp_timer_get_time() / 1000;
        g_master->work_distributed++;
    } else {
        ESP_LOGW(TAG, "Failed to send work to slave %d: %s",
                 slave_id, esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief Distribute work to all active slaves
 */
esp_err_t cluster_master_distribute_work(const cluster_work_t *work)
{
    if (!g_master || !work) {
        return ESP_ERR_INVALID_ARG;
    }

    // Store current work
    xSemaphoreTake(g_master->work_mutex, portMAX_DELAY);
    memcpy(&g_master->current_work, work, sizeof(cluster_work_t));
    g_master->work_valid = true;
    xSemaphoreGive(g_master->work_mutex);

    // Recalculate nonce ranges if needed
    calculate_nonce_ranges();

    // 1. Identify active slaves and copy their IDs to a local list
    // We do this inside the lock, but we DON'T send inside the lock.
    // This prevents the HTTP server from hanging while we wait for the radio.
    uint8_t active_slaves[CLUSTER_MAX_SLAVES];
    uint8_t active_count = 0;

    xSemaphoreTake(g_master->slaves_mutex, portMAX_DELAY);
    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (g_master->slaves[i].state == SLAVE_STATE_ACTIVE) {
            active_slaves[active_count++] = i;
        }
    }
    xSemaphoreGive(g_master->slaves_mutex);

    // 2. Send to active slaves (Lock is RELEASED now)
    uint8_t sent_count = 0;
    for (int i = 0; i < active_count; i++) {
        uint8_t slave_id = active_slaves[i];
        if (send_work_to_slave(slave_id, work) == ESP_OK) {
            sent_count++;
        }
    }

    ESP_LOGI(TAG, "Distributed job %lu to %d slaves",
             (unsigned long)work->job_id, sent_count);

    return ESP_OK;
}

// ============================================================================
// Share Handling
// ============================================================================

/**
 * @brief Simple deduplication buffer for recent shares
 */
#define RECENT_SHARES_SIZE 32
static struct {
    uint32_t nonce;
    uint32_t job_id;
    uint8_t slave_id;
    bool valid;
} recent_shares[RECENT_SHARES_SIZE];
static int recent_shares_idx = 0;

static bool is_duplicate_share(const cluster_share_t *share)
{
    for (int i = 0; i < RECENT_SHARES_SIZE; i++) {
        if (recent_shares[i].valid &&
            recent_shares[i].nonce == share->nonce &&
            recent_shares[i].job_id == share->job_id &&
            recent_shares[i].slave_id == share->slave_id) {
            return true;
        }
    }
    return false;
}

static void record_share(const cluster_share_t *share)
{
    recent_shares[recent_shares_idx].nonce = share->nonce;
    recent_shares[recent_shares_idx].job_id = share->job_id;
    recent_shares[recent_shares_idx].slave_id = share->slave_id;
    recent_shares[recent_shares_idx].valid = true;
    recent_shares_idx = (recent_shares_idx + 1) % RECENT_SHARES_SIZE;
}

/**
 * @brief Receive and queue share from slave
 */
esp_err_t cluster_master_receive_share(const cluster_share_t *share)
{
    if (!g_master || !share) {
        return ESP_ERR_INVALID_ARG;
    }

    // Validate slave ID
    if (share->slave_id >= CLUSTER_MAX_SLAVES) {
        ESP_LOGW(TAG, "Share from invalid slave ID: %d", share->slave_id);
        return ESP_ERR_INVALID_ARG;
    }

    // Check for duplicate share
    if (is_duplicate_share(share)) {
        ESP_LOGD(TAG, "Ignoring duplicate share from slave %d (nonce 0x%08lX)",
                 share->slave_id, (unsigned long)share->nonce);
        return ESP_OK;  // Not an error, just ignore
    }

    // Record this share to prevent future duplicates
    record_share(share);

    // Update slave stats
    xSemaphoreTake(g_master->slaves_mutex, portMAX_DELAY);
    g_master->slaves[share->slave_id].shares_submitted++;
    g_master->slaves[share->slave_id].last_seen = esp_timer_get_time() / 1000;
    xSemaphoreGive(g_master->slaves_mutex);

    // Queue for submission to pool
    if (xQueueSend(g_master->share_queue, share, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Share queue full, dropping share from slave %d",
                 share->slave_id);
        return ESP_ERR_NO_MEM;
    }

    g_master->total_shares++;

    ESP_LOGI(TAG, "Received share from slave %d (job %lu, nonce 0x%08lX)",
             share->slave_id,
             (unsigned long)share->job_id,
             (unsigned long)share->nonce);

    return ESP_OK;
}

/**
 * @brief Task: Submit queued shares to stratum pool
 */
static void share_submitter_task(void *pvParameters)
{
    cluster_share_t share;

    ESP_LOGI(TAG, "Share submitter task started");

    while (1) {
        if (xQueueReceive(g_master->share_queue, &share, portMAX_DELAY) == pdTRUE) {
            // Submit to pool via existing stratum infrastructure
            // Pass slave_id so we can update the correct slave's counter when pool responds
            stratum_submit_share_from_cluster(share.job_id,
                                               share.nonce,
                                               share.extranonce2,
                                               share.extranonce2_len,
                                               share.ntime,
                                               share.version,
                                               share.slave_id);

            ESP_LOGD(TAG, "Submitted share from slave %d to pool", share.slave_id);
        }
    }
}

// ============================================================================
// Slave Management
// ============================================================================

/**
 * @brief Handle slave registration request (internal implementation)
 */
static esp_err_t handle_registration_internal(const char *hostname,
                                               const char *ip_addr,
                                               const uint8_t *mac_addr)
{
    if (!g_master || !hostname) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(g_master->slaves_mutex, portMAX_DELAY);

    // Find existing or free slot
    int slot = -1;
    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        // Check if already registered (by hostname or MAC)
        if (g_master->slaves[i].state != SLAVE_STATE_DISCONNECTED) {
            // Check by MAC first if available
            if (mac_addr && memcmp(g_master->slaves[i].mac_addr, mac_addr, 6) == 0) {
                slot = i;
                ESP_LOGI(TAG, "Slave '%s' reconnecting (MAC match) to slot %d", hostname, i);
                break;
            }
            // Fall back to hostname match
            if (strncmp(g_master->slaves[i].hostname, hostname, 31) == 0) {
                slot = i;
                ESP_LOGI(TAG, "Slave '%s' reconnecting to slot %d", hostname, i);
                break;
            }
        }
        // Find first free slot
        if (slot < 0 && g_master->slaves[i].state == SLAVE_STATE_DISCONNECTED) {
            slot = i;
        }
    }

    if (slot < 0) {
        xSemaphoreGive(g_master->slaves_mutex);
        ESP_LOGW(TAG, "No free slots for slave '%s'", hostname);
        return ESP_ERR_NO_MEM;
    }

    // Initialize slave entry
    cluster_slave_t *slave = &g_master->slaves[slot];
    slave->slave_id = slot;
    slave->state = SLAVE_STATE_ACTIVE;
    strncpy(slave->hostname, hostname, 31);
    slave->hostname[31] = '\0';

    // Store IP address if provided
    if (ip_addr && ip_addr[0] != '\0') {
        strncpy(slave->ip_addr, ip_addr, 15);
        slave->ip_addr[15] = '\0';
        ESP_LOGI(TAG, "Slave IP: %s", slave->ip_addr);
    } else {
        slave->ip_addr[0] = '\0';
    }

    // Store MAC address if provided (for ESP-NOW direct communication)
    if (mac_addr) {
        memcpy(slave->mac_addr, mac_addr, 6);
        ESP_LOGI(TAG, "Slave MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
    } else {
        memset(slave->mac_addr, 0, 6);
    }

    slave->last_heartbeat = esp_timer_get_time() / 1000;
    slave->last_seen = slave->last_heartbeat;
    slave->shares_submitted = 0;
    slave->shares_accepted = 0;
    slave->shares_rejected = 0;

    g_master->slave_count++;

    xSemaphoreGive(g_master->slaves_mutex);

    // Send acknowledgment with assigned ID
    char ack_payload[64];
    int len = cluster_protocol_encode_ack(slot, hostname, ack_payload, sizeof(ack_payload));

    if (len > 0) {
        // Send ACK directly to this slave via ESP-NOW if we have their MAC
#if defined(CONFIG_CLUSTER_TRANSPORT_ESPNOW) || defined(CONFIG_CLUSTER_TRANSPORT_BOTH)
        if (mac_addr) {
            extern esp_err_t cluster_espnow_send(const uint8_t *dest_mac, const char *data, size_t len);
            cluster_espnow_send(mac_addr, ack_payload, len);
        } else
#endif
        {
            extern esp_err_t BAP_uart_send_raw(const char *data, size_t len);
            BAP_uart_send_raw(ack_payload, len);
        }
    }

    ESP_LOGI(TAG, "Registered slave '%s' with ID %d (total: %d)",
             hostname, slot, g_master->slave_count);

    // Recalculate nonce ranges and send current work if available
    calculate_nonce_ranges();

    if (g_master->work_valid) {
        send_work_to_slave(slot, &g_master->current_work);
    }

    return ESP_OK;
}

/**
 * @brief Handle slave registration request
 */
esp_err_t cluster_master_handle_registration(const char *hostname,
                                              const char *ip_addr)
{
    return handle_registration_internal(hostname, ip_addr, NULL);
}

/**
 * @brief Handle slave registration with MAC address (for ESP-NOW)
 */
esp_err_t cluster_master_handle_registration_with_mac(const char *hostname,
                                                       const char *ip_addr,
                                                       const uint8_t *mac_addr)
{
    return handle_registration_internal(hostname, ip_addr, mac_addr);
}

/**
 * @brief Handle slave heartbeat with extended data
 */
esp_err_t cluster_master_handle_heartbeat_ex(const cluster_heartbeat_data_t *data)
{
    if (!g_master || !data || data->slave_id >= CLUSTER_MAX_SLAVES) {
        ESP_LOGW(TAG, "Invalid heartbeat: g_master=%p, data=%p, slave_id=%d",
                 g_master, data, data ? data->slave_id : -1);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Received heartbeat from slave %d: hashrate=%lu, temp=%.1f",
             data->slave_id, (unsigned long)data->hashrate, data->temp);

    xSemaphoreTake(g_master->slaves_mutex, portMAX_DELAY);

    cluster_slave_t *slave = &g_master->slaves[data->slave_id];

    if (slave->state == SLAVE_STATE_DISCONNECTED) {
        // Slave was disconnected but is sending heartbeats again - recover it
        ESP_LOGI(TAG, "Recovering disconnected slave %d via heartbeat", data->slave_id);
        slave->state = SLAVE_STATE_ACTIVE;
        g_master->slave_count++;
        // Note: nonce ranges will be recalculated on next work distribution
    }

    slave->last_heartbeat = esp_timer_get_time() / 1000;
    slave->last_seen = slave->last_heartbeat;
    slave->hashrate = data->hashrate;
    slave->temperature = data->temp;
    slave->fan_rpm = data->fan_rpm;
    // NOTE: Don't overwrite shares_submitted here - master tracks that when receiving shares from slave
    // data->shares is slave's LOCAL share count, not shares submitted TO master

    // Extended fields
    slave->frequency = data->frequency;
    slave->core_voltage = data->core_voltage;
    slave->power = data->power;
    slave->voltage_in = data->voltage_in;

    if (slave->state == SLAVE_STATE_STALE) {
        slave->state = SLAVE_STATE_ACTIVE;
        ESP_LOGI(TAG, "Slave %d recovered from stale state", data->slave_id);
    }

    xSemaphoreGive(g_master->slaves_mutex);

    // Send heartbeat response
    char response[64];
    int len = cluster_protocol_encode_heartbeat(data->slave_id, 0, 0, 0, 0, response, sizeof(response));
    if (len > 0) {
        extern esp_err_t BAP_uart_send_raw(const char *data, size_t len);
        BAP_uart_send_raw(response, len);
    }

    return ESP_OK;
}

/**
 * @brief Handle slave heartbeat (legacy, for backwards compatibility)
 */
esp_err_t cluster_master_handle_heartbeat(uint8_t slave_id,
                                           uint32_t hashrate,
                                           float temp,
                                           uint16_t fan_rpm)
{
    cluster_heartbeat_data_t data = {
        .slave_id = slave_id,
        .hashrate = hashrate,
        .temp = temp,
        .fan_rpm = fan_rpm,
        .shares = 0,
        .frequency = 0,
        .core_voltage = 0,
        .power = 0.0f,
        .voltage_in = 0.0f
    };
    return cluster_master_handle_heartbeat_ex(&data);
}

/**
 * @brief Update slave's MAC address (called from ESP-NOW layer when heartbeat received)
 *
 * This fixes cases where the slave's MAC was stored incorrectly during registration.
 * Since heartbeats come via ESP-NOW with the correct source MAC, we can use this
 * to ensure the stored MAC is correct for work distribution.
 */
void cluster_master_update_slave_mac(uint8_t slave_id, const uint8_t *mac)
{
    if (!g_master || slave_id >= CLUSTER_MAX_SLAVES || !mac) {
        return;
    }

    // Use short timeout to avoid blocking
    if (xSemaphoreTake(g_master->slaves_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    cluster_slave_t *slave = &g_master->slaves[slave_id];

    // Only update if slave exists and MAC is different
    if (slave->state != SLAVE_STATE_DISCONNECTED) {
        if (memcmp(slave->mac_addr, mac, 6) != 0) {
            ESP_LOGW(TAG, "Updating slave %d MAC: %02X:%02X:%02X:%02X:%02X:%02X -> %02X:%02X:%02X:%02X:%02X:%02X",
                     slave_id,
                     slave->mac_addr[0], slave->mac_addr[1], slave->mac_addr[2],
                     slave->mac_addr[3], slave->mac_addr[4], slave->mac_addr[5],
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            memcpy(slave->mac_addr, mac, 6);
        }
    }

    xSemaphoreGive(g_master->slaves_mutex);
}

/**
 * @brief Task: Monitor slave health and manage timeouts
 *
 * Also handles periodic work re-broadcast to ensure slaves receive work
 * even if some broadcasts are lost (ESP-NOW broadcast is not reliable).
 */
static void coordinator_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Coordinator task started");

    const int64_t WORK_REBROADCAST_INTERVAL_MS = 10000;  // Re-broadcast every 10 seconds

    while (1) {
        int64_t now = esp_timer_get_time() / 1000;
        uint32_t total_hashrate = 0;
        bool needs_recalc = false;  // Flag to defer calculate_nonce_ranges()

        // Collect list of slaves that need work re-broadcast
        uint8_t slaves_needing_work[CLUSTER_MAX_SLAVES];
        uint8_t work_rebroadcast_count = 0;

        xSemaphoreTake(g_master->slaves_mutex, portMAX_DELAY);

        for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
            cluster_slave_t *slave = &g_master->slaves[i];

            if (slave->state == SLAVE_STATE_DISCONNECTED) {
                continue;
            }

            int64_t elapsed = now - slave->last_heartbeat;

            if (elapsed > CLUSTER_TIMEOUT_MS) {
                // Slave has timed out
                ESP_LOGW(TAG, "Slave %d ('%s') timed out after %lld ms",
                         i, slave->hostname, elapsed);
                slave->state = SLAVE_STATE_DISCONNECTED;
                g_master->slave_count--;

                // Set flag - will recalculate after releasing mutex
                needs_recalc = true;
            }
            else if (elapsed > CLUSTER_HEARTBEAT_MS * 2) {
                // Slave is stale but not yet timed out
                if (slave->state == SLAVE_STATE_ACTIVE) {
                    ESP_LOGW(TAG, "Slave %d marked as stale", i);
                    slave->state = SLAVE_STATE_STALE;
                }
            }
            else if (slave->state == SLAVE_STATE_ACTIVE) {
                total_hashrate += slave->hashrate;

                // Check if this slave needs work re-broadcast
                // Uses last_work_sent which is updated by send_work_to_slave()
                // Re-broadcast if: work is valid AND enough time has passed since last send
                int64_t time_since_work = now - slave->last_work_sent;
                if (g_master->work_valid && time_since_work > WORK_REBROADCAST_INTERVAL_MS) {
                    slaves_needing_work[work_rebroadcast_count++] = i;
                }
            }
        }

        xSemaphoreGive(g_master->slaves_mutex);

        // Recalculate nonce ranges AFTER releasing mutex to avoid deadlock
        if (needs_recalc) {
            calculate_nonce_ranges();
        }

        // Re-broadcast work to slaves that need it (mutex released)
        // send_work_to_slave() updates last_work_sent, preventing repeated rebroadcasts
        for (int i = 0; i < work_rebroadcast_count; i++) {
            uint8_t slave_id = slaves_needing_work[i];
            ESP_LOGI(TAG, "Re-broadcasting work to slave %d (periodic refresh)", slave_id);
            send_work_to_slave(slave_id, &g_master->current_work);
            // Small delay between broadcasts to avoid flooding
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        // Add master's own hashrate AFTER releasing mutex to avoid potential deadlock
        extern uint32_t cluster_get_asic_hashrate(void);
        uint32_t master_hashrate = cluster_get_asic_hashrate();
        g_master->total_hashrate = total_hashrate + master_hashrate;

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============================================================================
// Statistics
// ============================================================================

void cluster_master_get_stats(cluster_stats_t *stats, uint8_t *active_slaves)
{
    if (!g_master) {
        if (stats) {
            memset(stats, 0, sizeof(cluster_stats_t));
        }
        if (active_slaves) *active_slaves = 0;
        return;
    }

    // Use timeout to avoid blocking HTTP server if mutex is held
    if (xSemaphoreTake(g_master->slaves_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "get_stats: mutex timeout");
        if (stats) memset(stats, 0, sizeof(cluster_stats_t));
        if (active_slaves) *active_slaves = 0;
        return;
    }

    if (stats) {
        stats->total_hashrate = g_master->total_hashrate;
        stats->total_shares = g_master->total_shares;

        // Calculate accepted/rejected from all slaves
        stats->total_shares_accepted = 0;
        stats->total_shares_rejected = 0;
        for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
            stats->total_shares_accepted += g_master->slaves[i].shares_accepted;
            stats->total_shares_rejected += g_master->slaves[i].shares_rejected;
        }
    }

    if (active_slaves) {
        uint8_t count = 0;
        for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
            if (g_master->slaves[i].state == SLAVE_STATE_ACTIVE) {
                count++;
            }
        }
        *active_slaves = count;
    }

    xSemaphoreGive(g_master->slaves_mutex);
}

esp_err_t cluster_master_get_slave(uint8_t slave_id, cluster_slave_t *slave)
{
    if (!g_master || !slave || slave_id >= CLUSTER_MAX_SLAVES) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(g_master->slaves_mutex, portMAX_DELAY);
    memcpy(slave, &g_master->slaves[slave_id], sizeof(cluster_slave_t));
    xSemaphoreGive(g_master->slaves_mutex);

    return ESP_OK;
}

esp_err_t cluster_master_get_slave_info(uint8_t slot_index, cluster_slave_t *slave)
{
    if (!g_master || !slave || slot_index >= CLUSTER_MAX_SLAVES) {
        return ESP_ERR_INVALID_ARG;
    }

    // Use timeout to avoid blocking HTTP server if mutex is held
    if (xSemaphoreTake(g_master->slaves_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "get_slave_info: mutex timeout");
        return ESP_ERR_TIMEOUT;
    }
    memcpy(slave, &g_master->slaves[slot_index], sizeof(cluster_slave_t));
    xSemaphoreGive(g_master->slaves_mutex);

    return ESP_OK;
}

/**
 * @brief Update slave's share counter when pool responds
 * Called from cluster_integration.c when pool accepts/rejects a share
 */
void cluster_master_update_slave_share_count(uint8_t slave_id, bool accepted)
{
    if (!g_master || slave_id >= CLUSTER_MAX_SLAVES) {
        return;
    }

    xSemaphoreTake(g_master->slaves_mutex, portMAX_DELAY);
    if (accepted) {
        g_master->slaves[slave_id].shares_accepted++;
        ESP_LOGI(TAG, "Slave %d shares_accepted now: %lu", slave_id,
                 (unsigned long)g_master->slaves[slave_id].shares_accepted);
    } else {
        g_master->slaves[slave_id].shares_rejected++;
        ESP_LOGW(TAG, "Slave %d shares_rejected now: %lu", slave_id,
                 (unsigned long)g_master->slaves[slave_id].shares_rejected);
    }
    xSemaphoreGive(g_master->slaves_mutex);
}

// ============================================================================
// Initialization
// ============================================================================

esp_err_t cluster_master_init(cluster_master_state_t *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }

    g_master = state;

    // Initialize synchronization primitives
    g_master->slaves_mutex = xSemaphoreCreateMutex();
    g_master->work_mutex = xSemaphoreCreateMutex();
    g_master->share_queue = xQueueCreate(CLUSTER_SHARE_QUEUE_SIZE,
                                          sizeof(cluster_share_t));

    if (!g_master->slaves_mutex || !g_master->work_mutex ||
        !g_master->share_queue) {
        ESP_LOGE(TAG, "Failed to create synchronization primitives");
        return ESP_ERR_NO_MEM;
    }

    // Clear slave array
    memset(g_master->slaves, 0, sizeof(g_master->slaves));
    g_master->slave_count = 0;
    g_master->work_valid = false;
    g_master->total_hashrate = 0;
    g_master->total_shares = 0;
    g_master->work_distributed = 0;

    // Create tasks
    xTaskCreate(coordinator_task, "cluster_coord", 4096, NULL, 5,
                &g_master->coordinator_task);
    xTaskCreate(share_submitter_task, "cluster_shares", 4096, NULL, 6,
                &g_master->share_submitter_task);

    g_master->initialized = true;

    ESP_LOGI(TAG, "Cluster master initialized");

    return ESP_OK;
}

void cluster_master_deinit(void)
{
    if (!g_master) return;

    // Stop tasks
    if (g_master->coordinator_task) {
        vTaskDelete(g_master->coordinator_task);
    }
    if (g_master->share_submitter_task) {
        vTaskDelete(g_master->share_submitter_task);
    }

    // Clean up resources
    if (g_master->slaves_mutex) {
        vSemaphoreDelete(g_master->slaves_mutex);
    }
    if (g_master->work_mutex) {
        vSemaphoreDelete(g_master->work_mutex);
    }
    if (g_master->share_queue) {
        vQueueDelete(g_master->share_queue);
    }

    g_master->initialized = false;
    g_master = NULL;

    ESP_LOGI(TAG, "Cluster master deinitialized");
}

#endif // CLUSTER_ENABLED && CLUSTER_IS_MASTER
