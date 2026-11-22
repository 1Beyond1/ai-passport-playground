#include "desktop_state.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void assert_parse_rejected_unchanged(desktop_state_t *state, const char *line)
{
    desktop_state_t before = *state;
    assert(!desktop_state_parse_line(state, line));
    assert(memcmp(state, &before, sizeof(*state)) == 0);
}

int main(void)
{
    desktop_state_t state;

    /* Null safety and default initialization. */
    desktop_state_init(NULL);
    desktop_state_init(&state);
    assert(!state.valid);
    assert(state.cpu_percent == 0);
    assert(state.gpu_percent == 0);
    assert(state.memory_percent == 0);
    assert(state.disk_percent == 0);
    assert(state.rx_tenths == 0);
    assert(state.tx_tenths == 0);
    assert(state.cpu_temp_c == -1);
    assert(state.gpu_temp_c == -1);
    assert(!state.gpu_valid);
    assert(!state.cpu_frequency_valid);
    assert(!state.gpu_frequency_valid);
    assert(!state.refresh_valid);
    assert(strcmp(state.time, "--:--") == 0);
    assert(strcmp(state.uptime, "waiting") == 0);
    assert(strcmp(state.host, "USB WAIT") == 0);

    /* Legacy full PCSTAT record (CRLF terminated, integer and tenth rates). */
    assert(desktop_state_parse_line(
        &state,
        "PCSTAT|cpu=37|mem=58|disk=68|rx=1.2|tx=2|cpu_mhz=4200|gpu=61|"
        "gpu_mhz=2400|gpu_c=68|cpu_c=72|hz=165|time=14:28|uptime=3h18m|host=Windows\r\n"));
    assert(state.valid);
    assert(state.cpu_percent == 37);
    assert(state.memory_percent == 58);
    assert(state.disk_percent == 68);
    assert(state.rx_tenths == 12);
    assert(state.tx_tenths == 20);
    assert(state.cpu_frequency_valid && state.cpu_mhz == 4200);
    assert(state.gpu_valid && state.gpu_percent == 61);
    assert(state.gpu_frequency_valid && state.gpu_mhz == 2400);
    assert(state.cpu_temp_c == 72);
    assert(state.gpu_temp_c == 68);
    assert(state.refresh_valid && state.refresh_hz == 165);
    assert(strcmp(state.time, "14:28") == 0);
    assert(strcmp(state.uptime, "3h18m") == 0);
    assert(strcmp(state.host, "Windows") == 0);

    /*
     * Forward/backward compatibility with extended desktop_bridge.py frames:
     * unknown keys (mem_used_gb, mem_total_gb, disk_used_gb, disk_total_gb)
     * must be ignored cleanly without breaking existing firmware parsing.
     */
    assert(desktop_state_parse_line(
        &state,
        "PCSTAT|cpu=45|mem=62|disk=74|rx=6553.5|tx=0.4|cpu_mhz=4500|gpu=50|"
        "gpu_mhz=2100|gpu_c=-10|cpu_c=0|hz=240|mem_used_gb=19.8|mem_total_gb=32.0|"
        "disk_used_gb=740.4|disk_total_gb=1000.0|time=18:05|uptime=5h12m|host=WORKSTATION-01\n"));
    assert(state.valid);
    assert(state.cpu_percent == 45);
    assert(state.memory_percent == 62);
    assert(state.disk_percent == 74);
    assert(state.rx_tenths == UINT16_MAX);
    assert(state.tx_tenths == 4);
    assert(state.cpu_frequency_valid && state.cpu_mhz == 4500);
    assert(state.gpu_valid && state.gpu_percent == 50);
    assert(state.gpu_frequency_valid && state.gpu_mhz == 2100);
    assert(state.gpu_temp_c == -10);
    assert(state.cpu_temp_c == 0);
    assert(state.refresh_valid && state.refresh_hz == 240);
    assert(strcmp(state.time, "18:05") == 0);
    assert(strcmp(state.uptime, "5h12m") == 0);
    assert(strcmp(state.host, "WORKSTATION-01") == 0);

    /* Snapshot semantics: absent optional telemetry must clear previous values. */
    assert(desktop_state_parse_line(
        &state,
        "PCSTAT|cpu=12|mem=40|disk=50|rx=0.0|tx=0.0|time=19:00|uptime=1h2m|host=Laptop\n"));
    assert(state.valid);
    assert(state.cpu_percent == 12);
    assert(state.memory_percent == 40);
    assert(state.disk_percent == 50);
    assert(state.rx_tenths == 0);
    assert(state.tx_tenths == 0);
    assert(!state.gpu_valid && state.gpu_percent == 0);
    assert(!state.cpu_frequency_valid && state.cpu_mhz == 0);
    assert(!state.gpu_frequency_valid && state.gpu_mhz == 0);
    assert(!state.refresh_valid && state.refresh_hz == 0);
    assert(state.cpu_temp_c == -1);
    assert(state.gpu_temp_c == -1);
    assert(strcmp(state.host, "Laptop") == 0);

    /* gpu_c alone marks GPU telemetry valid. */
    assert(desktop_state_parse_line(&state, "PCSTAT|gpu_c=63\n"));
    assert(state.gpu_valid && state.gpu_temp_c == 63);

    /* Malformed, out-of-range, and non-PCSTAT lines must leave state intact. */
    assert(!desktop_state_parse_line(NULL, "PCSTAT|cpu=10"));
    assert_parse_rejected_unchanged(&state, NULL);
    assert_parse_rejected_unchanged(&state, "");
    assert_parse_rejected_unchanged(&state, "OTHER|cpu=10");
    assert_parse_rejected_unchanged(&state, "PCSTAT");
    assert_parse_rejected_unchanged(&state, "PCSTAT|");
    assert_parse_rejected_unchanged(&state, "PCSTAT|noequals");
    assert_parse_rejected_unchanged(&state, "PCSTAT|mem_used_gb=8.0|mem_total_gb=16.0");
    assert_parse_rejected_unchanged(&state, "PCSTAT|cpu=101");
    assert_parse_rejected_unchanged(&state, "PCSTAT|cpu=-1");
    assert_parse_rejected_unchanged(&state, "PCSTAT|mem=abc");
    assert_parse_rejected_unchanged(&state, "PCSTAT|disk=255");
    assert_parse_rejected_unchanged(&state, "PCSTAT|rx=1.25");
    assert_parse_rejected_unchanged(&state, "PCSTAT|rx=-1.0");
    assert_parse_rejected_unchanged(&state, "PCSTAT|rx=6553.6");
    assert_parse_rejected_unchanged(&state, "PCSTAT|cpu_mhz=65536");
    assert_parse_rejected_unchanged(&state, "PCSTAT|cpu_c=-41");
    assert_parse_rejected_unchanged(&state, "PCSTAT|gpu_c=151");
    assert_parse_rejected_unchanged(&state, "PCSTAT|cpu=50|mem=999|host=PartialFail");

    /* Buffer length boundary: 255 bytes passes; 256 bytes (>= DESKTOP_LINE_LEN) fails. */
    char boundary[260];
    const char *prefix = "PCSTAT|cpu=55|mem_used_gb=8.0|pad=";
    size_t prefix_len = strlen(prefix);
    memcpy(boundary, prefix, prefix_len);
    memset(boundary + prefix_len, 'a', 255 - prefix_len);
    boundary[255] = '\0';
    assert(strlen(boundary) == 255);
    assert(desktop_state_parse_line(&state, boundary));
    assert(state.cpu_percent == 55);

    boundary[255] = 'b';
    boundary[256] = '\0';
    assert(strlen(boundary) == 256);
    assert_parse_rejected_unchanged(&state, boundary);

    /* String fields are truncated safely to their destination buffer capacity. */
    assert(desktop_state_parse_line(
        &state,
        "PCSTAT|time=12:34:56:78|uptime=12345678901234567890|"
        "host=ABCDEFGHIJKLMNOPQRSTUVWXYZ012345\n"));
    assert(strlen(state.time) == DESKTOP_TIME_LEN - 1);
    assert(strlen(state.uptime) == DESKTOP_UPTIME_LEN - 1);
    assert(strlen(state.host) == DESKTOP_HOST_LEN - 1);

    puts("Desktop state: PASS");
    return 0;
}
