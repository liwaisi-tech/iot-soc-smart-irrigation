# CMakeLists.txt para el componente soil_moisture_driver
# Smart Irrigation System - ESP32 IoT Project
# Hexagonal Architecture - Infrastructure Layer Driver

idf_component_register(
    SRCS
        "src/soil_moisture_driver.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        # ESP-IDF core components
        driver          # Para ADC, GPIO
        esp_adc         # Para calibración ADC
        esp_timer       # Para timestamps
        esp_common      # Para esp_err_t
        log            # Para ESP_LOG
        freertos       # Para vTaskDelay

        # Domain layer dependencies (value objects)
        domain
    PRIV_REQUIRES
        # Componentes privados (solo para implementación)
        nvs_flash      # Para persistencia futura de calibración
)

# Configuraciones específicas del componente
target_compile_options(${COMPONENT_LIB} PRIVATE
    # Optimizaciones para drivers críticos
    -O2
    # Advertencias adicionales para calidad de código
    -Wall
    -Wextra
    # Suprimir advertencias específicas para arrays de tamaño variable (C99)
    -Wno-vla
)

# Definiciones de preprocesador para el componente
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    # Habilitar logs de debug en development
    SOIL_MOISTURE_DEBUG_ENABLED=1

    # Configuración por defecto de sensores
    SOIL_MOISTURE_DEFAULT_SENSOR_COUNT=3

    # Configuración ADC por defecto
    ADC_ATTEN_DEFAULT=ADC_ATTEN_DB_11
    ADC_BITWIDTH_DEFAULT=ADC_BITWIDTH_DEFAULT
)

# Información del componente para debugging
message(STATUS "Configuring soil_moisture_driver component")
message(STATUS "  - Source files: src/soil_moisture_driver.c")
message(STATUS "  - Include dirs: include/")
message(STATUS "  - Dependencies: driver, esp_adc, esp_timer, domain")
message(STATUS "  - Optimization: -O2 (performance critical)")