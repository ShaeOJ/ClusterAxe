#include <sys/time.h>
#include <limits.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "work_queue.h"
#include "global_state.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mining.h"
#include "string.h"
#include "stratum_task.h"

#include "asic.h"

// Clusteraxe integration
#include "cluster_config.h"
#if CLUSTER_ENABLED
#include "cluster_integration.h"
#include "cluster.h"
#endif

static const char *TAG = "create_jobs_task";

#define QUEUE_LOW_WATER_MARK 6 // Optimized for faster job dispatch
#define WORK_WAIT_TIMEOUT_MS 100 // Max time to wait for new work notification

static bool should_generate_more_work(GlobalState *GLOBAL_STATE);
static void generate_work_for_pool(GlobalState *GLOBAL_STATE, mining_notify *notification, uint64_t extranonce_2, uint32_t difficulty, uint8_t pool_id);

void create_jobs_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    uint32_t difficulty_primary = GLOBAL_STATE->pool_difficulty;
    uint32_t difficulty_secondary = GLOBAL_STATE->pool_difficulty_secondary;

    // Current notification being worked on for each pool
    mining_notify *current_notification_primary = NULL;
    mining_notify *current_notification_secondary = NULL;
    uint64_t extranonce_2_primary = 0;
    uint64_t extranonce_2_secondary = 0;

    while (1)
    {
#if CLUSTER_ENABLED && CLUSTER_IS_SLAVE
        // In slave mode, work comes from cluster master, not stratum
        // The cluster_slave module handles work reception and ASIC job creation
        if (cluster_slave_should_skip_stratum()) {
            // Wait for cluster work - the cluster_submit_work_to_asic() function
            // directly enqueues jobs to ASIC_jobs_queue when work arrives from master
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WORK_WAIT_TIMEOUT_MS));
            continue;
        }
#endif

        // Check for new difficulty messages
        if (GLOBAL_STATE->new_set_mining_difficulty_msg)
        {
            ESP_LOGI(TAG, "New primary pool difficulty %lu", GLOBAL_STATE->pool_difficulty);
            difficulty_primary = GLOBAL_STATE->pool_difficulty;
            GLOBAL_STATE->new_set_mining_difficulty_msg = false;
        }

        if (GLOBAL_STATE->new_set_mining_difficulty_msg_secondary)
        {
            ESP_LOGI(TAG, "New secondary pool difficulty %lu", GLOBAL_STATE->pool_difficulty_secondary);
            difficulty_secondary = GLOBAL_STATE->pool_difficulty_secondary;
            GLOBAL_STATE->new_set_mining_difficulty_msg_secondary = false;
        }

        if (GLOBAL_STATE->new_stratum_version_rolling_msg && GLOBAL_STATE->ASIC_initalized) {
            ESP_LOGI(TAG, "Set chip version rolls %i", (int)(GLOBAL_STATE->version_mask >> 13));
            ASIC_set_version_mask(GLOBAL_STATE, GLOBAL_STATE->version_mask);
            GLOBAL_STATE->new_stratum_version_rolling_msg = false;
        }

        // Handle abandon work
        if (GLOBAL_STATE->abandon_work == 1)
        {
            GLOBAL_STATE->abandon_work = 0;
            ASIC_jobs_queue_clear(&GLOBAL_STATE->ASIC_jobs_queue);
            xSemaphoreGive(GLOBAL_STATE->ASIC_TASK_MODULE.semaphore);

            // Clear current notifications
            if (current_notification_primary) {
                STRATUM_V1_free_mining_notify(current_notification_primary);
                current_notification_primary = NULL;
            }
            if (current_notification_secondary) {
                STRATUM_V1_free_mining_notify(current_notification_secondary);
                current_notification_secondary = NULL;
            }
            extranonce_2_primary = 0;
            extranonce_2_secondary = 0;
            continue;
        }

        // Check if we need more work
        if (!should_generate_more_work(GLOBAL_STATE))
        {
            // Wait for notification from stratum task or timeout
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
            continue;
        }

        // Check for new work from primary pool
        if (GLOBAL_STATE->stratum_queue.count > 0) {
            if (current_notification_primary) {
                STRATUM_V1_free_mining_notify(current_notification_primary);
            }
            current_notification_primary = (mining_notify *)queue_dequeue(&GLOBAL_STATE->stratum_queue);
            extranonce_2_primary = 0;
            if (current_notification_primary) {
                ESP_LOGI(TAG, "Primary work dequeued: %s", current_notification_primary->job_id);
            }
        }

        // Check for new work from secondary pool (only in dual pool mode)
        if (stratum_is_dual_pool_mode(GLOBAL_STATE) && GLOBAL_STATE->stratum_queue_secondary.count > 0) {
            if (current_notification_secondary) {
                STRATUM_V1_free_mining_notify(current_notification_secondary);
            }
            current_notification_secondary = (mining_notify *)queue_dequeue(&GLOBAL_STATE->stratum_queue_secondary);
            extranonce_2_secondary = 0;
            if (current_notification_secondary) {
                ESP_LOGI(TAG, "Secondary work dequeued: %s", current_notification_secondary->job_id);
            }
        }

        // Decide which pool to generate work for
        if (stratum_is_dual_pool_mode(GLOBAL_STATE) && stratum_is_secondary_connected(GLOBAL_STATE)) {
            // Dual pool mode - use dithering to decide
            int target_pool = stratum_get_next_pool(GLOBAL_STATE);

            if (target_pool == POOL_SECONDARY && current_notification_secondary != NULL) {
                // Generate work for secondary pool
                generate_work_for_pool(GLOBAL_STATE, current_notification_secondary, extranonce_2_secondary, difficulty_secondary, POOL_SECONDARY);
                extranonce_2_secondary++;
            } else if (current_notification_primary != NULL) {
                // Generate work for primary pool
                generate_work_for_pool(GLOBAL_STATE, current_notification_primary, extranonce_2_primary, difficulty_primary, POOL_PRIMARY);
                extranonce_2_primary++;
            } else {
                // No work available, wait for notification from stratum task
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WORK_WAIT_TIMEOUT_MS));
            }
        } else {
            // Single pool mode (or failover) - only use primary
            if (current_notification_primary != NULL) {
                generate_work_for_pool(GLOBAL_STATE, current_notification_primary, extranonce_2_primary, difficulty_primary, POOL_PRIMARY);
                extranonce_2_primary++;
            } else {
                // No work available, wait and try to get from queue
                mining_notify *notification = (mining_notify *)queue_dequeue(&GLOBAL_STATE->stratum_queue);
                if (notification != NULL) {
                    current_notification_primary = notification;
                    extranonce_2_primary = 0;
                    ESP_LOGI(TAG, "Work dequeued: %s", notification->job_id);
                } else {
                    // No work available, wait for notification from stratum task
                    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WORK_WAIT_TIMEOUT_MS));
                }
            }
        }
    }
}

static bool should_generate_more_work(GlobalState *GLOBAL_STATE)
{
    return GLOBAL_STATE->ASIC_jobs_queue.count < QUEUE_LOW_WATER_MARK;
}

static void generate_work_for_pool(GlobalState *GLOBAL_STATE, mining_notify *notification, uint64_t extranonce_2, uint32_t difficulty, uint8_t pool_id)
{
    // Select extranonce based on pool
    char *extranonce_str;
    int extranonce_2_len;
    uint32_t version_mask;

    if (pool_id == POOL_SECONDARY) {
        extranonce_str = GLOBAL_STATE->extranonce_str_secondary;
        extranonce_2_len = GLOBAL_STATE->extranonce_2_len_secondary;
        version_mask = GLOBAL_STATE->version_mask_secondary;

        // Fallback to primary if secondary not set
        if (extranonce_str == NULL || extranonce_2_len == 0) {
            ESP_LOGW(TAG, "Secondary extranonce not set, skipping");
            return;
        }
    } else {
        extranonce_str = GLOBAL_STATE->extranonce_str;
        extranonce_2_len = GLOBAL_STATE->extranonce_2_len;
        version_mask = GLOBAL_STATE->version_mask;
    }

    if (extranonce_str == NULL || extranonce_2_len == 0) {
        ESP_LOGW(TAG, "Extranonce not set for pool %d", pool_id);
        return;
    }

    char extranonce_2_str[extranonce_2_len * 2 + 1];
    extranonce_2_generate(extranonce_2, extranonce_2_len, extranonce_2_str);

    // Use 2.12.0 API - calculate_coinbase_tx_hash instead of construct_coinbase_tx
    uint8_t coinbase_tx_hash[32];
    calculate_coinbase_tx_hash(notification->coinbase_1, notification->coinbase_2, extranonce_str, extranonce_2_str, coinbase_tx_hash);

    uint8_t merkle_root[32];
    calculate_merkle_root_hash(coinbase_tx_hash, (uint8_t(*)[32])notification->merkle_branches, notification->n_merkle_branches, merkle_root);

    bm_job *queued_next_job = malloc(sizeof(bm_job));
    if (queued_next_job == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for queued_next_job");
        return;
    }

    // Use 2.12.0 API - construct_bm_job takes pointer parameter
    construct_bm_job(notification, merkle_root, version_mask, difficulty, queued_next_job);

    queued_next_job->extranonce2 = strdup(extranonce_2_str);
    if (!queued_next_job->extranonce2) {
        ESP_LOGE(TAG, "Failed to allocate memory for extranonce2");
        free(queued_next_job);
        return;
    }

    queued_next_job->jobid = strdup(notification->job_id);
    if (!queued_next_job->jobid) {
        ESP_LOGE(TAG, "Failed to allocate memory for jobid");
        free(queued_next_job->extranonce2);
        free(queued_next_job);
        return;
    }

    queued_next_job->version_mask = version_mask;
    queued_next_job->pool_id = pool_id;  // Tag with pool ID

#if CLUSTER_ENABLED && CLUSTER_IS_MASTER
    // In cluster master mode, limit nonce range to master's assigned slot
    // This prevents duplicate work with slaves who have their own ranges
    if (cluster_is_active()) {
        uint32_t nonce_start, nonce_end;
        cluster_master_get_local_nonce_range(&nonce_start, &nonce_end);
        queued_next_job->starting_nonce = nonce_start;
        // Note: The ASIC will naturally roll through nonces from starting_nonce
        // The range limitation is implicit - when slaves are added, ranges shrink
    }
#endif

    queue_enqueue(&GLOBAL_STATE->ASIC_jobs_queue, queued_next_job);
}
