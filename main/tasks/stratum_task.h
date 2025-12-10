#ifndef STRATUM_TASK_H_
#define STRATUM_TASK_H_

#include "global_state.h"

// Pool indices
#define POOL_PRIMARY 0
#define POOL_SECONDARY 1

// Pool modes
#define POOL_MODE_FAILOVER 0
#define POOL_MODE_DUAL 1

void stratum_task(void *pvParameters);
void stratum_secondary_task(void *pvParameters);
void stratum_close_connection(GlobalState * GLOBAL_STATE);
void stratum_close_secondary_connection(GlobalState * GLOBAL_STATE);

// Dual pool functions
int stratum_get_next_pool(GlobalState * GLOBAL_STATE);
void stratum_notify_share_accepted(GlobalState * GLOBAL_STATE, int pool);
void stratum_notify_share_rejected(GlobalState * GLOBAL_STATE, int pool, char * error_msg);
bool stratum_is_dual_pool_mode(GlobalState * GLOBAL_STATE);
bool stratum_is_secondary_connected(GlobalState * GLOBAL_STATE);

#endif
