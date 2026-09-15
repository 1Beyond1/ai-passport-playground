#include "pocket_state.h"

static uint8_t wrap(uint8_t value, int direction, uint8_t count)
{
    return (uint8_t)((value + count + direction) % count);
}

void pocket_init(pocket_state_t *state)
{
    *state = (pocket_state_t){0};
}

void pocket_move(pocket_state_t *state, int direction)
{
    if (state->screen == POCKET_SCREEN_HOME) {
        state->selection = wrap(state->selection, direction, POCKET_APP_COUNT);
    } else if (state->screen == POCKET_SCREEN_SIGN) {
        state->mood = wrap(state->mood, direction, POCKET_MOOD_COUNT);
    } else if (state->screen == POCKET_SCREEN_RANDOM) {
        state->random_kind = wrap(state->random_kind, direction, POCKET_RANDOM_COUNT);
        state->random_ready = false;
    } else if (state->screen == POCKET_SCREEN_REACTION &&
               state->reaction != POCKET_REACTION_WAIT) {
        state->reaction = POCKET_REACTION_IDLE;
    }
}

void pocket_confirm(pocket_state_t *state, uint32_t now, uint32_t random)
{
    if (state->screen == POCKET_SCREEN_HOME) {
        state->screen = (pocket_screen_t)(POCKET_SCREEN_POST + state->selection);
        return;
    }
    if (state->screen == POCKET_SCREEN_RANDOM) {
        static const uint8_t limits[POCKET_RANDOM_COUNT] = {2, 6, 6};
        state->random_value = (uint8_t)(random % limits[state->random_kind]);
        state->random_ready = true;
        return;
    }
    if (state->screen != POCKET_SCREEN_REACTION) return;

    if (state->reaction == POCKET_REACTION_IDLE ||
        state->reaction == POCKET_REACTION_RESULT ||
        state->reaction == POCKET_REACTION_EARLY) {
        state->reaction = POCKET_REACTION_WAIT;
        state->reaction_deadline = now + 1500U + random % 3001U;
    } else if (state->reaction == POCKET_REACTION_WAIT) {
        state->reaction = POCKET_REACTION_EARLY;
    } else if (state->reaction == POCKET_REACTION_GO) {
        state->reaction_ms = now - state->reaction_started;
        state->reaction = POCKET_REACTION_RESULT;
    }
}

void pocket_home(pocket_state_t *state)
{
    state->screen = POCKET_SCREEN_HOME;
    state->reaction = POCKET_REACTION_IDLE;
}

bool pocket_tick(pocket_state_t *state, uint32_t now)
{
    if (state->screen != POCKET_SCREEN_REACTION ||
        state->reaction != POCKET_REACTION_WAIT ||
        (int32_t)(now - state->reaction_deadline) < 0) return false;
    state->reaction = POCKET_REACTION_GO;
    state->reaction_started = now;
    return true;
}

