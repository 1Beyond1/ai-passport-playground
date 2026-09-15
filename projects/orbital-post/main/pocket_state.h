#pragma once

#include <stdbool.h>
#include <stdint.h>

#define POCKET_APP_COUNT 4
#define POCKET_MOOD_COUNT 6
#define POCKET_RANDOM_COUNT 3

typedef enum {
    POCKET_SCREEN_HOME = 0,
    POCKET_SCREEN_POST,
    POCKET_SCREEN_SIGN,
    POCKET_SCREEN_RANDOM,
    POCKET_SCREEN_REACTION,
} pocket_screen_t;

typedef enum {
    POCKET_REACTION_IDLE = 0,
    POCKET_REACTION_WAIT,
    POCKET_REACTION_GO,
    POCKET_REACTION_RESULT,
    POCKET_REACTION_EARLY,
} pocket_reaction_t;

typedef struct {
    pocket_screen_t screen;
    uint8_t selection;
    uint8_t mood;
    uint8_t random_kind;
    uint8_t random_value;
    bool random_ready;
    pocket_reaction_t reaction;
    uint32_t reaction_deadline;
    uint32_t reaction_started;
    uint32_t reaction_ms;
} pocket_state_t;

void pocket_init(pocket_state_t *state);
void pocket_move(pocket_state_t *state, int direction);
void pocket_confirm(pocket_state_t *state, uint32_t now, uint32_t random);
void pocket_home(pocket_state_t *state);
bool pocket_tick(pocket_state_t *state, uint32_t now);

