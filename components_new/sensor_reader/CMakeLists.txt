idf_component_register(
    SRCS
        "sensor_reader.c"
        "dht22_driver.c"
        "soil_adc_driver.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "."
    REQUIRES
        esp_adc
        driver
        esp_driver_gpio
    PRIV_REQUIRES
        esp_timer
        nvs_flash
)

# Add include path for common_types.h
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/include")
