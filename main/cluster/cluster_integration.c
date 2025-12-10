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
#include "esp_log.h"
#include "esp_timer.h"
#include "string.h"
#include "mining.h"
#include "nvs_config.h"
#include "power/power.h"

#if CLUSTER_ENABLED

static const char *TAG = "cluster_integ";

// Global state reference for integration functions
static GlobalState *g_global_state = NULL;

// ============================================================================
// Common Integration Functions
// ============================================================================

esp_err_t cluster_integration_init(GlobalState *GLOBAL_STATE)
{
    if (!GLOBAL_STATE) {
        return ESP_ERR_INVALID_ARG;
    }

    g_global_state = GLOBAL_STATE;

    // Initialize cluster subsystem with compile-time default mode
    esp_err_t ret = cluster_init(CLUSTER_MODE_DEFAULT);

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
    // voltage is in Volts, convert to mV
    return (uint16_t)(g_global_state->POWER_MANAGEMENT_MODULE.voltage * 1000);
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

void cluster_master_on_mining_notify(GlobalState *GLOBAL_STATE,
                                      const mining_notify *notification,
                                      const char *extranonce_str,
                                      int extranonce_2_len)
{
    if (!notification || !cluster_is_active()) {
        return;
    }

    ESP_LOGI(TAG, "Converting mining.notify to cluster work: job=%s", notification->job_id);

    cluster_work_t work = {0};

    // Convert job_id to numeric (use hash of string if not numeric)
    work.job_id = strtoul(notification->job_id, NULL, 16);
    if (work.job_id == 0) {
        // Use simple hash of job_id string
        for (const char *p = notification->job_id; *p; p++) {
            work.job_id = work.job_id * 31 + *p;
        }
    }

    // Convert prev_block_hash from hex string
    if (notification->prev_block_hash) {
        hex_to_bytes(notification->prev_block_hash, work.prev_block_hash, 32);
    }

    // Store version, target (nbits), and ntime
    work.version = notification->version;
    work.nbits = notification->target;
    work.ntime = notification->ntime;

    // Extranonce2 length (will be assigned per-slave in distribution)
    work.extranonce2_len = (uint8_t)extranonce_2_len;

    // Mark as clean job if stratum says to abandon work
    // Note: This would need to be passed from stratum_task
    work.clean_jobs = false;

    work.timestamp = esp_timer_get_time() / 1000;

    // Calculate merkle root placeholder - slaves will compute this themselves
    // For now, we'll pass the first merkle branch or zeros
    if (notification->merkle_branches && notification->n_merkle_branches > 0) {
        memcpy(work.merkle_root, notification->merkle_branches, 32);
    }

    // Distribute work to all connected slaves
    cluster_master_distribute_work(&work);
}

// Store mapping of job_id to job string for share submission
#define MAX_JOB_MAPPINGS 128
static struct {
    uint32_t numeric_id;
    char job_id_str[32];
    char extranonce2_str[32];
    uint32_t ntime;
    uint32_t version;
    bool valid;
} job_mappings[MAX_JOB_MAPPINGS];
static int job_mapping_index = 0;

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
                                        uint32_t ntime, uint32_t version)
{
    if (!g_global_state) {
        ESP_LOGE(TAG, "Global state not available for share submission");
        return;
    }

    // Look up the original job_id string
    char job_id_str[32];
    if (!cluster_master_find_job_mapping(job_id, job_id_str, sizeof(job_id_str))) {
        // Fallback: convert numeric ID to hex string
        snprintf(job_id_str, sizeof(job_id_str), "%08x", job_id);
        ESP_LOGW(TAG, "Job mapping not found, using numeric: %s", job_id_str);
    }

    // Convert extranonce2 bytes to hex string
    char extranonce2_str[en2_len * 2 + 1];
    for (int i = 0; i < en2_len; i++) {
        sprintf(extranonce2_str + i * 2, "%02x", extranonce2[i]);
    }
    extranonce2_str[en2_len * 2] = '\0';

    ESP_LOGI(TAG, "Submitting cluster share: job=%s, nonce=%08x, en2=%s",
             job_id_str, nonce, extranonce2_str);

    // Get socket and credentials for primary pool
    int sock = g_global_state->sock;
    int send_uid = g_global_state->send_uid++;
    char *user = g_global_state->SYSTEM_MODULE.pool_user;

    // Calculate version bits (version XOR with rolled version if any)
    uint32_t version_bits = version ^ g_global_state->version_mask;

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

    // Copy block header components
    memcpy(job->merkle_root, work->merkle_root, 32);
    memcpy(job->prev_block_hash, work->prev_block_hash, 32);
    job->version = work->version;
    job->target = work->nbits;
    job->ntime = work->ntime;
    job->pool_diff = GLOBAL_STATE->pool_difficulty;

    // Set nonce range for this slave
    job->starting_nonce = work->nonce_start;

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

    // Calculate midstate for ASIC
    // Note: This would need proper midstate calculation based on merkle root and version
    // For now, we'll let the ASIC driver handle it

    // Enqueue the job
    queue_enqueue(&GLOBAL_STATE->ASIC_jobs_queue, job);

    // Notify the ASIC task
    if (GLOBAL_STATE->ASIC_TASK_MODULE.semaphore) {
        xSemaphoreGive(GLOBAL_STATE->ASIC_TASK_MODULE.semaphore);
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

    ESP_LOGI(TAG, "Intercepting share for cluster: job=%d, nonce=0x%08lx",
             job_id, (unsigned long)nonce);

    // Get the active job to retrieve the numeric job ID
    bm_job *active_job = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id];
    if (!active_job) {
        ESP_LOGW(TAG, "No active job found for ID %d", job_id);
        return;
    }

    // Convert job ID string to numeric
    uint32_t numeric_job_id = strtoul(active_job->jobid, NULL, 16);

    // Route to cluster slave share handler
    cluster_slave_on_share_found(nonce, numeric_job_id);
}

bool cluster_slave_should_skip_stratum(void)
{
    return cluster_is_active() && cluster_get_mode() == CLUSTER_MODE_SLAVE;
}

// Implementation of the weak function declared in cluster_slave.c
void cluster_submit_work_to_asic(const cluster_work_t *work)
{
    if (g_global_state && work) {
        cluster_slave_submit_to_asic(g_global_state, work);
    }
}

#endif // CLUSTER_IS_SLAVE

#endif // CLUSTER_ENABLED
