idf_component_register(
    SRCS
        "notification_service.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "."
    REQUIRES
        wifi_manager
    PRIV_REQUIRES
        esp_http_client
        json
)

# Add include path for common_types.h
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/include")
