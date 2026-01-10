/**
 * @file cluster_integration.c
 * @brief Clusteraxe Integration with ESP-Miner
 *
 * Bridges the cluster module with ESP-Miner's stratum and ASIC subsystems.
 *
 * @author Clusteraxe Project
 * @license GPL-3.0
 */

#include "cluster_integration.h"
#include "cluster.h"
#include "cluster_protocol.h"
#include "cluster_config.h"
#include "cluster_espnow.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "string.h"
#include "mining.h"
#include "nvs_config.h"
#include "power/power.h"
#include "power/vcore.h"
#include <inttypes.h>

#if CLUSTER_ENABLED

static const char *TAG = "cluster_integ";

// Global state reference for integration functions
static GlobalState *g_global_state = NULL;

// ============================================================================
// Common Integration Functions
// ============================================================================

#if defined(CONFIG_CLUSTER_TRANSPORT_ESPNOW) || defined(CONFIG_CLUSTER_TRANSPORT_BOTH)
/**
 * @brief Wrapper to adapt ESP-NOW callback to cluster message handler
 */
static void espnow_rx_wrapper(const char *msg_type, const char *payload, size_t len, const uint8_t *src_mac, void *ctx)
{
    (void)ctx;
    // Pass MAC to enhanced handler that can route ESP-NOW specific data
    extern esp_err_t cluster_handle_espnow_message(const char *msg_type, const char *payload, size_t len, const uint8_t *src_mac);
    cluster_handle_espnow_message(msg_type, payload, len, src_mac);
}
#endif

esp_err_t cluster_integration_init(GlobalState *GLOBAL_STATE)
{
    if (!GLOBAL_STATE) {
        ESP_LOGE(TAG, "GLOBAL_STATE is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    g_global_state = GLOBAL_STATE;

    // Initialize cluster subsystem with compile-time default mode
    esp_err_t ret = cluster_init(CLUSTER_MODE_DEFAULT);

#if defined(CONFIG_CLUSTER_TRANSPORT_ESPNOW) || defined(CONFIG_CLUSTER_TRANSPORT_BOTH)
    if (ret == ESP_OK && cluster_is_active()) {
        if (cluster_espnow_init() == ESP_OK) {
            cluster_espnow_set_rx_callback(espnow_rx_wrapper, NULL);
            ESP_LOGI(TAG, "ESP-NOW transport initialized");
            
            // Start discovery if we are master
            #if CLUSTER_IS_MASTER
            cluster_espnow_start_discovery();
            #endif
        } else {
            ESP_LOGE(TAG, "Failed to initialize ESP-NOW transport");
        }
    }
#endif

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Cluster integration initialized: %s",
                 CLUSTER_IS_MASTER ? "MASTER" : (CLUSTER_IS_SLAVE ? "SLAVE" : "DISABLED"));
    }

    return ret;
}

uint32_t cluster_get_asic_hashrate(void)
{
    if (!g_global_state) {
        return 0;
    }
    // Convert from GH/s to GH/s * 100 for cluster protocol
    return (uint32_t)(g_global_state->SYSTEM_MODULE.current_hashrate * 100);
}

float cluster_get_chip_temp(void)
{
    if (!g_global_state) {
        return 0.0f;
    }
    return g_global_state->POWER_MANAGEMENT_MODULE.chip_temp_avg;
}

uint16_t cluster_get_fan_rpm(void)
{
    if (!g_global_state) {
        return 0;
    }
    return g_global_state->POWER_MANAGEMENT_MODULE.fan_rpm;
}

const char* cluster_get_hostname(void)
{
    // Get hostname from NVS or use default
    static char hostname[32] = {0};

    if (hostname[0] == '\0') {
        char *stored = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
        if (stored) {
            strncpy(hostname, stored, sizeof(hostname) - 1);
            free(stored);
        } else {
            strncpy(hostname, "bitaxe", sizeof(hostname) - 1);
        }
    }

    return hostname;
}

const char* cluster_get_ip_addr(void)
{
    if (!g_global_state) {
        return "";
    }
    return g_global_state->SYSTEM_MODULE.ip_addr_str;
}

uint16_t cluster_get_asic_frequency(void)
{
    if (!g_global_state) {
        return 0;
    }
    // frequency_value is in MHz as float, convert to uint16_t
    return (uint16_t)g_global_state->POWER_MANAGEMENT_MODULE.frequency_value;
}

uint16_t cluster_get_core_voltage(void)
{
    if (!g_global_state) {
        return 0;
    }
    // Get actual core voltage in mV from VCORE module
    int16_t voltage_mv = VCORE_get_voltage_mv(g_global_state);
    return (voltage_mv > 0) ? (uint16_t)voltage_mv : 0;
}

float cluster_get_power(void)
{
    if (!g_global_state) {
        return 0.0f;
    }
    return g_global_state->POWER_MANAGEMENT_MODULE.power;
}

float cluster_get_voltage_in(void)
{
    if (!g_global_state) {
        return 0.0f;
    }
    // Get input voltage from power subsystem (returns mV, convert to V)
    return Power_get_input_voltage(g_global_state) / 1000.0f;
}

// ============================================================================
// Master Integration Functions
// ============================================================================

#if CLUSTER_IS_MASTER

// Forward declaration for job mapping storage
void cluster_master_store_job_mapping(uint32_t numeric_id, const char *job_id_str,
                                       const char *extranonce2, uint32_t ntime, uint32_t version);

// Store notification data needed for merkle root computation
static struct {
    char *coinbase_1;
    char *coinbase_2;
    char *extranonce_str;
    int extranonce_2_len;
    uint8_t (*merkle_branches)[32];
    int n_merkle_branches;
    bool valid;
} g_stored_notify = {0};

/**
 * @brief Convert hex string to bytes
 */
static void hex_to_bytes(const char *hex, uint8_t *bytes, size_t len)
{
    for (size_t i = 0; i < len && hex[i*2] && hex[i*2+1]; i++) {
        char byte_str[3] = {hex[i*2], hex[i*2+1], '\0'};
        bytes[i] = (uint8_t)strtol(byte_str, NULL, 16);
    }
}

/**
 * @brief Store notification data for later merkle root computation
 */
static void store_notify_data(const mining_notify *notification,
                               const char *extranonce_str,
                               int extranonce_2_len)
{
    // Free old data
    if (g_stored_notify.coinbase_1) free(g_stored_notify.coinbase_1);
    if (g_stored_notify.coinbase_2) free(g_stored_notify.coinbase_2);
    if (g_stored_notify.extranonce_str) free(g_stored_notify.extranonce_str);
    if (g_stored_notify.merkle_branches) free(g_stored_notify.merkle_branches);

    // Store new data
    g_stored_notify.coinbase_1 = notification->coinbase_1 ? strdup(notification->coinbase_1) : NULL;
    g_stored_notify.coinbase_2 = notification->coinbase_2 ? strdup(notification->coinbase_2) : NULL;
    g_stored_notify.extranonce_str = extranonce_str ? strdup(extranonce_str) : NULL;
    g_stored_notify.extranonce_2_len = extranonce_2_len;

    // Copy merkle branches
    if (notification->n_merkle_branches > 0 && notification->merkle_branches) {
        g_stored_notify.merkle_branches = malloc(notification->n_merkle_branches * 32);
        if (g_stored_notify.merkle_branches) {
            memcpy(g_stored_notify.merkle_branches, notification->merkle_branches,
                   notification->n_merkle_branches * 32);
        }
        g_stored_notify.n_merkle_branches = notification->n_merkle_branches;
    } else {
        g_stored_notify.merkle_branches = NULL;
        g_stored_notify.n_merkle_branches = 0;
    }

    g_stored_notify.valid = true;
    ESP_LOGD(TAG, "Stored notify data: cb1=%s, branches=%d",
             g_stored_notify.coinbase_1 ? "yes" : "no", g_stored_notify.n_merkle_branches);
}

/**
 * @brief Compute merkle root for a specific extranonce2
 * This allows each slave to have a unique merkle root based on their extranonce2
 */
bool cluster_master_compute_merkle_root(const uint8_t *extranonce2, uint8_t extranonce2_len,
                                         uint8_t *merkle_root_out)
{
    if (!g_stored_notify.valid || !g_stored_notify.coinbase_1 || !g_stored_notify.coinbase_2) {
        ESP_LOGW(TAG, "No stored notify data for merkle computation");
        return false;
    }

    // Convert extranonce2 bytes to hex string
    char extranonce2_str[extranonce2_len * 2 + 1];
    for (int i = 0; i < extranonce2_len; i++) {
        sprintf(extranonce2_str + i * 2, "%02x", extranonce2[i]);
    }
    extranonce2_str[extranonce2_len * 2] = '\0';

    // Calculate coinbase hash and merkle root using mining.h functions
    // (mining.h is already included via mining.h)
    uint8_t coinbase_hash[32];
    calculate_coinbase_tx_hash(g_stored_notify.coinbase_1, g_stored_notify.coinbase_2,
                               g_stored_notify.extranonce_str, extranonce2_str, coinbase_hash);

    calculate_merkle_root_hash(coinbase_hash, (const uint8_t (*)[32])g_stored_notify.merkle_branches,
                               g_stored_notify.n_merkle_branches, merkle_root_out);

    return true;
}

void cluster_master_on_mining_notify(GlobalState *GLOBAL_STATE,
                                      const mining_notify *notification,
                                      const char *extranonce_str,
                                      int extranonce_2_len)
{
    if (!notification || !cluster_is_active()) {
        return;
    }

    ESP_LOGI(TAG, "Converting mining.notify to cluster work: job=%s", notification->job_id);

    // Store notification data for merkle root computation when distributing to slaves
    store_notify_data(notification, extranonce_str, extranonce_2_len);

    cluster_work_t work = {0};

    // Convert job_id to numeric (use hash of string if not numeric)
    work.job_id = strtoul(notification->job_id, NULL, 16);
    if (work.job_id == 0) {
        // Use simple hash of job_id string
        for (const char *p = notification->job_id; *p; p++) {
            work.job_id = work.job_id * 31 + *p;
        }
    }

    // Store job mapping for share submission later
    cluster_master_store_job_mapping(work.job_id, notification->job_id,
                                      "", notification->ntime, notification->version);
    ESP_LOGD(TAG, "Stored job mapping: %08lx -> %s", (unsigned long)work.job_id, notification->job_id);

    // Convert prev_block_hash from hex string
    if (notification->prev_block_hash) {
        hex_to_bytes(notification->prev_block_hash, work.prev_block_hash, 32);
    }

    // Store version, version_mask, target (nbits), and ntime
    work.version = notification->version;
    work.version_mask = g_global_state->version_mask;  // Pass version rolling mask to slave
    work.nbits = notification->target;
    work.ntime = notification->ntime;

    // Set pool difficulty so slave knows minimum share requirement
    work.pool_diff = g_global_state->pool_difficulty;
    ESP_LOGI(TAG, "Work pool_diff set to: %lu", (unsigned long)work.pool_diff);

    // Extranonce2 length (will be assigned per-slave in distribution)
    work.extranonce2_len = (uint8_t)extranonce_2_len;

    // Mark as clean job if stratum says to abandon work
    // Note: This would need to be passed from stratum_task
    work.clean_jobs = false;

    work.timestamp = esp_timer_get_time() / 1000;

    // Display info for slave UI (currently not transmitted due to ESP-NOW size limits)
    // Pool difficulty is transmitted and displayed on slaves
    work.block_height = g_global_state->block_height;
    if (g_global_state->scriptsig) {
        strncpy(work.scriptsig, g_global_state->scriptsig, sizeof(work.scriptsig) - 1);
    }
    strncpy(work.network_diff_str, g_global_state->network_diff_string, sizeof(work.network_diff_str) - 1);

    // Calculate merkle root placeholder - slaves will compute this themselves
    // For now, we'll pass the first merkle branch or zeros
    if (notification->merkle_branches && notification->n_merkle_branches > 0) {
        memcpy(work.merkle_root, notification->merkle_branches, 32);
    }

    // Distribute work to all connected slaves
    cluster_master_distribute_work(&work);
}

// Store mapping of job_id to job string for share submission
// Increased from 128 to 256 to prevent job mapping loss with multiple slaves
#define MAX_JOB_MAPPINGS 256
static struct {
    uint32_t numeric_id;
    char job_id_str[32];
    char extranonce2_str[32];
    uint32_t ntime;
    uint32_t version;
    bool valid;
} job_mappings[MAX_JOB_MAPPINGS];
static int job_mapping_index = 0;

// Track pending cluster shares to update slave counters when pool responds
#define MAX_PENDING_SHARES 64
static struct {
    int send_uid;       // Stratum message ID
    uint8_t slave_id;   // Which slave sent this share
    bool valid;
} pending_cluster_shares[MAX_PENDING_SHARES];
static int pending_share_index = 0;

void cluster_master_store_job_mapping(uint32_t numeric_id, const char *job_id_str,
                                       const char *extranonce2, uint32_t ntime, uint32_t version)
{
    int idx = job_mapping_index % MAX_JOB_MAPPINGS;
    job_mappings[idx].numeric_id = numeric_id;
    strncpy(job_mappings[idx].job_id_str, job_id_str, sizeof(job_mappings[idx].job_id_str) - 1);
    strncpy(job_mappings[idx].extranonce2_str, extranonce2, sizeof(job_mappings[idx].extranonce2_str) - 1);
    job_mappings[idx].ntime = ntime;
    job_mappings[idx].version = version;
    job_mappings[idx].valid = true;
    job_mapping_index++;
}

static bool cluster_master_find_job_mapping(uint32_t numeric_id, char *job_id_str, size_t max_len)
{
    for (int i = 0; i < MAX_JOB_MAPPINGS; i++) {
        if (job_mappings[i].valid && job_mappings[i].numeric_id == numeric_id) {
            strncpy(job_id_str, job_mappings[i].job_id_str, max_len - 1);
            return true;
        }
    }
    return false;
}

void stratum_submit_share_from_cluster(uint32_t job_id, uint32_t nonce,
                                        uint8_t *extranonce2, uint8_t en2_len,
                                        uint32_t ntime, uint32_t version,
                                        uint8_t slave_id)
{
    if (!g_global_state) {
        ESP_LOGE(TAG, "Global state not available for share submission");
        return;
    }

    // Look up the original job_id string
    char job_id_str[32];
    if (!cluster_master_find_job_mapping(job_id, job_id_str, sizeof(job_id_str))) {
        // Fallback: convert numeric ID to hex string
        snprintf(job_id_str, sizeof(job_id_str), "%08" PRIx32, job_id);
        ESP_LOGE(TAG, "JOB MAPPING NOT FOUND! job_id=%08" PRIx32 " - share will likely be REJECTED by pool", job_id);
    }

    // Convert extranonce2 bytes to hex string
    char extranonce2_str[en2_len * 2 + 1];
    for (int i = 0; i < en2_len; i++) {
        sprintf(extranonce2_str + i * 2, "%02x", extranonce2[i]);
    }
    extranonce2_str[en2_len * 2] = '\0';

    ESP_LOGI(TAG, "Submitting cluster share: job=%s, nonce=%08" PRIx32 ", en2=%s, ver=%08" PRIx32 ", slave=%d",
             job_id_str, nonce, extranonce2_str, version, slave_id);

    // Get socket and credentials for primary pool
    int sock = g_global_state->sock;
    int send_uid = g_global_state->send_uid++;
    char *user = g_global_state->SYSTEM_MODULE.pool_user;

    // Track this pending share so we can update slave stats when pool responds
    int idx = pending_share_index % MAX_PENDING_SHARES;
    pending_cluster_shares[idx].send_uid = send_uid;
    pending_cluster_shares[idx].slave_id = slave_id;
    pending_cluster_shares[idx].valid = true;
    pending_share_index++;

    // Version bits come from slave already in correct format (rolled_version ^ base_version)
    // Do NOT XOR again - pass directly to stratum
    uint32_t version_bits = version;

    // Stamp transmission time for response tracking
    extern void STRATUM_V1_stamp_tx(int request_id);
    STRATUM_V1_stamp_tx(send_uid);

    // Submit share to pool
    extern int STRATUM_V1_submit_share(int socket, int send_uid, const char *username,
                                        const char *job_id, const char *extranonce_2,
                                        const uint32_t ntime, const uint32_t nonce,
                                        const uint32_t version_bits);

    int ret = STRATUM_V1_submit_share(sock, send_uid, user, job_id_str,
                                       extranonce2_str, ntime, nonce, version_bits);

    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to submit cluster share: %d", ret);
    }
}

void cluster_master_get_local_nonce_range(uint32_t *nonce_start, uint32_t *nonce_end)
{
    // Master gets slot 0 (first portion of nonce space)
    // This is calculated based on total active nodes (master + slaves)
    uint32_t active_slaves = 0;
    cluster_master_get_stats(NULL, (uint8_t*)&active_slaves);

    uint32_t total_nodes = 1 + active_slaves;
    uint32_t range_size = 0xFFFFFFFF / total_nodes;

    if (nonce_start) *nonce_start = 0;
    if (nonce_end) *nonce_end = range_size - 1;
}

/**
 * @brief Called when pool responds to a share submission
 * Updates the slave's shares_accepted/shares_rejected counter
 */
void cluster_notify_share_result(int message_id, bool accepted)
{
    // Look up which slave this share belongs to
    for (int i = 0; i < MAX_PENDING_SHARES; i++) {
        if (pending_cluster_shares[i].valid && pending_cluster_shares[i].send_uid == message_id) {
            uint8_t slave_id = pending_cluster_shares[i].slave_id;
            pending_cluster_shares[i].valid = false;  // Clear this entry

            // Update slave counters
            cluster_slave_t slave;
            if (cluster_master_get_slave(slave_id, &slave) == ESP_OK) {
                // Update the slave's counter in master state
                extern void cluster_master_update_slave_share_count(uint8_t slave_id, bool accepted);
                cluster_master_update_slave_share_count(slave_id, accepted);

                ESP_LOGI(TAG, "Cluster share from slave %d %s", slave_id,
                         accepted ? "ACCEPTED" : "REJECTED");
            }
            return;
        }
    }
    // Not a cluster share, or already processed - that's fine
}

#endif // CLUSTER_IS_MASTER

// ============================================================================
// Slave Integration Functions
// ============================================================================

#if CLUSTER_IS_SLAVE

void cluster_slave_submit_to_asic(GlobalState *GLOBAL_STATE,
                                   const cluster_work_t *work)
{
    if (!GLOBAL_STATE || !work) {
        return;
    }

    ESP_LOGI(TAG, "Converting cluster work to ASIC job: job=%lu, nonce=0x%08lX-0x%08lX",
             (unsigned long)work->job_id,
             (unsigned long)work->nonce_start,
             (unsigned long)work->nonce_end);

    // Create a bm_job from cluster work
    bm_job *job = malloc(sizeof(bm_job));
    if (!job) {
        ESP_LOGE(TAG, "Failed to allocate job");
        return;
    }
    memset(job, 0, sizeof(bm_job));

    // Import byte manipulation functions from mining.c
    extern void reverse_32bit_words(const uint8_t *src, uint8_t *dest);
    extern void reverse_endianness_per_word(uint8_t *data);
    extern void midstate_sha256_bin(const uint8_t *data, size_t len, uint8_t *midstate);

    // Process merkle root - reverse 32-bit words for ASIC
    reverse_32bit_words(work->merkle_root, job->merkle_root);

    // Process prev_block_hash - already in bytes, just need proper formatting
    uint8_t prev_hash_work[32];
    memcpy(prev_hash_work, work->prev_block_hash, 32);
    reverse_endianness_per_word(prev_hash_work);
    reverse_32bit_words(prev_hash_work, job->prev_block_hash);

    job->version = work->version;
    job->target = work->nbits;
    job->ntime = work->ntime;
    // Use pool difficulty from master's work, not slave's local settings
    job->pool_diff = work->pool_diff;
    ESP_LOGI(TAG, "Job pool_diff set to: %lu", (unsigned long)job->pool_diff);

    // Compute midstate hash - this is critical for the ASIC to mine correctly
    // The midstate is computed from: version (4) + prev_block_hash (32) + merkle_root (28) = 64 bytes
    uint8_t midstate_data[64];
    memcpy(midstate_data, &job->version, 4);           // version (4 bytes)
    memcpy(midstate_data + 4, prev_hash_work, 32);     // prev_block_hash (32 bytes)
    memcpy(midstate_data + 36, work->merkle_root, 28); // first 28 bytes of merkle_root

    uint8_t midstate[32];
    midstate_sha256_bin(midstate_data, 64, midstate);
    reverse_32bit_words(midstate, job->midstate);

    ESP_LOGD(TAG, "Job version=0x%08lX, version_mask=0x%08lX",
             (unsigned long)job->version, (unsigned long)work->version_mask);

    // Compute all 4 midstates when version rolling is enabled (matching construct_bm_job)
    // Use version_mask from master's work, not slave's local settings
    uint32_t version_mask = work->version_mask;
    if (version_mask != 0) {
        extern uint32_t increment_bitmask(uint32_t value, uint32_t mask);

        // midstate1 - first rolled version
        uint32_t rolled_version = increment_bitmask(job->version, version_mask);
        memcpy(midstate_data, &rolled_version, 4);
        midstate_sha256_bin(midstate_data, 64, midstate);
        reverse_32bit_words(midstate, job->midstate1);

        // midstate2 - second rolled version
        rolled_version = increment_bitmask(rolled_version, version_mask);
        memcpy(midstate_data, &rolled_version, 4);
        midstate_sha256_bin(midstate_data, 64, midstate);
        reverse_32bit_words(midstate, job->midstate2);

        // midstate3 - third rolled version
        rolled_version = increment_bitmask(rolled_version, version_mask);
        memcpy(midstate_data, &rolled_version, 4);
        midstate_sha256_bin(midstate_data, 64, midstate);
        reverse_32bit_words(midstate, job->midstate3);

        job->num_midstates = 4;
    } else {
        job->num_midstates = 1;
    }

    // Set nonce range for this slave
    job->starting_nonce = work->nonce_start;
    job->version_mask = version_mask;

    // Create job ID string
    char job_id_str[16];
    snprintf(job_id_str, sizeof(job_id_str), "%08lx", (unsigned long)work->job_id);
    job->jobid = strdup(job_id_str);

    // Create extranonce2 string
    char en2_str[17];
    for (int i = 0; i < work->extranonce2_len; i++) {
        sprintf(en2_str + i * 2, "%02x", work->extranonce2[i]);
    }
    en2_str[work->extranonce2_len * 2] = '\0';
    job->extranonce2 = strdup(en2_str);

    // Set pool ID to indicate cluster source
    job->pool_id = 0xFF;  // Special ID for cluster work

    // Enqueue the job
    queue_enqueue(&GLOBAL_STATE->ASIC_jobs_queue, job);

    // Notify the ASIC task
    if (GLOBAL_STATE->ASIC_TASK_MODULE.semaphore) {
        xSemaphoreGive(GLOBAL_STATE->ASIC_TASK_MODULE.semaphore);
    }

    // Update display info from master's work
    if (work->block_height > 0) {
        GLOBAL_STATE->block_height = work->block_height;
    }
    if (work->scriptsig[0]) {
        // Free old scriptsig if exists and allocate new one
        if (GLOBAL_STATE->scriptsig) {
            free(GLOBAL_STATE->scriptsig);
        }
        GLOBAL_STATE->scriptsig = strdup(work->scriptsig);
    }
    if (work->network_diff_str[0]) {
        strncpy(GLOBAL_STATE->network_diff_string, work->network_diff_str,
                sizeof(GLOBAL_STATE->network_diff_string) - 1);
    }
}

void cluster_slave_intercept_share(GlobalState *GLOBAL_STATE,
                                    uint8_t job_id,
                                    uint32_t nonce,
                                    uint32_t ntime,
                                    uint32_t version,
                                    const char *extranonce2)
{
    if (!cluster_is_active() || cluster_get_mode() != CLUSTER_MODE_SLAVE) {
        return;
    }

    ESP_LOGI(TAG, "Intercepting share for cluster: job=%d, nonce=0x%08lx, ver=0x%08lx",
             job_id, (unsigned long)nonce, (unsigned long)version);

    // Get the active job to retrieve the numeric job ID
    bm_job *active_job = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id];
    if (!active_job) {
        ESP_LOGW(TAG, "No active job found for ID %d", job_id);
        return;
    }

    // Convert job ID string to numeric
    uint32_t numeric_job_id = strtoul(active_job->jobid, NULL, 16);

    // Get the extranonce2 from the job struct - this is the CORRECT one
    // that was used when this specific job was submitted to the ASIC
    const char *job_en2 = active_job->extranonce2 ? active_job->extranonce2 : "";

    // Route to cluster slave share handler with actual ASIC version bits
    // Pass the extranonce2 from the job so we use the correct one
    cluster_slave_on_share_found(nonce, numeric_job_id, version, ntime, job_en2);
}

bool cluster_slave_should_skip_stratum(void)
{
    return cluster_is_active() && cluster_get_mode() == CLUSTER_MODE_SLAVE;
}

// Implementation of the weak function declared in cluster_slave.c
void cluster_submit_work_to_asic(const cluster_work_t *work)
{
    if (!g_global_state) {
        ESP_LOGE(TAG, "ERROR: g_global_state is NULL - cluster_integration_init not called?");
        return;
    }
    if (!work) {
        ESP_LOGE(TAG, "ERROR: work is NULL");
        return;
    }
    ESP_LOGD(TAG, "Submitting work to ASIC: job=%lu", (unsigned long)work->job_id);
    cluster_slave_submit_to_asic(g_global_state, work);
}

#endif // CLUSTER_IS_SLAVE

// ============================================================================
// WiFi Event Handling
// ============================================================================

void cluster_on_wifi_reconnect(void)
{
#if defined(CONFIG_CLUSTER_TRANSPORT_ESPNOW) || defined(CONFIG_CLUSTER_TRANSPORT_BOTH)
    if (cluster_espnow_is_initialized()) {
        cluster_espnow_on_wifi_reconnect();
        ESP_LOGI(TAG, "Cluster notified of WiFi reconnection");
    }
#endif
}

GlobalState* cluster_get_global_state(void)
{
    return g_global_state;
}

#endif // CLUSTER_ENABLED
