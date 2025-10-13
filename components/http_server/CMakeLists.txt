idf_component_register(
    SRCS "http_server.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "."
    REQUIRES
        esp_http_server
        json
        esp_event
        sensor_reader      # For sensor_reading_t
        device_config      # For device configuration
        wifi_manager       # For MAC/IP address
    PRIV_REQUIRES
        esp_timer
)

# Add include path for common_types.h
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/include")
