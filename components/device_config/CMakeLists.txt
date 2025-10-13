idf_component_register(
    SRCS "device_config.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
    PRIV_REQUIRES
        esp_netif
)

# Add include path for common_types.h
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/include")
