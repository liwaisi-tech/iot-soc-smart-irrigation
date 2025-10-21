idf_component_register(
    SRCS
        "irrigation_controller.c"
        "drivers/valve_driver/valve_driver.c"
        "drivers/safety_watchdog/safety_watchdog.c"
    INCLUDE_DIRS "." "drivers/valve_driver" "drivers/safety_watchdog"
    PRIV_INCLUDE_DIRS "." "drivers/valve_driver" "drivers/safety_watchdog"
    REQUIRES
        sensor_reader
        wifi_manager
        mqtt_client
        device_config
        driver
        esp_driver_gpio
    PRIV_REQUIRES
        esp_timer
        nvs_flash
        esp_http_client
)

# Add include path for common_types.h
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/include")
