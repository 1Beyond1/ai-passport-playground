#include "pocket_state.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    pocket_state_t state;
    pocket_init(&state);
    pocket_move(&state, -1);
    assert(state.selection == 3);
    pocket_confirm(&state, 100, 0);
    assert(state.screen == POCKET_SCREEN_REACTION);
    pocket_confirm(&state, 100, 0);
    assert(state.reaction == POCKET_REACTION_WAIT && state.reaction_deadline == 1600);
    assert(!pocket_tick(&state, 1599));
    assert(pocket_tick(&state, 1600));
    pocket_confirm(&state, 1818, 0);
    assert(state.reaction == POCKET_REACTION_RESULT && state.reaction_ms == 218);
    pocket_confirm(&state, 2000, 3000);
    assert(state.reaction == POCKET_REACTION_WAIT);
    pocket_confirm(&state, 2001, 0);
    assert(state.reaction == POCKET_REACTION_EARLY);

    pocket_home(&state);
    state.selection = 2;
    pocket_confirm(&state, 0, 0);
    assert(state.screen == POCKET_SCREEN_RANDOM);
    pocket_confirm(&state, 0, 5);
    assert(state.random_ready && state.random_value == 1);
    pocket_move(&state, 1);
    assert(state.random_kind == 1 && !state.random_ready);
    pocket_confirm(&state, 0, 8);
    assert(state.random_value == 2);

    pocket_home(&state);
    state.selection = 1;
    pocket_confirm(&state, 0, 0);
    pocket_move(&state, -1);
    assert(state.mood == POCKET_MOOD_COUNT - 1);
    puts("Pocket state: PASS");
}

