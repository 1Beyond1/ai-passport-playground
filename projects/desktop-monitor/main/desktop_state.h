#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DESKTOP_HOST_LEN   24
#define DESKTOP_TIME_LEN    9
#define DESKTOP_UPTIME_LEN 18

typedef struct {
    uint8_t cpu_percent;
    uint8_t gpu_percent;
    uint8_t memory_percent;
    uint8_t disk_percent;
    uint16_t rx_tenths;
    uint16_t tx_tenths;
    uint16_t cpu_mhz;
    uint16_t gpu_mhz;
    uint16_t refresh_hz;
    int16_t cpu_temp_c;
    int16_t gpu_temp_c;
    bool gpu_valid;
    bool cpu_frequency_valid;
    bool gpu_frequency_valid;
    bool refresh_valid;
    char time[DESKTOP_TIME_LEN];
    char uptime[DESKTOP_UPTIME_LEN];
    char host[DESKTOP_HOST_LEN];
    bool valid;
} desktop_state_t;

void desktop_state_init(desktop_state_t *state);

// Parse one newline-delimited PCSTAT record. The existing state is unchanged
// when the record is malformed or does not begin with "PCSTAT|".
bool desktop_state_parse_line(desktop_state_t *state, const char *line);
