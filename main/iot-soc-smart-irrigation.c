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

// Headers de la aplicación - Component-Based Architecture
#include "wifi_adapter.h"
#include "http_adapter.h"
#include "device_config_service.h"
#include "shared_resource_manager.h"
#include "sensor_reader.h"           // Nuevo componente unificado de sensores
// TODO: Uncomment when implementations are created
// #include "use_cases/device_registration.h"
// #include "use_cases/read_sensors.h"
// #include "use_cases/control_irrigation.h"
#include "mqtt_adapter.h"
#include "ambient_sensor_data.h"     // Para compatibilidad temporal con MQTT

static const char *TAG = "SMART_IRRIGATION_MAIN";
static bool s_http_adapter_initialized = false;

// Task configuration constants
#define SENSOR_PUBLISH_TASK_STACK_SIZE    4096
#define SENSOR_PUBLISH_TASK_PRIORITY      3  // Reduced from 5 to 3 - avoid priority inversion with HTTP/WiFi tasks
#define SENSOR_PUBLISH_INTERVAL_MS        30000  // 30 seconds

/**
 * @brief Sensor publishing task (Component-Based Architecture)
 *
 * Periodically reads all sensors (DHT22 + soil) and publishes data via MQTT.
 * Uses vTaskDelayUntil() for precise 30-second intervals.
 * Handles failures gracefully by continuing the publishing loop.
 * Implements anti-deadlock measures for WiFi provisioning compatibility.
 */
static void sensor_publishing_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Sensor publishing task started (Component-Based Architecture)");

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(SENSOR_PUBLISH_INTERVAL_MS);
    uint32_t cycle_count = 0;

    while (1) {
        // Wait for the next cycle (30 seconds)
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        cycle_count++;

        // 1. LEER TODOS LOS SENSORES usando nuevo componente
        sensor_reading_t reading;
        esp_err_t ret = sensor_reader_get_all(&reading);

        if (ret != ESP_OK) {
            // Falló lectura de sensores - log reducido para evitar spam
            if (cycle_count % 10 == 0) {
                ESP_LOGW(TAG, "Cycle %" PRIu32 ": Sensor read failed: %s",
                         cycle_count, esp_err_to_name(ret));
            }
            continue;  // Saltar publicación si no hay datos válidos
        }

        // 2. LOG DE DATOS LEÍDOS (cada 5 ciclos = 2.5 minutos)
        if (cycle_count % 5 == 0) {
            ESP_LOGI(TAG, "Cycle %" PRIu32 ": T=%.1f°C H=%.1f%% Soil=[%.1f%%, %.1f%%, %.1f%%] - Heap:%" PRIu32,
                     cycle_count,
                     reading.ambient.temperature,
                     reading.ambient.humidity,
                     reading.soil.soil_humidity[0],
                     reading.soil.soil_humidity[1],
                     reading.soil.soil_humidity[2],
                     esp_get_free_heap_size());
        }

        // 3. VERIFICAR CONECTIVIDAD MQTT antes de publicar
        if (!mqtt_adapter_is_connected()) {
            if (cycle_count % 10 == 0) {
                ESP_LOGW(TAG, "MQTT not connected, skipping data publish");
            }
            continue;
        }

        // 4. PUBLICAR VÍA MQTT (adaptar temporalmente a formato viejo)
        // NOTA: mqtt_adapter espera ambient_sensor_data_t, adaptar hasta migrar MQTT
        ambient_sensor_data_t mqtt_data = {
            .ambient_temperature = reading.ambient.temperature,
            .ambient_humidity = reading.ambient.humidity
        };

        ret = mqtt_adapter_publish_sensor_data(&mqtt_data);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "MQTT publish failed: %s", esp_err_to_name(ret));
            continue;
        }

        // Éxito - log solo cada 10 ciclos (5 minutos)
        if (cycle_count % 10 == 0) {
            ESP_LOGI(TAG, "Cycle %" PRIu32 ": Data published successfully to MQTT", cycle_count);
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
    
    // Inicializar componente sensor_reader (DHT22 + sensores de suelo)
    ESP_LOGI(TAG, "Inicializando componente sensor_reader...");
    sensor_config_t sensor_cfg = {
        .soil_sensor_count = 3,
        .enable_soil_filtering = true,
        .filter_window_size = 5,
        .dht22_gpio = GPIO_DHT22,           // Definido en common_types.h como 18
        .dht22_read_timeout_ms = 2000,
        .adc_attenuation = ADC_ATTEN_DB_11,
        .soil_cal_dry_mv = {2800, 2800, 2800},
        .soil_cal_wet_mv = {1200, 1200, 1200},
        .max_consecutive_errors = 5
    };
    ret = sensor_reader_init(&sensor_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al inicializar sensor_reader: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "CRITICAL: Sistema no puede funcionar sin sensores");
        ESP_ERROR_CHECK(ESP_FAIL);  // Forzar reinicio
    } else {
        ESP_LOGI(TAG, "Sensor reader inicializado: DHT22 (GPIO %d) + %d sensores suelo",
                 GPIO_DHT22, sensor_cfg.soil_sensor_count);
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

    // 3. Inicialización de la capa de aplicación (Use Cases)
    ESP_LOGI(TAG, "Inicializando capa de Aplicación...");
    
    // TODO: Implementar inicialización de use cases
    // device_registration_init();
    // read_sensors_init();
    // control_irrigation_init();
    
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
