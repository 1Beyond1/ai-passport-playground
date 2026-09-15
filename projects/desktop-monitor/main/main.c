// Direct-launch Desktop Monitor application for the FoloToy AI Passport.
// The USB Serial/JTAG console receives PCSTAT records; the three buttons
// navigate the on-device pages without requiring a menu or audio stack.
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "demo.h"

#include "esp_log.h"

static const char *TAG = "main";

// Button callbacks run in the button task. Keep the callback non-blocking
// apart from the bounded LVGL lock; the page itself only changes lightweight
// labels and layout objects.
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!bsp_lvgl_lock(500)) return;

    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        (void)demo_desktop_back();
    } else {
        demo_desktop_key(btn, ev);
    }
    bsp_lvgl_unlock();
}

void app_main(void)
{
    ESP_LOGI(TAG, "AI Passport Desktop Monitor starting");

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

    esp_err_t button_err = bsp_button_init(on_key, NULL);
    if (button_err != ESP_OK) {
        ESP_LOGE(TAG, "button initialization failed: %s", esp_err_to_name(button_err));
    }

    if (bsp_lvgl_lock(1000)) {
        demo_desktop_enter();
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "Desktop Monitor ready; send PCSTAT lines over USB Serial/JTAG");
}
