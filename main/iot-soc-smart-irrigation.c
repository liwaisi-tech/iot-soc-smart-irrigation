// main/main.c
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>

#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Headers de la aplicación - Arquitectura Hexagonal
#include "wifi_adapter.h"
#include "http_adapter.h"
#include "device_config_service.h"
#include "shared_resource_manager.h"
#include "dht_sensor.h"
// TODO: Uncomment when implementations are created
// #include "use_cases/device_registration.h"
// #include "use_cases/read_sensors.h"
// #include "use_cases/control_irrigation.h"
#include "mqtt_adapter.h"
#include "publish_sensor_data.h"
#include "process_mqtt_commands.h"

// Fase 4 - Control de Riego - Headers
#include "control_irrigation.h"
#include "valve_control_driver.h"
#include "soil_moisture_driver.h"
#include "irrigation_logic.h"
#include "safety_manager.h"
#include "offline_mode_logic.h"
#include "read_soil_sensors.h"

static const char *TAG = "SMART_IRRIGATION_MAIN";
static bool s_http_adapter_initialized = false;

// Fase 4 - Control de Riego - Global state
static control_irrigation_context_t g_irrigation_context;
static bool s_irrigation_control_initialized = false;

// Task configuration constants
#define SENSOR_PUBLISH_TASK_STACK_SIZE    4096
#define SENSOR_PUBLISH_TASK_PRIORITY      3  // Reduced from 5 to 3 - avoid priority inversion with HTTP/WiFi tasks
#define SENSOR_PUBLISH_INTERVAL_MS        30000  // 30 seconds

// Fase 4 - Control de Riego - Task configuration
#define IRRIGATION_CONTROL_TASK_STACK_SIZE    8192
#define IRRIGATION_CONTROL_TASK_PRIORITY      4
#define IRRIGATION_EVALUATION_INTERVAL_MS     30000  // 30 seconds

// GPIO Configuration for Phase 4
#define VALVE_1_GPIO_PIN    GPIO_NUM_2   // Válvula principal
#define VALVE_2_GPIO_PIN    GPIO_NUM_15  // Válvula secundaria (reservada)
#define VALVE_3_GPIO_PIN    GPIO_NUM_16  // Válvula terciaria (reservada)

#define SOIL_SENSOR_1_ADC_CHANNEL    ADC1_CHANNEL_0  // GPIO 36
#define SOIL_SENSOR_2_ADC_CHANNEL    ADC1_CHANNEL_3  // GPIO 39
#define SOIL_SENSOR_3_ADC_CHANNEL    ADC1_CHANNEL_6  // GPIO 34

/**
 * @brief Sensor publishing task
 *
 * Periodically reads sensor data and publishes it via MQTT.
 * Uses vTaskDelayUntil() for precise 30-second intervals.
 * Handles failures gracefully by continuing the publishing loop.
 * Implements anti-deadlock measures for WiFi provisioning compatibility.
 */
static void sensor_publishing_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting sensor publishing task (Core 1, Priority 3)");

    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(SENSOR_PUBLISH_INTERVAL_MS);
    uint32_t cycle_count = 0;

    // Initialize xLastWakeTime with current time
    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        // Wait for the next cycle
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        cycle_count++;

        // Execute the sensor publishing use case
        esp_err_t ret = publish_sensor_data_use_case();
        if (ret != ESP_OK) {
            // Reduced logging frequency to avoid deadlocks during WiFi provisioning
            if (cycle_count % 10 == 0) {  // Log only every 10th cycle if errors persist
                ESP_LOGW(TAG, "Sensor cycle %" PRIu32 " failed: %s",
                         cycle_count, esp_err_to_name(ret));
            }
        }

        // Log memory status less frequently to reduce logging contention
        if (cycle_count % 5 == 0) {  // Log every 5th cycle (every 2.5 minutes)
            ESP_LOGI(TAG, "Sensor cycle %" PRIu32 " completed - Free heap: %" PRIu32 " bytes",
                    cycle_count, esp_get_free_heap_size());
        }
    }
}

/**
 * @brief Manejador de eventos WiFi para la aplicación principal
 */
static void main_wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_ADAPTER_EVENTS) {
        switch (event_id) {
            case WIFI_ADAPTER_EVENT_INIT_COMPLETE:
                ESP_LOGI(TAG, "WiFi adapter inicializado");
                break;
                
            case WIFI_ADAPTER_EVENT_PROVISIONING_STARTED:
                ESP_LOGI(TAG, "Modo de aprovisionamiento WiFi iniciado");
                ESP_LOGI(TAG, "Conectar a red 'Liwaisi-Config' para configurar WiFi");
                break;
                
            case WIFI_ADAPTER_EVENT_PROVISIONING_COMPLETED:
                ESP_LOGI(TAG, "Aprovisionamiento WiFi completado");
                break;

            case WIFI_ADAPTER_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Conexión WiFi establecida");
                break;

            case WIFI_ADAPTER_EVENT_IP_OBTAINED: {
                esp_ip4_addr_t ip;
                if (wifi_adapter_get_ip(&ip) == ESP_OK) {
                    ESP_LOGI(TAG, "Dirección IP obtenida: " IPSTR, IP2STR(&ip));
                }

                // Initialize HTTP adapter when IP is obtained (both after provisioning and normal connections)
                if (!s_http_adapter_initialized) {
                    ESP_LOGI(TAG, "Inicializando adaptador HTTP tras obtener IP...");
                    // Small delay to ensure any provisioning server is fully stopped
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_err_t ret = http_adapter_init();
                    if (ret == ESP_OK) {
                        s_http_adapter_initialized = true;
                        ESP_LOGI(TAG, "Adaptador HTTP inicializado correctamente en IP: " IPSTR, IP2STR(&ip));
                    } else {
                        ESP_LOGE(TAG, "Error al inicializar adaptador HTTP: %s", esp_err_to_name(ret));
                    }
                }
                break;
            }
            
            case WIFI_ADAPTER_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "Conexión WiFi perdida - reintentando...");
                break;
                
            case WIFI_ADAPTER_EVENT_RESET_REQUESTED:
                ESP_LOGW(TAG, "Patrón de reinicio detectado - iniciando aprovisionamiento");
                break;
                
            case WIFI_ADAPTER_EVENT_CONNECTION_FAILED:
                ESP_LOGE(TAG, "Fallo en conexión WiFi");
                break;
                
            default:
                break;
        }
    }
}

/**
 * @brief Fase 4 - Tarea de control de riego
 *
 * Ejecuta la evaluación automática del sistema de riego cada 30 segundos.
 * Lee sensores, aplica lógica de decisión y controla válvulas según sea necesario.
 */
static void irrigation_control_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting irrigation control task (Core 1, Priority 4)");

    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(IRRIGATION_EVALUATION_INTERVAL_MS);
    uint32_t cycle_count = 0;

    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        cycle_count++;

        if (!s_irrigation_control_initialized) {
            ESP_LOGW(TAG, "Irrigation control not initialized - skipping cycle %" PRIu32, cycle_count);
            continue;
        }

        control_irrigation_operation_result_t result = 
            control_irrigation_execute_automatic_evaluation(&g_irrigation_context);

        if (result.result_code != CONTROL_IRRIGATION_SUCCESS && 
            result.result_code != CONTROL_IRRIGATION_NO_ACTION) {
            ESP_LOGW(TAG, "Irrigation cycle %" PRIu32 " result: %s - %s",
                     cycle_count, 
                     control_irrigation_result_to_string(result.result_code),
                     result.description);
        }

        if (cycle_count % 10 == 0) {
            uint32_t total_eval, successful_ops, failed_ops;
            control_irrigation_get_statistics(&g_irrigation_context, &total_eval, &successful_ops, &failed_ops);
            
            ESP_LOGI(TAG, "Irrigation cycle %" PRIu32 " - Eval: %" PRIu32 ", Success: %" PRIu32 ", Failed: %" PRIu32,
                     cycle_count, total_eval, successful_ops, failed_ops);
        }
    }
}

/**
 * @brief Fase 4 - Inicializar sistema de control de riego
 *
 * Inicializa todos los componentes de la Fase 4: drivers, servicios de dominio,
 * casos de uso y configuración de seguridad.
 */
static esp_err_t initialize_irrigation_control_system(void)
{
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "FASE 4 - Inicializando Sistema de Control de Riego");
    ESP_LOGI(TAG, "==============================================");

    // 1. Configurar límites de seguridad por defecto
    safety_limits_t default_safety_limits = {
        .soil_humidity = {
            .critical_low_percent = 30.0f,      // Emergencia - riego inmediato
            .warning_low_percent = 45.0f,       // Advertencia - programar riego
            .target_min_percent = 70.0f,        // Objetivo mínimo
            .target_max_percent = 75.0f,        // Objetivo máximo - PARAR
            .emergency_high_percent = 80.0f     // Peligro - PARADA EMERGENCIA
        },
        .temperature = {
            .night_irrigation_max = 32.0f,      // Máx temp para riego nocturno
            .thermal_stress_min = 32.0f,        // Mín temp para estrés térmico
            .emergency_stop_max = 40.0f,        // Parada por temperatura
            .optimal_range_min = 26.0f,         // Rango óptimo
            .optimal_range_max = 30.0f
        },
        .duration = {
            .min_session_minutes = 2,           // Mínimo 2 minutos
            .default_duration_minutes = 15,     // Por defecto 15 minutos
            .max_session_minutes = 30,          // Máximo 30 minutos
            .emergency_duration_minutes = 5     // Emergencia 5 minutos
        }
    };

    // 2. Inicializar drivers de hardware
    ESP_LOGI(TAG, "Inicializando drivers de hardware...");

    // Driver de control de válvulas
    valve_control_config_t valve_config = {
        .valve_response_timeout_ms = 5000,
        .relay_settling_time_ms = 100,
        .interlock_delay_ms = 200,
        .max_operation_duration_ms = 1800000, // 30 minutos
        .enable_interlock_protection = true,
        .enable_timeout_protection = true,
        .enable_state_monitoring = true,
        .monitoring_interval_ms = 1000,
        .enable_power_optimization = true,
        .relay_hold_reduction_delay_ms = 5000
    };

    esp_err_t ret = valve_control_driver_init(&valve_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al inicializar driver de válvulas: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Driver de válvulas inicializado correctamente");

    // Driver de sensores de suelo
    soil_moisture_driver_config_t soil_config = {
        .adc_attenuation = ADC_ATTEN_DB_11,
        .adc_width = ADC_WIDTH_BIT_12,
        .samples_per_reading = 5,
        .sample_interval_ms = 50,
        .enable_median_filter = true,
        .enable_average_filter = true,
        .enable_outlier_detection = true,
        .outlier_threshold_std_dev = 2.0f,
        .enable_auto_calibration = true,
        .auto_calibration_interval_s = 86400, // 24 horas
        .calibration_drift_threshold = 5.0f,
        .enable_temperature_compensation = true,
        .default_temperature_celsius = 25.0f,
        .sensor_timeout_ms = 2000,
        .max_consecutive_failures = 5
    };

    ret = soil_moisture_driver_init(&soil_config, 3); // 3 sensores
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al inicializar driver de sensores de suelo: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Driver de sensores de suelo inicializado correctamente");

    // 3. Inicializar servicios de dominio
    ESP_LOGI(TAG, "Inicializando servicios de dominio...");

    ret = safety_manager_init(&default_safety_limits);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al inicializar safety manager: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Safety manager inicializado correctamente");

    ret = irrigation_logic_init(&default_safety_limits);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al inicializar irrigation logic: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Irrigation logic inicializado correctamente");

    ret = offline_mode_logic_init(&default_safety_limits);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al inicializar offline mode logic: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Offline mode logic inicializado correctamente");

    // 4. Inicializar caso de uso principal
    ESP_LOGI(TAG, "Inicializando caso de uso de control de riego...");

    ret = control_irrigation_init_context(&g_irrigation_context, "AA:BB:CC:DD:EE:FF", &default_safety_limits);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al inicializar contexto de control de riego: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configurar adapters (simplificado para esta implementación)
    ret = control_irrigation_set_hardware_adapter(&g_irrigation_context, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al configurar hardware adapter: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = control_irrigation_set_mqtt_adapter(&g_irrigation_context, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al configurar MQTT adapter: %s", esp_err_to_name(ret));
        return ret;
    }

    // Marcar como inicializado
    g_irrigation_context.state.is_initialized = true;
    s_irrigation_control_initialized = true;

    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "FASE 4 - Sistema de Control de Riego Inicializado");
    ESP_LOGI(TAG, "Válvulas: GPIO %d, %d, %d", VALVE_1_GPIO_PIN, VALVE_2_GPIO_PIN, VALVE_3_GPIO_PIN);
    ESP_LOGI(TAG, "Sensores suelo: ADC %d, %d, %d", SOIL_SENSOR_1_ADC_CHANNEL, SOIL_SENSOR_2_ADC_CHANNEL, SOIL_SENSOR_3_ADC_CHANNEL);
    ESP_LOGI(TAG, "==============================================");

    return ESP_OK;
}

/**
 * @brief Punto de entrada principal de la aplicación
 * 
 * Inicializa todos los componentes del sistema siguiendo los principios
 * de arquitectura hexagonal, donde el dominio es independiente de la
 * infraestructura.
 */
void app_main(void)
{
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "Liwaisi Tech - https://liwaisi.tech");
    ESP_LOGI(TAG, "Iniciando Sistema de Riego Inteligente v1.0.0");
    ESP_LOGI(TAG, "Compilado: %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "==============================================");


    // 1. Inicialización del sistema base ESP-IDF
    ESP_LOGI(TAG, "Inicializando sistema base ESP-IDF...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Inicializar sistema de recursos compartidos
    ESP_LOGI(TAG, "Inicializando sistema de recursos compartidos...");
    ESP_ERROR_CHECK(shared_resource_manager_init());
    
    // Inicializar sensor DHT22 en GPIO_NUM_4 (pin seguro, no bootstrap)
    ESP_LOGI(TAG, "Inicializando sensor DHT22 en GPIO 4...");
    ret = dht_sensor_init(GPIO_NUM_4);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al inicializar sensor DHT22: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "El sistema continuará sin sensor de temperatura/humedad");
    } else {
        ESP_LOGI(TAG, "Sensor DHT22 inicializado correctamente en GPIO 4");
    }
    
    // Inicializar servicio de configuración del dispositivo
    ESP_LOGI(TAG, "Inicializando servicio de configuración del dispositivo...");
    ESP_ERROR_CHECK(device_config_service_init());

    // 2. Inicialización de la capa de infraestructura
    ESP_LOGI(TAG, "Inicializando capa de Infraestructura...");
    
    // Registrar manejador de eventos WiFi
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_ADAPTER_EVENTS, ESP_EVENT_ANY_ID, &main_wifi_event_handler, NULL));
    
    // Inicializar y arrancar adaptador WiFi
    ESP_LOGI(TAG, "Inicializando adaptador WiFi...");
    ESP_ERROR_CHECK(wifi_adapter_init());
    ESP_ERROR_CHECK(wifi_adapter_start());
    
    // Mostrar estado del dispositivo
    wifi_adapter_status_t wifi_status;
    if (wifi_adapter_get_status(&wifi_status) == ESP_OK) {
        ESP_LOGI(TAG, "Estado WiFi: %s", 
                 wifi_status.provisioned ? "Aprovisionado" : "No aprovisionado");
        
        uint8_t mac[6];
        if (wifi_adapter_get_mac(mac) == ESP_OK) {
            ESP_LOGI(TAG, "Dirección MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    }
    
    // DO NOT initialize HTTP adapter here - it conflicts with provisioning server
    // HTTP adapter will be initialized after WiFi connection is established
    ESP_LOGI(TAG, "HTTP adapter initialization deferred until WiFi connection");
    
    // Inicializar adaptador MQTT después de HTTP
    ESP_LOGI(TAG, "Inicializando adaptador MQTT...");
    ESP_ERROR_CHECK(mqtt_adapter_init());

    // Inicializar procesador de comandos MQTT
    ESP_LOGI(TAG, "Inicializando procesador de comandos MQTT...");
    // TODO: Re-enable once process_mqtt_commands is fixed
    // ESP_ERROR_CHECK(process_mqtt_commands_init("AA:BB:CC:DD:EE:FF", &default_safety_limits)); // Temporarily disabled

    // 3. Inicialización de la capa de aplicación (Use Cases)
    ESP_LOGI(TAG, "Inicializando capa de Aplicación...");

    // TODO: Implementar inicialización de use cases
    // device_registration_init();
    // read_sensors_init();
    // control_irrigation_init();
    
    // Fase 4 - Inicializar sistema de control de riego
    ESP_LOGI(TAG, "Inicializando Fase 4 - Sistema de Control de Riego...");
    esp_err_t irrigation_ret = initialize_irrigation_control_system();
    if (irrigation_ret != ESP_OK) {
        ESP_LOGE(TAG, "Error crítico al inicializar sistema de riego: %s", esp_err_to_name(irrigation_ret));
        ESP_LOGE(TAG, "El sistema continuará sin control de riego automático");
        // No hacer ESP_ERROR_CHECK para permitir que el sistema continúe
    } else {
        ESP_LOGI(TAG, "Fase 4 - Sistema de control de riego inicializado correctamente");
    }
    
    // Crear tarea de publicación de sensores
    ESP_LOGI(TAG, "Creando tarea de publicación de sensores...");
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        sensor_publishing_task,                 // Task function
        "sensor_publish",                       // Task name
        SENSOR_PUBLISH_TASK_STACK_SIZE,        // Stack size
        NULL,                                   // Task parameters
        SENSOR_PUBLISH_TASK_PRIORITY,          // Task priority (3 - lower than HTTP/WiFi)
        NULL,                                   // Task handle
        1                                       // Core 1 - separate from WiFi provisioning on core 0
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Error al crear tarea de publicación de sensores");
        ESP_ERROR_CHECK(ESP_FAIL); // This will cause a restart
    } else {
        ESP_LOGI(TAG, "Tarea de publicación de sensores creada correctamente");
    }
    
    // Fase 4 - Crear tarea de control de riego (solo si se inicializó correctamente)
    if (s_irrigation_control_initialized) {
        ESP_LOGI(TAG, "Creando tarea de control de riego...");
        BaseType_t irrigation_task_ret = xTaskCreatePinnedToCore(
            irrigation_control_task,               // Task function
            "irrigation_control",                  // Task name
            IRRIGATION_CONTROL_TASK_STACK_SIZE,   // Stack size
            NULL,                                  // Task parameters
            IRRIGATION_CONTROL_TASK_PRIORITY,     // Task priority (4)
            NULL,                                  // Task handle
            1                                      // Core 1
        );
        
        if (irrigation_task_ret != pdPASS) {
            ESP_LOGE(TAG, "Error al crear tarea de control de riego");
            // No hacer ESP_ERROR_CHECK para permitir que el sistema continúe
        } else {
            ESP_LOGI(TAG, "Tarea de control de riego creada correctamente");
        }
    } else {
        ESP_LOGW(TAG, "Sistema de riego no inicializado - tarea de control no creada");
    }

    // 4. Mensaje de inicialización completa
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "Sistema inicializado correctamente");
    ESP_LOGI(TAG, "Memoria libre: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "==============================================");

    // 5. Loop principal - mantener el sistema funcionando
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000)); // 10 segundos
        ESP_LOGI(TAG, "Sistema funcionando - Memoria libre: %" PRIu32 " bytes", 
                esp_get_free_heap_size());
    }
}
