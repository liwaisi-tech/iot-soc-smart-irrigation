idf_component_register(
    SRCS "iot-soc-smart-irrigation.c"
    INCLUDE_DIRS "."
    REQUIRES
        infrastructure       # Arquitectura hexagonal (temporal)
        sensor_reader       # NUEVO: Componente unificado de sensores
        device_config       # NUEVO: Componente de configuración (migrado)
        nvs_flash
        esp_event
        esp_system
        esp_netif           # Para tipos de red en sensor_reader
        freertos
        esp_http_server
)

# Agregar include para common_types.h
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/include")
