#include "orbit_state.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
int main(void)
{
    orbit_state_t s;
    orbit_init(&s, 0xff);
    assert(orbit_count(&s) == 6);
    orbit_init(&s, 0);
    orbit_move(&s, -1); assert(s.page == 2);
    orbit_confirm(&s, 0); assert(s.page == 1);
    orbit_move(&s, -1); assert(s.station == 5);
    orbit_confirm(&s, UINT32_MAX - 1000);
    orbit_move(&s, 1); assert(s.station == 5);
    assert(!orbit_tick(&s, 100));
    assert(orbit_progress(&s, 1499) == 50);
    assert(orbit_tick(&s, 3999));
    assert(s.arrived && orbit_count(&s) == 1);
    assert(!orbit_tick(&s, 4000));
    orbit_confirm(&s, 4000); assert(s.page == 2);
    orbit_back(&s); assert(s.page == 0);
    for (unsigned i = 0; i < ORBIT_STATIONS; ++i) {
        s.page = 1; s.station = (uint8_t)i; s.arrived = false;
        orbit_confirm(&s, 0); orbit_tick(&s, ORBIT_SCAN_MS);
    }
    assert(orbit_count(&s) == 6);
    orbit_back(&s); orbit_confirm(&s, 0); orbit_confirm(&s, 0);
    orbit_back(&s); assert(!orbit_tick(&s, 10000));
    puts("Orbit state: PASS");
}
