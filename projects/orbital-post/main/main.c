// Direct-launch Chinese Pocket Universe. Desktop Monitor remains in source.
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "demo.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "main";
typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } key_event_t;
static QueueHandle_t keys;

// Button callbacks run in the button task. Keep the callback non-blocking
// apart from the bounded LVGL lock; the page itself only changes lightweight
// labels and layout objects.
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG) return;
    key_event_t event = {btn, ev};
    if (keys) (void)xQueueSend(keys, &event, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "AI Passport Pocket Universe starting");
    demo_orbit_restore();

    // The monitor uses the shared bus only for the optional battery header.
    // Scanning is diagnostic and never erases NVS or the protected cardid.
    if (bsp_i2c_init() == ESP_OK) (void)bsp_i2c_scan();
    (void)bsp_battery_init();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display/LVGL initialization failed; monitor cannot start"
                      " (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    keys = xQueueCreate(8, sizeof(key_event_t));
    if (!keys) { ESP_LOGE(TAG, "key queue allocation failed"); return; }
    esp_err_t button_err = bsp_button_init(on_key, NULL);
    if (button_err != ESP_OK) {
        ESP_LOGE(TAG, "button initialization failed: %s", esp_err_to_name(button_err));
    }

    if (bsp_lvgl_lock(1000)) {
        demo_orbit_enter();
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "Pocket Universe ready; four offline tools, Chinese UI");
    key_event_t event;
    while (xQueueReceive(keys, &event, portMAX_DELAY) == pdTRUE) {
        if (!bsp_lvgl_lock(100)) continue;
        if (event.button == BSP_BTN_OK && event.event == BSP_BTN_LONG)
            (void)demo_orbit_back();
        else demo_orbit_key(event.button, event.event);
        bsp_lvgl_unlock();
    }
}
