idf_component_register(
    SRCS
        "sensor_reader.c"
        "drivers/dht22/dht.c"       # DHT22 driver (importado de iot-mqtt-temp-sensor)
        "drivers/moisture_sensor/moisture_sensor.c"  # Soil moisture sensor driver
        "soil_adc_driver.c"
    INCLUDE_DIRS "." "drivers/dht22"  # Agregar path para dht.h
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
