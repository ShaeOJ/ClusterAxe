#include <lwip/tcpip.h>

#include "system.h"
#include "work_queue.h"
#include "serial.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_config.h"
#include "utils.h"
#include "stratum_task.h"
#include "stratum_api.h"
#include "hashrate_monitor_task.h"
#include "asic.h"

// Clusteraxe integration
#include "cluster_config.h"
#if CLUSTER_ENABLED && CLUSTER_IS_SLAVE
#include "cluster_integration.h"
#include "cluster.h"
#endif

static const char *TAG = "asic_result";

void ASIC_result_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    while (1)
    {
        // Check if ASIC is initialized before trying to process work
        if (!GLOBAL_STATE->ASIC_initalized) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        //task_result *asic_result = (*GLOBAL_STATE->ASIC_functions.receive_result_fn)(GLOBAL_STATE);
        task_result *asic_result = ASIC_process_work(GLOBAL_STATE);

        if (asic_result == NULL)
        {
            continue;
        }

        if (asic_result->register_type != REGISTER_INVALID) {
            hashrate_monitor_register_read(GLOBAL_STATE, asic_result->register_type, asic_result->asic_nr, asic_result->value);
            continue;
        }

        uint8_t job_id = asic_result->job_id;

        if (GLOBAL_STATE->valid_jobs[job_id] == 0)
        {
            ESP_LOGW(TAG, "Invalid job nonce found, 0x%02X", job_id);
            continue;
        }

        bm_job *active_job = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id];
        // check the nonce difficulty
        double nonce_diff = test_nonce_value(active_job, asic_result->nonce, asic_result->rolled_version);

        //log the ASIC response
        ESP_LOGI(TAG, "ID: %s, ASIC nr: %d, ver: %08" PRIX32 " Nonce %08" PRIX32 " diff %.1f of %ld.", active_job->jobid, asic_result->asic_nr, asic_result->rolled_version, asic_result->nonce, nonce_diff, active_job->pool_diff);

        if (nonce_diff >= active_job->pool_diff)
        {
#if CLUSTER_ENABLED && CLUSTER_IS_SLAVE
            // In slave mode, route shares to cluster master instead of pool
            if (cluster_slave_should_skip_stratum()) {
                // Intercept and send to master via cluster protocol
                cluster_slave_intercept_share(GLOBAL_STATE,
                                               job_id,
                                               asic_result->nonce,
                                               active_job->ntime,
                                               asic_result->rolled_version ^ active_job->version,
                                               active_job->extranonce2);
                ESP_LOGI(TAG, "Share routed to cluster master: job=%s, nonce=0x%08lX",
                         active_job->jobid, (unsigned long)asic_result->nonce);
            } else
#endif
            {
                // Use the job's pool_id to determine which pool to submit to
                // This ensures we submit to the pool that issued this job
                uint8_t target_pool = active_job->pool_id;

                int sock;
                int *send_uid;
                char *user;
                int current_uid;

                if (target_pool == POOL_SECONDARY && stratum_is_secondary_connected(GLOBAL_STATE)) {
                    // Submit to secondary pool
                    sock = GLOBAL_STATE->sock_secondary;
                    send_uid = &GLOBAL_STATE->send_uid_secondary;
                    user = GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_user;
                    current_uid = (*send_uid)++;
                    STRATUM_V1_stamp_tx_secondary(current_uid);  // Track response time for secondary
                    ESP_LOGI(TAG, "Submitting share to SECONDARY pool (job: %s, uid: %d)", active_job->jobid, current_uid);
                } else {
                    // Submit to primary pool (or fallback in failover mode)
                    sock = GLOBAL_STATE->sock;
                    send_uid = &GLOBAL_STATE->send_uid;
                    user = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ?
                           GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_user :
                           GLOBAL_STATE->SYSTEM_MODULE.pool_user;
                    target_pool = POOL_PRIMARY;  // Ensure we track as primary
                    current_uid = (*send_uid)++;
                    STRATUM_V1_stamp_tx(current_uid);  // Track response time for primary
                    ESP_LOGI(TAG, "Submitting share to PRIMARY pool (job: %s, uid: %d)", active_job->jobid, current_uid);
                }

                int ret = STRATUM_V1_submit_share(
                    sock,
                    current_uid,
                    user,
                    active_job->jobid,
                    active_job->extranonce2,
                    active_job->ntime,
                    asic_result->nonce,
                    asic_result->rolled_version ^ active_job->version);

                if (ret < 0) {
                    ESP_LOGI(TAG, "Unable to write share to socket. Closing connection. Ret: %d (errno %d: %s)", ret, errno, strerror(errno));
                    if (target_pool == POOL_SECONDARY) {
                        stratum_close_secondary_connection(GLOBAL_STATE);
                    } else {
                        stratum_close_connection(GLOBAL_STATE);
                    }
                }
            }
        }

        SYSTEM_notify_found_nonce(GLOBAL_STATE, nonce_diff, job_id);
    }
}
