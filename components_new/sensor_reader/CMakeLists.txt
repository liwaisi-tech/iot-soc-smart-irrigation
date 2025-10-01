idf_component_register(
    SRCS
        "sensor_reader.c"                           # NUEVO: Implementación principal
        "drivers/dht22/dht.c"                       # DHT22 driver
        "drivers/moisture_sensor/moisture_sensor.c" # Soil moisture sensor driver
    INCLUDE_DIRS "." "drivers/dht22" "drivers/moisture_sensor"
    PRIV_INCLUDE_DIRS "."
    REQUIRES
        esp_adc
        driver
        esp_driver_gpio
        esp_netif           # Para esp_netif_get_handle_from_ifkey()
    PRIV_REQUIRES
        esp_timer
        nvs_flash
)

# Add include path for common_types.h
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/include")
