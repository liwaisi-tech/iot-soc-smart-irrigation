idf_component_register(
    SRCS
        "irrigation_controller.c"
        "valve_driver.c"
        "safety_logic.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        driver
        esp_event
    PRIV_REQUIRES
        esp_timer
        nvs_flash
)

# Add include path for common_types.h
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/include")
