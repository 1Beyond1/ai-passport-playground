// Pocket Universe: a Chinese-first offline collection for three buttons.
#include "demo.h"
#include "orbit_state.h"
#include "pocket_state.h"
#include "ui_pixel.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>

#define PAPER 0xF6F1E4
#define INK 0x17364D
#define BLUE 0x4F7890
#define ORANGE 0xE97843
#define PALE 0xD6E2DE
#define GREEN 0x3FAE70
#define RED 0xDB5B52
#define SCREEN_OFF_MS 90000U

LV_FONT_DECLARE(font_pocket_16);
LV_FONT_DECLARE(font_pocket_28);

static const char *const app_names[] = {"星际邮局", "心情挂牌", "随机器", "反应测试"};
static const char *const app_notes[] = {
    "收集六封星际来信", "告诉别人现在的状态", "硬币、骰子和答案", "看看你的手有多快"
};
static const char *const station_names[] = {
    "月光花园", "潮汐书馆", "云朵港", "琥珀沙漠", "沉睡彗星", "地球家园"
};
static const char *const letters[] = {
    "花园为你留了\n一点月光。\n今晚记得看窗外。",
    "一本空白的书\n来到岸边。\n它记得大海。",
    "你的包裹到了。\n里面是一朵云，\n请不要折叠。",
    "这里什么都没有。\n然后你看见了\n一朵很小的花。",
    "彗星正在睡觉。\n把烦恼留在这里，\n它会帮你带走。",
    "没有特别任务。\n有人为你留着灯。\n欢迎回家。"
};
static const char *const moods[] = {
    "开开心心", "正在摸鱼", "专注中", "有点累", "请勿打扰", "游戏中"
};
static const char *const mood_notes[] = {
    "今天很好说话", "人在这里，心在放假", "有事稍后说",
    "需要一点安静", "看到也先别叫我", "这一局很重要"
};
static const uint32_t mood_colors[] = {
    0xF2B84B, 0x65B9C7, 0x5286C5, 0x8B78B8, 0xDB655D, 0x61A66A
};
static const char *const random_names[] = {"抛硬币", "掷骰子", "帮我决定"};
static const char *const coin_results[] = {"正面", "反面"};
static const char *const answer_results[] = {
    "去做吧", "别冲动", "可以试试", "稍后再说", "都可以", "再想想"
};
static const char *const stamp_names[] = {"月光", "潮汐", "云港", "琥珀", "彗星", "家园"};

static pocket_state_t pocket;
static orbit_state_t post;
static lv_obj_t *screen, *body, *battery, *mascot, *meter;
static lv_timer_t *timer;
static QueueHandle_t saves;
static TaskHandle_t saver;
static TickType_t last_activity;
static uint8_t restored;
static unsigned frame;
static bool storage_ok, screen_awake;

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
        if (err != ESP_OK) ESP_LOGW("pocket", "stamp save failed: %s", esp_err_to_name(err));
    }
}

void demo_orbit_restore(void)
{
    restored = 0;
    storage_ok = nvs_flash_init() == ESP_OK;
    if (!storage_ok) return;
    nvs_handle_t handle;
    if (nvs_open("orbit_post", NVS_READONLY, &handle) == ESP_OK) {
        (void)nvs_get_u8(handle, "stamps", &restored);
        nvs_close(handle);
    }
    saves = xQueueCreate(1, sizeof(uint8_t));
    if (!saves || xTaskCreate(save_worker, "pocket_save", 3072, NULL, 2, &saver) != pdPASS)
        storage_ok = false;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int width, int height, uint32_t color)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    return object;
}

static lv_obj_t *text(lv_obj_t *parent, int x, int y, int width, const char *value,
                      const lv_font_t *font, uint32_t color, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, value);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_text_line_space(label, 4, 0);
    return label;
}

static void header(const char *title)
{
    text(body, 12, 8, 190, title, &font_pocket_16, BLUE, LV_TEXT_ALIGN_LEFT);
    box(body, 12, 31, 190, 2, INK);
}

static void footer(const char *hint)
{
    box(body, 0, 225, 219, 1, PALE);
    text(body, 8, 229, 203, hint, &font_pocket_16, BLUE, LV_TEXT_ALIGN_CENTER);
}

static void draw_home(void)
{
    header("我的小宇宙");
    text(body, 12, 39, 190, "选一个现在想玩的", &font_pocket_16, INK, LV_TEXT_ALIGN_LEFT);
    for (unsigned i = 0; i < POCKET_APP_COUNT; ++i) {
        int x = 12 + (i % 2) * 99;
        int y = 68 + (i / 2) * 72;
        bool selected = pocket.selection == i;
        lv_obj_t *card = box(body, x, y, 91, 62, selected ? ORANGE : PALE);
        lv_obj_set_style_border_width(card, selected ? 3 : 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(selected ? INK : BLUE), 0);
        text(card, 5, 7, 81, app_names[i], &font_pocket_16,
             selected ? PAPER : INK, LV_TEXT_ALIGN_CENTER);
        text(card, 5, 32, 81, i == 0 ? "来信" : i == 1 ? "状态" : i == 2 ? "决定" : "速度",
             &font_pocket_16, selected ? PAPER : BLUE, LV_TEXT_ALIGN_CENTER);
    }
    text(body, 12, 204, 190, app_notes[pocket.selection], &font_pocket_16,
         INK, LV_TEXT_ALIGN_CENTER);
    footer("上下选择  确认进入");
}

static void draw_post(void)
{
    header(post.page == 2 ? "我的邮戳" : "星际邮局");
    if (post.page == 2) {
        text(body, 12, 40, 190, "去过的地方", &font_pocket_16, INK, LV_TEXT_ALIGN_LEFT);
        for (unsigned i = 0; i < ORBIT_STATIONS; ++i) {
            bool received = post.stamps & (1U << i);
            int x = 12 + (i % 3) * 65;
            int y = 69 + (i / 3) * 65;
            lv_obj_t *stamp = box(body, x, y, 58, 55, received ? INK : PALE);
            text(stamp, 3, 8, 52, received ? "已收到" : "还没去", &font_pocket_16,
                 received ? PAPER : BLUE, LV_TEXT_ALIGN_CENTER);
            text(stamp, 3, 32, 52, stamp_names[i], &font_pocket_16,
                 received ? PAPER : BLUE, LV_TEXT_ALIGN_CENTER);
        }
        footer("确认继续收信  长按返回");
        return;
    }

    text(body, 12, 40, 190, station_names[post.station], &font_pocket_28,
         INK, LV_TEXT_ALIGN_CENTER);
    if (post.arrived) {
        text(body, 12, 77, 190, "收到一封来信", &font_pocket_16, ORANGE, LV_TEXT_ALIGN_CENTER);
        text(body, 14, 106, 186, letters[post.station], &font_pocket_16,
             INK, LV_TEXT_ALIGN_CENTER);
        text(body, 12, 199, 190, storage_ok ? "邮戳已经保存" : "本次临时保存", &font_pocket_16,
             BLUE, LV_TEXT_ALIGN_CENTER);
        footer("确认查看邮戳  长按返回");
    } else {
        mascot = ui_pixel_mascot_create(body, 89, 88);
        text(body, 12, 146, 190, post.scanning ? "正在穿过星空……" : "这里有一封信等你",
             &font_pocket_16, INK, LV_TEXT_ALIGN_CENTER);
        box(body, 14, 180, 186, 6, PALE);
        meter = box(body, 14, 180, 1, 6, ORANGE);
        text(body, 12, 198, 190, post.scanning ? "长按可以取消" : "上下换地点，确认收信",
             &font_pocket_16, BLUE, LV_TEXT_ALIGN_CENTER);
        footer("上下换地点  确认收信");
    }
}

static void draw_sign(void)
{
    uint32_t color = mood_colors[pocket.mood];
    box(body, 0, 0, 219, 253, color);
    text(body, 10, 17, 199, "我的状态", &font_pocket_16, PAPER, LV_TEXT_ALIGN_CENTER);
    box(body, 24, 51, 171, 2, PAPER);
    text(body, 10, 74, 199, moods[pocket.mood], &font_pocket_28, PAPER, LV_TEXT_ALIGN_CENTER);
    text(body, 20, 128, 179, mood_notes[pocket.mood], &font_pocket_16,
         PAPER, LV_TEXT_ALIGN_CENTER);
    mascot = ui_pixel_mascot_create(body, 90, 164);
    text(body, 8, 229, 203, "上下换状态  长按返回", &font_pocket_16,
         PAPER, LV_TEXT_ALIGN_CENTER);
}

static void draw_random(void)
{
    header("随机器");
    text(body, 12, 42, 190, random_names[pocket.random_kind], &font_pocket_28,
         INK, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *result = box(body, 21, 87, 177, 95, pocket.random_ready ? ORANGE : PALE);
    if (!pocket.random_ready) {
        text(result, 8, 30, 161, "按确认试试", &font_pocket_28, BLUE, LV_TEXT_ALIGN_CENTER);
    } else if (pocket.random_kind == 0) {
        text(result, 8, 30, 161, coin_results[pocket.random_value], &font_pocket_28,
             PAPER, LV_TEXT_ALIGN_CENTER);
    } else if (pocket.random_kind == 1) {
        char number[4];
        snprintf(number, sizeof(number), "%u", pocket.random_value + 1);
        text(result, 8, 25, 161, number, &lv_font_montserrat_20, PAPER, LV_TEXT_ALIGN_CENTER);
    } else {
        text(result, 8, 30, 161, answer_results[pocket.random_value], &font_pocket_28,
             PAPER, LV_TEXT_ALIGN_CENTER);
    }
    text(body, 12, 195, 190, "每次确认都会重新随机", &font_pocket_16,
         BLUE, LV_TEXT_ALIGN_CENTER);
    footer("上下换玩法  确认随机");
}

static void draw_reaction(void)
{
    uint32_t color = pocket.reaction == POCKET_REACTION_GO ? GREEN :
                     pocket.reaction == POCKET_REACTION_EARLY ? RED : PAPER;
    box(body, 0, 0, 219, 253, color);
    bool pale_ink = pocket.reaction == POCKET_REACTION_GO ||
                    pocket.reaction == POCKET_REACTION_EARLY;
    text(body, 10, 18, 199, "反应测试", &font_pocket_16,
         pale_ink ? PAPER : BLUE, LV_TEXT_ALIGN_CENTER);
    const char *main_text = "按确认开始";
    const char *note = "变成绿色时立刻按确认";
    char result[24];
    const lv_font_t *font = &font_pocket_28;
    if (pocket.reaction == POCKET_REACTION_WAIT) {
        main_text = "先别按"; note = "等它变成绿色";
    } else if (pocket.reaction == POCKET_REACTION_GO) {
        main_text = "快按"; note = "现在按确认";
    } else if (pocket.reaction == POCKET_REACTION_EARLY) {
        main_text = "太早了"; note = "确认再试一次";
    } else if (pocket.reaction == POCKET_REACTION_RESULT) {
        snprintf(result, sizeof(result), "%lu 毫秒", (unsigned long)pocket.reaction_ms);
        main_text = result;
        note = pocket.reaction_ms < 220 ? "真的很快" :
               pocket.reaction_ms < 350 ? "反应不错" : "再练一次";
        font = &font_pocket_16;
    }
    text(body, 10, 77, 199, main_text, font, pale_ink ? PAPER : INK, LV_TEXT_ALIGN_CENTER);
    text(body, 15, 130, 189, note, &font_pocket_16,
         pale_ink ? PAPER : INK, LV_TEXT_ALIGN_CENTER);
    if (pocket.reaction == POCKET_REACTION_IDLE || pocket.reaction == POCKET_REACTION_RESULT)
        mascot = ui_pixel_mascot_create(body, 90, 165);
    text(body, 8, 229, 203, "确认操作  长按返回", &font_pocket_16,
         pale_ink ? PAPER : BLUE, LV_TEXT_ALIGN_CENTER);
}

static void draw(void)
{
    lv_obj_clean(body);
    mascot = meter = NULL;
    if (pocket.screen == POCKET_SCREEN_HOME) draw_home();
    else if (pocket.screen == POCKET_SCREEN_POST) draw_post();
    else if (pocket.screen == POCKET_SCREEN_SIGN) draw_sign();
    else if (pocket.screen == POCKET_SCREEN_RANDOM) draw_random();
    else draw_reaction();
}

static void tick(lv_timer_t *timer_arg)
{
    (void)timer_arg;
    uint32_t now = lv_tick_get();
    bool redraw = pocket_tick(&pocket, now);
    if (pocket.screen == POCKET_SCREEN_POST) {
        bool was_scanning = post.scanning;
        if (orbit_tick(&post, now) && storage_ok) xQueueOverwrite(saves, &post.stamps);
        if (was_scanning && !post.scanning) redraw = true;
        if (meter) lv_obj_set_width(meter, 1 + 185 * orbit_progress(&post, now) / 100);
    }
    if (redraw) draw();
    ++frame;
    if (mascot && frame % 12 == 0) ui_pixel_mascot_jump(mascot);
    if (frame % 100 == 0) {
        int soc = bsp_battery_soc();
        if (soc >= 0 && soc <= 100) lv_label_set_text_fmt(battery, "电量 %d%%", soc);
        else lv_label_set_text(battery, "电量 --");
    }
    if (screen_awake && xTaskGetTickCount() - last_activity >= pdMS_TO_TICKS(SCREEN_OFF_MS)) {
        bsp_display_backlight(0);
        screen_awake = false;
    }
}

void demo_orbit_enter(void)
{
    pocket_init(&pocket);
    orbit_init(&post, restored);
    screen = ui_pixel_screen_create("");
    text(screen, 14, 14, 140, "小宇宙", &font_pocket_16, UI_INK, LV_TEXT_ALIGN_CENTER);
    battery = text(screen, 158, 31, 76, "电量 --", &font_pocket_16, PAPER, LV_TEXT_ALIGN_RIGHT);
    body = ui_pixel_panel_create(screen, 8, 52, 219, 253, PAPER);
    lv_obj_set_style_pad_all(body, 0, 0);
    draw();
    lv_screen_load(screen);
    frame = 99;
    screen_awake = true;
    last_activity = xTaskGetTickCount();
    bsp_display_backlight(100);
    timer = lv_timer_create(tick, 100, NULL);
    tick(NULL);
    ESP_LOGI("pocket", "Pocket Universe ready: Chinese UI, four offline apps");
}

void demo_orbit_exit(void)
{
    if (timer) { lv_timer_delete(timer); timer = NULL; }
    if (screen) { lv_obj_delete(screen); screen = NULL; }
    body = battery = mascot = meter = NULL;
}

void demo_orbit_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (!screen || event != BSP_BTN_CLICK) return;
    last_activity = xTaskGetTickCount();
    if (!screen_awake) {
        screen_awake = true;
        bsp_display_backlight(100);
        return;
    }
    if (pocket.screen == POCKET_SCREEN_POST) {
        if (post.page == 2) {
            if (button == BSP_BTN_OK) post.page = 1;
        } else if (button == BSP_BTN_OK) {
            orbit_confirm(&post, lv_tick_get());
        } else {
            orbit_move(&post, button == BSP_BTN_UP ? -1 : 1);
        }
    } else if (button == BSP_BTN_OK) {
        pocket_confirm(&pocket, lv_tick_get(), esp_random());
        if (pocket.screen == POCKET_SCREEN_POST) post.page = 1;
    } else {
        pocket_move(&pocket, button == BSP_BTN_UP ? -1 : 1);
    }
    draw();
}

bool demo_orbit_back(void)
{
    last_activity = xTaskGetTickCount();
    if (!screen_awake) {
        screen_awake = true;
        bsp_display_backlight(100);
        return true;
    }
    post.scanning = false;
    post.arrived = false;
    post.page = 1;
    pocket_home(&pocket);
    draw();
    return true;
}
