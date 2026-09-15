#include "orbit_state.h"

void orbit_init(orbit_state_t *s, uint8_t stamps)
{
    *s = (orbit_state_t){.stamps = stamps & 0x3f};
}
void orbit_move(orbit_state_t *s, int direction)
{
    if (s->scanning) return;
    if (s->page == 1) {
        s->station = (s->station + ORBIT_STATIONS + direction) % ORBIT_STATIONS;
        s->arrived = false;
    } else {
        s->page = (s->page + 3 + direction) % 3;
    }
}
void orbit_confirm(orbit_state_t *s, uint32_t now)
{
    if (s->page != 1) { s->page = 1; return; }
    if (s->scanning) return;
    if (s->arrived) { s->page = 2; return; }
    s->scanning = true;
    s->started = now;
}
void orbit_back(orbit_state_t *s)
{
    s->scanning = false;
    s->arrived = false;
    s->page = 0;
}
bool orbit_tick(orbit_state_t *s, uint32_t now)
{
    if (!s->scanning || (uint32_t)(now - s->started) < ORBIT_SCAN_MS) return false;
    s->scanning = false;
    s->arrived = true;
    uint8_t previous = s->stamps;
    s->stamps |= (uint8_t)(1U << s->station);
    return previous != s->stamps;
}
unsigned orbit_progress(const orbit_state_t *s, uint32_t now)
{
    if (!s->scanning) return s->arrived ? 100 : 0;
    uint32_t elapsed = now - s->started;
    return elapsed >= ORBIT_SCAN_MS ? 100 : elapsed * 100 / ORBIT_SCAN_MS;
}
unsigned orbit_count(const orbit_state_t *s)
{
    unsigned n = 0;
    for (unsigned i = 0; i < ORBIT_STATIONS; ++i) n += (s->stamps >> i) & 1;
    return n;
}
