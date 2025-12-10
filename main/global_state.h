#ifndef GLOBAL_STATE_H_
#define GLOBAL_STATE_H_

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "asic_task.h"
#include "common.h"
#include "power_management_task.h"
#include "hashrate_monitor_task.h"
#include "serial.h"
#include "stratum_api.h"
#include "work_queue.h"
#include "device_config.h"
#include "display.h"

#define STRATUM_USER CONFIG_STRATUM_USER
#define FALLBACK_STRATUM_USER CONFIG_FALLBACK_STRATUM_USER

#define HISTORY_LENGTH 100
#define DIFF_STRING_SIZE 10

typedef struct {
    char message[64];
    uint32_t count;
} RejectedReasonStat;

typedef struct
{
    float current_hashrate;
    float hashrate_1m;
    float hashrate_10m;
    float hashrate_1h;
    float error_percentage;
    int64_t start_time;
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    uint64_t work_received;
    RejectedReasonStat rejected_reason_stats[10];
    int rejected_reason_stats_count;
    int screen_page;
    uint64_t best_nonce_diff;
    char best_diff_string[DIFF_STRING_SIZE];
    uint64_t best_session_nonce_diff;
    char best_session_diff_string[DIFF_STRING_SIZE];
    bool block_found;
    char ssid[32];
    char wifi_status[256];
    char ip_addr_str[16]; // IP4ADDR_STRLEN_MAX
    char ipv6_addr_str[64]; // IPv6 address string with zone identifier (INET6_ADDRSTRLEN=46 + % + interface=15)
    char ap_ssid[32];
    bool ap_enabled;
    bool is_connected;
    int identify_mode_time_ms;
    char * pool_url;
    char * fallback_pool_url;
    uint16_t pool_port;
    uint16_t fallback_pool_port;
    char * pool_user;
    char * fallback_pool_user;
    char * pool_pass;
    char * fallback_pool_pass;
    uint16_t pool_difficulty;
    uint16_t fallback_pool_difficulty;
    bool pool_extranonce_subscribe;
    bool fallback_pool_extranonce_subscribe;
    double response_time;
    double response_time_secondary;  // Response time for secondary pool in dual mode
    bool use_fallback_stratum;
    bool is_using_fallback;
    int pool_addr_family;

    // Dual pool mode settings
    uint16_t pool_mode;             // 0 = Failover, 1 = Dual Pool
    uint16_t pool_balance;          // 1-99, percentage for primary pool

    // Dual pool statistics (per pool)
    uint64_t dual_pool_shares_accepted[2];
    uint64_t dual_pool_shares_rejected[2];
    uint64_t dual_pool_best_diff[2];
    bool overheat_mode;
    uint16_t power_fault;
    uint32_t lastClockSync;
    bool is_screen_active;
    bool is_firmware_update;
    char firmware_update_filename[20];
    char firmware_update_status[20];
    char * asic_status;
} SystemModule;

typedef struct
{
    bool is_active;
    bool is_finished;
    char *message;
    char *result;
    char *finished;
} SelfTestModule;

typedef struct
{
    work_queue stratum_queue;
    work_queue stratum_queue_secondary;  // Secondary pool queue for dual pool mode
    work_queue ASIC_jobs_queue;

    SystemModule SYSTEM_MODULE;
    DeviceConfig DEVICE_CONFIG;
    DisplayConfig DISPLAY_CONFIG;
    AsicTaskModule ASIC_TASK_MODULE;
    PowerManagementModule POWER_MANAGEMENT_MODULE;
    SelfTestModule SELF_TEST_MODULE;
    HashrateMonitorModule HASHRATE_MONITOR_MODULE;

    char * extranonce_str;
    int extranonce_2_len;
    int abandon_work;

    uint8_t * valid_jobs;
    pthread_mutex_t valid_jobs_lock;

    uint32_t pool_difficulty;
    bool new_set_mining_difficulty_msg;
    uint32_t version_mask;
    bool new_stratum_version_rolling_msg;

    int sock;
    int sock_secondary;             // Secondary pool socket for dual pool mode

    // A message ID that must be unique per request that expects a response.
    // For requests not expecting a response (called notifications), this is null.
    int send_uid;
    int send_uid_secondary;         // Message ID for secondary pool

    // Dual pool connection state
    char * extranonce_str_secondary;
    int extranonce_2_len_secondary;
    uint32_t pool_difficulty_secondary;
    uint32_t version_mask_secondary;
    bool primary_pool_connected;
    bool secondary_pool_connected;
    int32_t dual_pool_error_accum;  // For dithering algorithm
    bool new_set_mining_difficulty_msg_secondary;
    bool new_stratum_version_rolling_msg_secondary;

    bool ASIC_initalized;
    bool psram_is_available;

    // Task handle for job creation task (for notifications)
    TaskHandle_t create_jobs_task_handle;

    int block_height;
    char * scriptsig;
    uint64_t network_nonce_diff;
    char network_diff_string[DIFF_STRING_SIZE];

    // Secondary pool block header info (for dual pool mode)
    int block_height_secondary;
    char * scriptsig_secondary;
    uint64_t network_nonce_diff_secondary;
    char network_diff_string_secondary[DIFF_STRING_SIZE];
} GlobalState;

#endif /* GLOBAL_STATE_H_ */
