#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ORBIT_STATIONS 6
#define ORBIT_SCAN_MS 5000U
typedef struct {
    uint8_t page, station, stamps;
    bool scanning, arrived;
    uint32_t started;
} orbit_state_t;
void orbit_init(orbit_state_t *s, uint8_t stamps);
void orbit_move(orbit_state_t *s, int direction);
void orbit_confirm(orbit_state_t *s, uint32_t now);
void orbit_back(orbit_state_t *s);
bool orbit_tick(orbit_state_t *s, uint32_t now);
unsigned orbit_progress(const orbit_state_t *s, uint32_t now);
unsigned orbit_count(const orbit_state_t *s);
