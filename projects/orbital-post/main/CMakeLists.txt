idf_component_register(
    SRCS "main.c" "demo_orbit.c" "orbit_state.c" "pocket_state.c"
         "font_pocket_16.c" "font_pocket_28.c" "ui_pixel.c" "ui_pixel_math.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support nvs_flash
)
