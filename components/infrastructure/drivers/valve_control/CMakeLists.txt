# CMakeLists.txt para el componente valve_control_driver
# Smart Irrigation System - ESP32 IoT Project
# Hexagonal Architecture - Infrastructure Layer Driver

idf_component_register(
    SRCS
        "src/valve_control_driver.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        # ESP-IDF core components
        driver          # Para GPIO control
        esp_timer       # Para timestamps y timers
        esp_common      # Para esp_err_t
        log            # Para ESP_LOG
        freertos       # Para vTaskDelay y timers

        # Domain layer dependencies (value objects)
        domain
    PRIV_REQUIRES
        # Componentes privados (solo para implementación)
        nvs_flash      # Para persistencia futura de estado válvulas
)

# Configuraciones específicas del componente
target_compile_options(${COMPONENT_LIB} PRIVATE
    # Optimizaciones para drivers de hardware crítico
    -O2
    # Advertencias adicionales para calidad de código
    -Wall
    -Wextra
    # Configuraciones específicas para GPIO control
    -fno-strict-aliasing
)

# Definiciones de preprocesador para el componente
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    # Habilitar logs de debug en development
    VALVE_CONTROL_DEBUG_ENABLED=1

    # Configuración de sistema simplificado
    VALVE_CONTROL_SIMPLIFIED_SYSTEM=1
    VALVE_CONTROL_MAX_VALVES=1

    # Configuración de safety por defecto
    VALVE_CONTROL_DEFAULT_MAX_DURATION_MS=1800000  # 30 minutos
    VALVE_CONTROL_DEFAULT_TIMEOUT_MS=3000          # 3 segundos

    # Configuración de GPIO por defecto
    VALVE_CONTROL_DEFAULT_GPIO_LEVEL_CLOSED=0      # LOW = cerrado
    VALVE_CONTROL_DEFAULT_GPIO_LEVEL_OPEN=1        # HIGH = abierto
)

# Información del componente para debugging
message(STATUS "Configuring valve_control_driver component")
message(STATUS "  - Source files: src/valve_control_driver.c")
message(STATUS "  - Include dirs: include/")
message(STATUS "  - Dependencies: driver, esp_timer, freertos, domain")
message(STATUS "  - System: Simplified (1 valve)")
message(STATUS "  - Safety: 30min max duration, 3s timeout")
message(STATUS "  - Optimization: -O2 (hardware critical)")