idf_component_register(
    SRCS "mqtt_client.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "."
    REQUIRES
        mqtt
        esp_event
        json
        wifi_manager      # WiFi manager for IP obtained events
        device_config     # Device configuration for NVS
        sensor_reader     # Sensor reading types
    PRIV_REQUIRES
        esp_timer
        nvs_flash
)

# Add include path for common_types.h
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/include")
