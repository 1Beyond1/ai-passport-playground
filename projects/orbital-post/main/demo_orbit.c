// Orbital Post: an offline, pocket-sized fictional passport. No radio needed.
#include "demo.h"
#include "orbit_state.h"
#include "ui_pixel.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define PAPER 0xEAF0ED
#define INK 0x193B59
#define BLUE 0x537C96
#define ORANGE 0xE87943
#define PALE 0xCEDDDC

static const char *const names[] = {
    "MOON GARDEN", "TIDAL LIBRARY", "CLOUD HARBOR",
    "AMBER DESERT", "SLEEPING COMET", "HOME, EARTH"
};
static const char *const letters[] = {
    "The gardener saved\na little moonlight\njust for your window.",
    "A book washed ashore.\nEvery blank page\nremembers the sea.",
    "Your parcel arrived.\nInside: one cloud.\nPlease do not fold.",
    "Nothing blooms here.\nThen you notice\none very small flower.",
    "The comet is asleep.\nLeave your worries.\nIt will carry them away.",
    "No special mission.\nSomeone left a light\non for you. Welcome."
};
static orbit_state_t state;
static lv_obj_t *screen, *body, *battery, *mascot, *status, *meter;
static lv_timer_t *timer;
static QueueHandle_t saves;
static TaskHandle_t saver;
static uint8_t restored;
static unsigned frame;
static bool storage_ok;

// Storage never executes in a button callback. Each new stamp is coalesced.
static void save_worker(void *arg)
{
    (void)arg;
    uint8_t bits;
    while (xQueueReceive(saves, &bits, portMAX_DELAY) == pdTRUE) {
        nvs_handle_t handle;
        esp_err_t err = nvs_open("orbit_post", NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            err = nvs_set_u8(handle, "stamps", bits);
            if (err == ESP_OK) err = nvs_commit(handle);
            nvs_close(handle);
        }
        if (err != ESP_OK) ESP_LOGW("orbit", "stamp save failed: %s", esp_err_to_name(err));
    }
}
void demo_orbit_restore(void)
{
    restored = 0;
    // Never erase NVS on failure: other applications may own its contents.
    storage_ok = nvs_flash_init() == ESP_OK;
    if (!storage_ok) return;
    nvs_handle_t handle;
    if (nvs_open("orbit_post", NVS_READONLY, &handle) == ESP_OK) {
        (void)nvs_get_u8(handle, "stamps", &restored);
        nvs_close(handle);
    }
    saves = xQueueCreate(1, sizeof(uint8_t));
    if (!saves || xTaskCreate(save_worker, "orbit_save", 3072, NULL, 2, &saver) != pdPASS)
        storage_ok = false;
}
static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}
static lv_obj_t *text(lv_obj_t *p, int x, int y, int w, const char *str, bool big, uint32_t color)
{
    lv_obj_t *o = ui_pixel_label(p, str, big ? &lv_font_montserrat_20 : &lv_font_montserrat_14, color);
    lv_obj_set_pos(o, x, y); lv_obj_set_width(o, w);
    lv_obj_set_style_text_line_space(o, 4, 0);
    return o;
}
static void draw(void)
{
    lv_obj_clean(body);
    mascot = status = meter = NULL;
    text(body, 12, 8, 188, state.page == 0 ? "TRAVEL DOCUMENT" : state.page == 1 ? "INCOMING POST" : "YOUR POSTMARKS", false, BLUE);
    box(body, 12, 30, 190, 2, INK);
    if (state.page == 0) {
        text(body, 12, 40, 190, "Little departures.", true, INK);
        lv_obj_t *portrait = box(body, 12, 75, 82, 88, PALE);
        mascot = ui_pixel_mascot_create(portrait, 22, 20);
        text(body, 105, 78, 98, "COURIER", false, BLUE);
        text(body, 105, 99, 100, "PIP-01", true, INK);
        text(body, 105, 129, 94, orbit_count(&state) == 6 ? "ALL STARS\nDELIVERED" : "LICENSE TO\nWANDER", false, INK);
        text(body, 12, 175, 192, "One small robot.\nSix places to call home.", false, INK);
        status = text(body, 12, 221, 192, "", false, BLUE);
        lv_label_set_text_fmt(status, "%u / 6 stamps   |   OFFLINE", orbit_count(&state));
    } else if (state.page == 1) {
        text(body, 12, 41, 192, names[state.station], false, INK);
        if (state.arrived) {
            text(body, 12, 81, 192, "POSTMARK RECEIVED", false, ORANGE);
            text(body, 12, 111, 192, letters[state.station], false, INK);
            text(body, 12, 205, 192, storage_ok ? "Filed in your passport." : "Session only: no storage.", false, BLUE);
        } else {
            mascot = ui_pixel_mascot_create(body, 88, 88);
            status = text(body, 12, 153, 192, state.scanning ? "Listening to the stars..." : "A letter is waiting.", false, INK);
            box(body, 12, 188, 190, 5, PALE);
            meter = box(body, 12, 188, 1, 5, ORANGE);
            text(body, 12, 209, 192, state.scanning ? "Hold OK to cancel." : "Choose a place. Press OK.", false, BLUE);
        }
    } else {
        text(body, 12, 40, 192, "A pocket universe.", true, INK);
        static const char *const short_names[] = {"MOON", "TIDE", "CLOUD", "AMBER", "COMET", "EARTH"};
        for (unsigned i = 0; i < 6; ++i) {
            bool got = state.stamps & (1U << i);
            int x = 12 + (i % 3) * 65, y = 79 + (i / 3) * 61;
            lv_obj_t *stamp = box(body, x, y, 59, 51, got ? INK : PALE);
            text(stamp, 5, 5, 52, got ? "POST" : "- - -", false, got ? PAPER : BLUE);
            text(stamp, 5, 27, 52, short_names[i], false, got ? PAPER : BLUE);
        }
        text(body, 12, 210, 192, orbit_count(&state) == 6 ? "Everywhere, a little home." : "There is more out there.", false, BLUE);
    }
    ESP_LOGI("orbit", "page=%u station=%u stamps=%u", state.page, state.station, orbit_count(&state));
}
static void tick(lv_timer_t *t)
{
    (void)t;
    bool scanning = state.scanning;
    if (orbit_tick(&state, lv_tick_get()) && storage_ok) xQueueOverwrite(saves, &state.stamps);
    if (scanning && !state.scanning) draw();
    if (meter) lv_obj_set_width(meter, 1 + 189 * orbit_progress(&state, lv_tick_get()) / 100);
    ++frame;
    if (mascot && frame % 12 == 0) ui_pixel_mascot_jump(mascot);
    if (frame % 40 == 0) {
        int soc = bsp_battery_soc();
        if (soc >= 0 && soc <= 100) lv_label_set_text_fmt(battery, "%d%%", soc);
        else lv_label_set_text(battery, "--%");
    }
}
void demo_orbit_enter(void)
{
    orbit_init(&state, restored);
    // Keep shared sky, grass and mascot primitives; only the document is new.
    screen = ui_pixel_screen_create("Orbit Post");
    battery = text(screen, 158, 30, 78, "--%", false, PAPER);
    lv_obj_set_style_text_align(battery, LV_TEXT_ALIGN_RIGHT, 0);
    body = ui_pixel_panel_create(screen, 8, 52, 219, 253, PAPER);
    lv_obj_set_style_pad_all(body, 0, 0);
    draw();
    text(screen, 14, 306, 220, "UP/DN  OK  HOLD:BACK", false, INK);
    lv_screen_load(screen);
    frame = 39;
    timer = lv_timer_create(tick, 250, NULL);
    tick(NULL);
    ESP_LOGI("orbit", "Orbital Post ready: 240x320, offline, storage=%d", storage_ok);
}
void demo_orbit_exit(void)
{
    if (timer) { lv_timer_delete(timer); timer = NULL; }
    // saver accesses storage only, never UI; it is owned by the application.
    if (screen) { lv_obj_delete(screen); screen = NULL; }
    body = battery = mascot = status = meter = NULL;
}
void demo_orbit_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!screen || ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_OK) orbit_confirm(&state, lv_tick_get());
    else orbit_move(&state, btn == BSP_BTN_UP ? -1 : 1);
    draw();
}
bool demo_orbit_back(void)
{
    if (screen) { orbit_back(&state); draw(); }
    return true;
}
