// Apple-inspired desktop telemetry page for the 240 x 320 AI Passport display.
#include "demo.h"
#include "desktop_state.h"

#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "demo_desktop";

#define DM_BG          0xF8F9FB
#define DM_WHITE       0xFFFFFF
#define DM_INK         0x161A22
#define DM_MUTED       0x697382
#define DM_RULE        0xD8DDE5
#define DM_BLUE        0x0A84FF
#define DM_BLUE_SOFT   0xEDF6FF
#define DM_GREEN       0x30D158
#define DM_ORANGE      0xFF9F0A
#define DM_PURPLE      0xAF52DE
#define DM_TRACK       0xE7EBF0

typedef enum {
    DM_PAGE_OVERVIEW = 0,
    DM_PAGE_CPU,
    DM_PAGE_MEMORY,
    DM_PAGE_STORAGE,
    DM_PAGE_NETWORK,
    DM_PAGE_GAMING,
    DM_PAGE_COUNT,
} dm_page_t;

typedef enum {
    DM_METRIC_CPU,
    DM_METRIC_MEMORY,
    DM_METRIC_DISK,
    DM_METRIC_RX,
    DM_METRIC_TX,
    DM_METRIC_UPTIME,
    DM_METRIC_HOST,
    DM_METRIC_LINK,
    DM_METRIC_APP_SPACE,
    DM_METRIC_SWAP,
    DM_METRIC_FREE,
    DM_METRIC_HEALTH,
    DM_METRIC_GAME_CPU,
    DM_METRIC_GAME_GPU,
    DM_METRIC_REFRESH,
} dm_metric_t;

typedef struct {
    dm_metric_t kind;
    lv_obj_t *value;
    lv_obj_t *sub;
    lv_obj_t *meter;
    int meter_width;
} dm_slot_t;

static const char *const PAGE_TITLES[] = {
    "Desktop Monitor", "CPU", "Memory", "Storage", "Network", "Gaming",
};
static const char *const PAGE_SUBTITLES[] = {
    "A quiet window into your desktop",
    "Processor load, right now",
    "How much room your apps have",
    "The system disk at a glance",
    "Live traffic from your desktop",
    "CPU, GPU and display while you play",
};

static lv_obj_t *s_screen;
static lv_obj_t *s_body;
static lv_obj_t *s_time;
static lv_obj_t *s_connection;
static lv_obj_t *s_battery;
static lv_obj_t *s_note_text;
static dm_slot_t s_slots[4];
static size_t s_slot_count;
static dm_page_t s_page;
static bool s_detail;
static desktop_state_t s_state;
static lv_timer_t *s_battery_timer;
static lv_timer_t *s_status_timer;
static TaskHandle_t s_serial_task;
static volatile bool s_serial_running;
static volatile TickType_t s_last_packet_tick;

#define DM_STATUS_TIMEOUT_MS 3500

static lv_obj_t *dm_block(lv_obj_t *parent, int x, int y, int width, int height,
                          uint32_t color, int radius)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    return object;
}

static lv_obj_t *dm_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                          uint32_t color, int x, int y, int width,
                          lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    // Every label in this fixed 240 px layout is intentionally one line.
    // LVGL's default wrapping otherwise turns short header labels into
    // two-line text and clips the second line inside the 32 px header.
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}

static uint8_t dm_metric_percent(dm_metric_t kind)
{
    if (!s_state.valid) return 0;
    switch (kind) {
    case DM_METRIC_CPU: return s_state.cpu_percent;
    case DM_METRIC_MEMORY: return s_state.memory_percent;
    case DM_METRIC_DISK: return s_state.disk_percent;
    case DM_METRIC_RX: {
        uint16_t value = s_state.rx_tenths > 1000 ? 1000 : s_state.rx_tenths;
        return (uint8_t)((value * 100U) / 1000U);
    }
    case DM_METRIC_TX: {
        uint16_t value = s_state.tx_tenths > 1000 ? 1000 : s_state.tx_tenths;
        return (uint8_t)((value * 100U) / 1000U);
    }
    case DM_METRIC_GAME_CPU: return s_state.cpu_percent;
    case DM_METRIC_GAME_GPU: return s_state.gpu_percent;
    default: return 0;
    }
}

static bool dm_metric_needs_live_data(dm_metric_t kind)
{
    switch (kind) {
    case DM_METRIC_CPU:
    case DM_METRIC_MEMORY:
    case DM_METRIC_DISK:
    case DM_METRIC_RX:
    case DM_METRIC_TX:
    case DM_METRIC_UPTIME:
    case DM_METRIC_HOST:
    case DM_METRIC_GAME_CPU:
    case DM_METRIC_GAME_GPU:
    case DM_METRIC_REFRESH:
        return true;
    default:
        return false;
    }
}

static void dm_metric_value(dm_metric_t kind, char *buffer, size_t capacity)
{
    if (!s_state.valid && dm_metric_needs_live_data(kind)) {
        if (kind == DM_METRIC_REFRESH) snprintf(buffer, capacity, "-- Hz");
        else if (kind == DM_METRIC_GAME_GPU) snprintf(buffer, capacity, "--");
        else snprintf(buffer, capacity, "--");
        return;
    }

    switch (kind) {
    case DM_METRIC_CPU:
        snprintf(buffer, capacity, "%u%%", s_state.cpu_percent);
        break;
    case DM_METRIC_MEMORY:
        snprintf(buffer, capacity, "%u%%", s_state.memory_percent);
        break;
    case DM_METRIC_DISK:
        snprintf(buffer, capacity, "%u%%", s_state.disk_percent);
        break;
    case DM_METRIC_RX:
        snprintf(buffer, capacity, "%u.%u MB/s",
                 s_state.rx_tenths / 10U, s_state.rx_tenths % 10U);
        break;
    case DM_METRIC_TX:
        snprintf(buffer, capacity, "%u.%u MB/s",
                 s_state.tx_tenths / 10U, s_state.tx_tenths % 10U);
        break;
    case DM_METRIC_UPTIME:
        snprintf(buffer, capacity, "%s", s_state.uptime);
        break;
    case DM_METRIC_HOST:
        snprintf(buffer, capacity, "%s", s_state.host);
        break;
    case DM_METRIC_LINK:
        snprintf(buffer, capacity, "%s", s_state.valid ? "USB" : "WAIT");
        break;
    case DM_METRIC_APP_SPACE:
        snprintf(buffer, capacity, "6.7 GB");
        break;
    case DM_METRIC_SWAP:
        snprintf(buffer, capacity, "0.0 GB");
        break;
    case DM_METRIC_FREE:
        snprintf(buffer, capacity, "171 GB");
        break;
    case DM_METRIC_HEALTH:
        snprintf(buffer, capacity, "GOOD");
        break;
    case DM_METRIC_GAME_CPU:
        if (s_state.cpu_frequency_valid) {
            snprintf(buffer, capacity, "%u%% %uM", s_state.cpu_percent, s_state.cpu_mhz);
        } else {
            snprintf(buffer, capacity, "%u%% --", s_state.cpu_percent);
        }
        break;
    case DM_METRIC_GAME_GPU:
        if (!s_state.gpu_valid) {
            snprintf(buffer, capacity, "N/A");
        } else if (s_state.gpu_frequency_valid) {
            snprintf(buffer, capacity, "%u%% %uM", s_state.gpu_percent, s_state.gpu_mhz);
        } else {
            snprintf(buffer, capacity, "%u%% --", s_state.gpu_percent);
        }
        break;
    case DM_METRIC_REFRESH:
        if (s_state.refresh_valid) snprintf(buffer, capacity, "%u Hz", s_state.refresh_hz);
        else snprintf(buffer, capacity, "-- Hz");
        break;
    }
}

static const char *dm_metric_subtitle(dm_metric_t kind)
{
    static char dynamic[32];

    if (!s_state.valid && dm_metric_needs_live_data(kind)) {
        return "waiting for desktop";
    }

    switch (kind) {
    case DM_METRIC_CPU: return "processor load";
    case DM_METRIC_MEMORY: return "9.3 / 16 GB used";
    case DM_METRIC_DISK: return "system disk used";
    case DM_METRIC_RX: return "from desktop";
    case DM_METRIC_TX: return "to desktop";
    case DM_METRIC_UPTIME: return "since last start";
    case DM_METRIC_HOST: return "connected over USB";
    case DM_METRIC_LINK: return "stable connection";
    case DM_METRIC_APP_SPACE: return "available to apps";
    case DM_METRIC_SWAP: return "under control";
    case DM_METRIC_FREE: return "available";
    case DM_METRIC_HEALTH: return "last checked now";
    case DM_METRIC_GAME_CPU:
        if (s_state.cpu_temp_c >= 0) {
            snprintf(dynamic, sizeof(dynamic), "%d C temperature", s_state.cpu_temp_c);
        } else {
            snprintf(dynamic, sizeof(dynamic), "CPU temp unavailable");
        }
        return dynamic;
    case DM_METRIC_GAME_GPU:
        if (!s_state.gpu_valid) return "GPU unavailable";
        if (s_state.gpu_temp_c >= 0) {
            snprintf(dynamic, sizeof(dynamic), "%d C temperature", s_state.gpu_temp_c);
        } else {
            snprintf(dynamic, sizeof(dynamic), "GPU temp unavailable");
        }
        return dynamic;
    case DM_METRIC_REFRESH: return "current refresh rate";
    }
    return "";
}

static void dm_slot_refresh(dm_slot_t *slot)
{
    if (!slot || !slot->value) return;

    char value[40];
    dm_metric_value(slot->kind, value, sizeof(value));
    lv_label_set_text(slot->value, value);
    if (slot->sub) {
        if (slot->kind == DM_METRIC_RX && s_page == DM_PAGE_OVERVIEW && s_state.valid) {
            lv_label_set_text_fmt(slot->sub, "TX %u.%u MB/s",
                                  s_state.tx_tenths / 10U, s_state.tx_tenths % 10U);
        } else {
            lv_label_set_text(slot->sub, dm_metric_subtitle(slot->kind));
        }
    }
    if (slot->meter) {
        uint8_t percent = dm_metric_percent(slot->kind);
        int width = s_state.valid ? 2 + (slot->meter_width * percent) / 100 : 0;
        lv_obj_set_width(slot->meter, width);
    }
}

static void dm_add_slot(lv_obj_t *parent, const char *label, dm_metric_t kind,
                        int x, int y, int width, int height, uint32_t accent,
                        bool meter)
{
    if (s_slot_count >= sizeof(s_slots) / sizeof(s_slots[0])) return;
    dm_slot_t *slot = &s_slots[s_slot_count++];
    memset(slot, 0, sizeof(*slot));
    slot->kind = kind;

    lv_obj_t *card = dm_block(parent, x, y, width, height, DM_WHITE, 7);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(DM_RULE), 0);
    dm_block(card, 0, 0, 3, height, accent, 0);
    dm_label(card, label, &lv_font_montserrat_14, DM_MUTED, 10, 6, width - 18,
             LV_TEXT_ALIGN_LEFT);
    slot->value = dm_label(card, "", &lv_font_montserrat_20, DM_INK,
                           10, 18, width - 18, LV_TEXT_ALIGN_LEFT);
    slot->sub = dm_label(card, "", &lv_font_montserrat_14, DM_MUTED,
                         10, height - 18, width - 18, LV_TEXT_ALIGN_LEFT);
    if (meter) {
        slot->meter_width = width - 20;
        dm_block(card, 10, height - 7, slot->meter_width, 3, DM_TRACK, 2);
        slot->meter = dm_block(card, 10, height - 7, 2, 3, DM_BLUE, 2);
    }
    dm_slot_refresh(slot);
}

static void dm_footer(lv_obj_t *parent)
{
    lv_obj_t *footer = dm_block(parent, 0, 246, 240, 42, DM_BG, 0);
    dm_block(footer, 0, 0, 240, 1, DM_RULE, 0);
    dm_label(footer, "UP/DOWN page", &lv_font_montserrat_14, DM_MUTED,
             12, 14, 105, LV_TEXT_ALIGN_LEFT);
    dm_label(footer, s_detail ? "OK normal" : "OK details", &lv_font_montserrat_14, DM_BLUE,
             148, 14, 80, LV_TEXT_ALIGN_RIGHT);
}

static void dm_refresh_global(void)
{
    if (!s_time) return;
    lv_label_set_text(s_time, s_state.valid ? s_state.time : "--:--");
    lv_label_set_text(s_connection, s_state.valid ? "USB" : "WAIT");
    lv_obj_set_style_text_color(s_connection,
                                lv_color_hex(s_state.valid ? DM_GREEN : DM_MUTED), 0);
    if (s_note_text) {
        if (s_detail) {
            lv_label_set_text(s_note_text, "OK: summary  LONG OK: overview");
        } else if (s_state.valid) {
            lv_label_set_text_fmt(s_note_text, "Last sync %s  %s",
                                  s_state.time, s_state.host);
        } else {
            lv_label_set_text(s_note_text, "Waiting for PCSTAT over USB");
        }
    }
    for (size_t i = 0; i < s_slot_count; i++) dm_slot_refresh(&s_slots[i]);
}

static void dm_draw_layout(void)
{
    if (!s_body) return;
    lv_obj_clean(s_body);
    memset(s_slots, 0, sizeof(s_slots));
    s_slot_count = 0;
    s_note_text = NULL;

    dm_label(s_body, s_detail ? "DETAIL / DESKTOP" : "STATUS / DESKTOP",
             &lv_font_montserrat_14, DM_MUTED, 14, 9, 212, LV_TEXT_ALIGN_LEFT);
    dm_label(s_body, PAGE_TITLES[s_page], &lv_font_montserrat_20,
             DM_INK, 14, 21, 212, LV_TEXT_ALIGN_LEFT);
    dm_label(s_body, PAGE_SUBTITLES[s_page], &lv_font_montserrat_14,
             DM_MUTED, 14, 47, 212, LV_TEXT_ALIGN_LEFT);

    if (s_page == DM_PAGE_OVERVIEW) {
        dm_add_slot(s_body, "CPU", DM_METRIC_CPU, 12, 67, 103, 57, DM_BLUE, true);
        dm_add_slot(s_body, "MEMORY", DM_METRIC_MEMORY, 125, 67, 103, 57, DM_GREEN, true);
        dm_add_slot(s_body, "DISK", DM_METRIC_DISK, 12, 130, 103, 57, DM_ORANGE, true);
        dm_add_slot(s_body, "NETWORK", DM_METRIC_RX, 125, 130, 103, 57, DM_PURPLE, false);

        lv_obj_t *note = dm_block(s_body, 14, 202, 212, 31, DM_BLUE_SOFT, 7);
        s_note_text = dm_label(note, "", &lv_font_montserrat_14, 0x245581,
                               9, 8, 194, LV_TEXT_ALIGN_LEFT);
    } else if (s_page == DM_PAGE_CPU) {
        dm_add_slot(s_body, "CPU LOAD", DM_METRIC_CPU, 12, 60, 216, 58, DM_BLUE, true);
        dm_add_slot(s_body, "UPTIME", DM_METRIC_UPTIME, 12, 122, 216, 58, DM_GREEN, false);
        dm_add_slot(s_body, "HOST", DM_METRIC_HOST, 12, 184, 216, 58, DM_ORANGE, false);
    } else if (s_page == DM_PAGE_MEMORY) {
        dm_add_slot(s_body, "MEMORY", DM_METRIC_MEMORY, 12, 60, 216, 58, DM_GREEN, true);
        dm_add_slot(s_body, "APP SPACE", DM_METRIC_APP_SPACE, 12, 122, 216, 58, DM_BLUE, false);
        dm_add_slot(s_body, "SWAP", DM_METRIC_SWAP, 12, 184, 216, 58, DM_PURPLE, false);
    } else if (s_page == DM_PAGE_STORAGE) {
        dm_add_slot(s_body, "SYSTEM DISK", DM_METRIC_DISK, 12, 60, 216, 58, DM_ORANGE, true);
        dm_add_slot(s_body, "FREE", DM_METRIC_FREE, 12, 122, 216, 58, DM_BLUE, false);
        dm_add_slot(s_body, "HEALTH", DM_METRIC_HEALTH, 12, 184, 216, 58, DM_GREEN, false);
    } else if (s_page == DM_PAGE_NETWORK) {
        dm_add_slot(s_body, "DOWNLOAD", DM_METRIC_RX, 12, 60, 216, 58, DM_BLUE, false);
        dm_add_slot(s_body, "UPLOAD", DM_METRIC_TX, 12, 122, 216, 58, DM_GREEN, false);
        dm_add_slot(s_body, "LINK", DM_METRIC_LINK, 12, 184, 216, 58, DM_PURPLE, false);
    } else {
        // The large value already carries utilization. Omitting the tiny
        // progress bar leaves the temperature subtitle clear on a 58 px card.
        dm_add_slot(s_body, "CPU", DM_METRIC_GAME_CPU, 12, 60, 216, 58, DM_BLUE, false);
        dm_add_slot(s_body, "GPU", DM_METRIC_GAME_GPU, 12, 122, 216, 58, DM_PURPLE, false);
        dm_add_slot(s_body, "DISPLAY", DM_METRIC_REFRESH, 12, 184, 216, 58, DM_GREEN, false);
    }

    dm_footer(s_body);
    dm_refresh_global();
}

static void dm_battery_tick(lv_timer_t *timer)
{
    (void)timer;
    if (!s_battery) return;
    int soc = bsp_battery_soc();
    if (soc < 0 || soc > 100) lv_label_set_text(s_battery, "BAT --");
    else lv_label_set_text_fmt(s_battery, "BAT %d%%", soc);
}

static void dm_status_tick(lv_timer_t *timer)
{
    (void)timer;
    if (!bsp_lvgl_lock(0)) return;
    if (!s_state.valid || s_last_packet_tick == 0) {
        bsp_lvgl_unlock();
        return;
    }

    TickType_t elapsed = xTaskGetTickCount() - s_last_packet_tick;
    if (elapsed < pdMS_TO_TICKS(DM_STATUS_TIMEOUT_MS)) {
        bsp_lvgl_unlock();
        return;
    }

    // Do not leave frozen telemetry on screen after the bridge or cable has
    // gone away. The next valid PCSTAT record repopulates the same page.
    desktop_state_init(&s_state);
    s_last_packet_tick = 0;
    dm_refresh_global();
    ESP_LOGW(TAG, "PCSTAT timeout; waiting for desktop bridge");
    bsp_lvgl_unlock();
}

static void dm_apply_line(const char *line)
{
    desktop_state_t next;
    if (!bsp_lvgl_lock(100)) return;
    next = s_state;
    bsp_lvgl_unlock();

    if (!desktop_state_parse_line(&next, line)) return;
    if (!bsp_lvgl_lock(500)) return;
    s_state = next;
    s_last_packet_tick = xTaskGetTickCount();
    dm_refresh_global();
    bsp_lvgl_unlock();
    ESP_LOGI(TAG, "PCSTAT received from %s", next.host);
}

static void dm_serial_worker(void *arg)
{
    (void)arg;
    char line[256];
    size_t length = 0;

    while (s_serial_running) {
        int character = fgetc(stdin);
        if (character == EOF) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (character == '\n' || character == '\r') {
            if (length > 0) {
                line[length] = '\0';
                dm_apply_line(line);
                length = 0;
            }
        } else if (length < sizeof(line) - 1) {
            line[length++] = (char)character;
        } else {
            // Drop an overlong record instead of applying a truncated state.
            length = 0;
        }
    }
    s_serial_task = NULL;
    vTaskDelete(NULL);
}

void demo_desktop_enter(void)
{
    desktop_state_init(&s_state);
    s_page = DM_PAGE_OVERVIEW;
    s_detail = false;
    s_last_packet_tick = 0;
    bsp_display_backlight(100);

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(DM_BG), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    lv_obj_t *header = dm_block(s_screen, 0, 0, 240, 32, DM_BG, 0);
    dm_block(header, 0, 31, 240, 1, DM_RULE, 0);
    dm_label(header, "AI Passport", &lv_font_montserrat_14, DM_INK,
             12, 8, 84, LV_TEXT_ALIGN_LEFT);
    s_time = dm_label(header, "--:--", &lv_font_montserrat_14, DM_MUTED,
                      98, 8, 42, LV_TEXT_ALIGN_CENTER);
    s_connection = dm_label(header, "WAIT", &lv_font_montserrat_14, DM_MUTED,
                            143, 8, 40, LV_TEXT_ALIGN_CENTER);
    s_battery = dm_label(header, "BAT --", &lv_font_montserrat_14, DM_INK,
                         180, 8, 56, LV_TEXT_ALIGN_RIGHT);

    s_body = dm_block(s_screen, 0, 32, 240, 288, DM_BG, 0);
    dm_draw_layout();
    lv_screen_load(s_screen);
    dm_battery_tick(NULL);
    s_battery_timer = lv_timer_create(dm_battery_tick, 10000, NULL);
    s_status_timer = lv_timer_create(dm_status_tick, 1000, NULL);

    s_serial_running = true;
    if (xTaskCreate(dm_serial_worker, "desktop_serial", 3072, NULL, 4,
                    &s_serial_task) != pdPASS) {
        s_serial_running = false;
        ESP_LOGE(TAG, "failed to create USB serial worker");
    }
}

void demo_desktop_exit(void)
{
    s_serial_running = false;
    if (s_serial_task) {
        vTaskDelete(s_serial_task);
        s_serial_task = NULL;
    }
    if (s_battery_timer) {
        lv_timer_delete(s_battery_timer);
        s_battery_timer = NULL;
    }
    if (s_status_timer) {
        lv_timer_delete(s_status_timer);
        s_status_timer = NULL;
    }
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
        s_body = s_time = s_connection = s_battery = NULL;
        s_note_text = NULL;
        memset(s_slots, 0, sizeof(s_slots));
        s_slot_count = 0;
    }
}

void demo_desktop_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP) {
        s_page = (s_page + DM_PAGE_COUNT - 1) % DM_PAGE_COUNT;
        s_detail = false;
    } else if (btn == BSP_BTN_DOWN) {
        s_page = (s_page + 1) % DM_PAGE_COUNT;
        s_detail = false;
    } else if (btn == BSP_BTN_OK) {
        s_detail = !s_detail;
    } else {
        return;
    }
    dm_draw_layout();
}

bool demo_desktop_back(void)
{
    if (s_page != DM_PAGE_OVERVIEW || s_detail) {
        s_page = DM_PAGE_OVERVIEW;
        s_detail = false;
        dm_draw_layout();
    }
    return true;
}
