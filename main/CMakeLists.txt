idf_component_register(
    SRCS "iot-soc-smart-irrigation.c"
    INCLUDE_DIRS "."
    REQUIRES
        # Component-Based Architecture (MIGRATED)
        wifi_manager        # Migrated from wifi_adapter
        http_server         # Migrated from http_adapter
        mqtt_client         # Migrated from mqtt_adapter
        sensor_reader       # Migrated component - unified sensor interface
        device_config       # Migrated component - configuration management
        irrigation_controller # Phase 5 - Irrigation control logic

        # ESP-IDF components
        nvs_flash
        esp_event
        esp_system
        esp_netif
        freertos
        esp_http_server
)

# Agregar include para common_types.h
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/include")
