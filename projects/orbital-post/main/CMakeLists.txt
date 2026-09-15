idf_component_register(
    SRCS "main.c" "demo_orbit.c" "orbit_state.c" "ui_pixel.c" "ui_pixel_math.c"
    INCLUDE_DIRS "."
    REQUIRES bsp nvs_flash
)
