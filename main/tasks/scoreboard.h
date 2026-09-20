#ifndef SCOREBOARD_H
#define SCOREBOARD_H

#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"

#define MAX_SCOREBOARD 20

// Top-N highest-difficulty shares seen this uptime. ClusterAxe keeps this in
// RAM only (it resets on reboot); the all-time best diff is persisted
// separately via NVS_CONFIG_BEST_DIFF.
typedef struct {
    double difficulty;
    char job_id[32];
    char extranonce2[32];
    uint32_t ntime;
    uint32_t nonce;
    uint32_t version_bits;
} ScoreboardEntry;

typedef struct {
    ScoreboardEntry entries[MAX_SCOREBOARD];
    int count;
    SemaphoreHandle_t mutex;
} Scoreboard;

esp_err_t scoreboard_init(Scoreboard *scoreboard);
esp_err_t scoreboard_add(Scoreboard *scoreboard, double difficulty, const char *job_id, const char *extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version_bits);

#endif /* SCOREBOARD_H */
