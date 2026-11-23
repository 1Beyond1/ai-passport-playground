#include "desktop_state.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define DESKTOP_LINE_LEN 256

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0) return;
    size_t length = strlen(source);
    if (length >= capacity) length = capacity - 1;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static bool parse_percent(const char *text, uint8_t *result)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value < 0 || value > 100) {
        return false;
    }
    *result = (uint8_t)value;
    return true;
}

static bool parse_rate_tenths(const char *text, uint16_t *result)
{
    char *end = NULL;
    errno = 0;
    long whole = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || whole < 0 || whole > 9999) return false;

    long tenths = whole * 10;
    if (*end == '.') {
        end++;
        if (*end < '0' || *end > '9' || end[1] != '\0') return false;
        tenths += *end - '0';
        end++;
    }
    if (*end != '\0' || tenths < 0 || tenths > UINT16_MAX) return false;
    *result = (uint16_t)tenths;
    return true;
}

static bool parse_uint16(const char *text, uint16_t *result)
{
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value > UINT16_MAX) {
        return false;
    }
    *result = (uint16_t)value;
    return true;
}

static bool parse_temperature(const char *text, int16_t *result)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value < -40 || value > 150) {
        return false;
    }
    *result = (int16_t)value;
    return true;
}

void desktop_state_init(desktop_state_t *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->cpu_temp_c = 0;
    state->gpu_temp_c = 0;
    state->cpu_temp_valid = false;
    state->gpu_temp_valid = false;
    copy_text(state->time, sizeof(state->time), "--:--");
    copy_text(state->uptime, sizeof(state->uptime), "waiting");
    copy_text(state->host, sizeof(state->host), "USB WAIT");
}

bool desktop_state_parse_line(desktop_state_t *state, const char *line)
{
    if (!state || !line || strncmp(line, "PCSTAT|", 7) != 0) return false;

    size_t length = strlen(line);
    if (length >= DESKTOP_LINE_LEN) return false;

    char buffer[DESKTOP_LINE_LEN];
    memcpy(buffer, line, length + 1);
    char *line_end = strpbrk(buffer, "\r\n");
    if (line_end) *line_end = '\0';

    desktop_state_t next = *state;
    // Optional telemetry is a snapshot, not a cumulative patch. Clear it
    // before parsing so a sensor disappearing cannot leave stale readings on
    // the Gaming page.
    next.gpu_percent = 0;
    next.cpu_mhz = 0;
    next.gpu_mhz = 0;
    next.refresh_hz = 0;
    next.cpu_temp_c = 0;
    next.gpu_temp_c = 0;
    next.gpu_valid = false;
    next.cpu_frequency_valid = false;
    next.gpu_frequency_valid = false;
    next.cpu_temp_valid = false;
    next.gpu_temp_valid = false;
    next.refresh_valid = false;
    bool recognized = false;
    bool malformed = false;
    char *save = NULL;
    char *part = strtok_r(buffer + 7, "|", &save);
    while (part) {
        char *separator = strchr(part, '=');
        if (separator) {
            *separator = '\0';
            const char *value = separator + 1;
            if (strcmp(part, "cpu") == 0) {
                if (!parse_percent(value, &next.cpu_percent)) malformed = true;
                else recognized = true;
            } else if (strcmp(part, "gpu") == 0) {
                if (!parse_percent(value, &next.gpu_percent)) malformed = true;
                else {
                    next.gpu_valid = true;
                    recognized = true;
                }
            } else if (strcmp(part, "mem") == 0) {
                if (!parse_percent(value, &next.memory_percent)) malformed = true;
                else recognized = true;
            } else if (strcmp(part, "disk") == 0) {
                if (!parse_percent(value, &next.disk_percent)) malformed = true;
                else recognized = true;
            } else if (strcmp(part, "rx") == 0) {
                if (!parse_rate_tenths(value, &next.rx_tenths)) malformed = true;
                else recognized = true;
            } else if (strcmp(part, "tx") == 0) {
                if (!parse_rate_tenths(value, &next.tx_tenths)) malformed = true;
                else recognized = true;
            } else if (strcmp(part, "cpu_mhz") == 0) {
                if (!parse_uint16(value, &next.cpu_mhz)) malformed = true;
                else {
                    next.cpu_frequency_valid = true;
                    recognized = true;
                }
            } else if (strcmp(part, "gpu_mhz") == 0) {
                if (!parse_uint16(value, &next.gpu_mhz)) malformed = true;
                else {
                    next.gpu_frequency_valid = true;
                    recognized = true;
                }
            } else if (strcmp(part, "cpu_c") == 0) {
                if (!parse_temperature(value, &next.cpu_temp_c)) malformed = true;
                else {
                    next.cpu_temp_valid = true;
                    recognized = true;
                }
            } else if (strcmp(part, "gpu_c") == 0) {
                if (!parse_temperature(value, &next.gpu_temp_c)) malformed = true;
                else {
                    next.gpu_temp_valid = true;
                    recognized = true;
                }
            } else if (strcmp(part, "hz") == 0) {
                if (!parse_uint16(value, &next.refresh_hz)) malformed = true;
                else {
                    next.refresh_valid = true;
                    recognized = true;
                }
            } else if (strcmp(part, "time") == 0) {
                copy_text(next.time, sizeof(next.time), value);
                recognized = true;
            } else if (strcmp(part, "uptime") == 0) {
                copy_text(next.uptime, sizeof(next.uptime), value);
                recognized = true;
            } else if (strcmp(part, "host") == 0) {
                copy_text(next.host, sizeof(next.host), value);
                recognized = true;
            }
        }
        part = strtok_r(NULL, "|", &save);
    }

    if (!recognized || malformed) return false;
    next.valid = true;
    *state = next;
    return true;
}
