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

// Headers de la aplicación - Component-Based Architecture (MIGRATED)
#include "wifi_manager.h"            // Migrated from wifi_adapter
#include "http_server.h"             // Migrated from http_adapter
#include "mqtt_client_manager.h"     // Migrated from mqtt_adapter
#include "device_config.h"           // Migrated component
#include "sensor_reader.h"           // Migrated component - unified sensor interface
#include "irrigation_controller.h"   // Phase 5 - Irrigation control logic

static const char *TAG = "SMART_IRRIGATION_MAIN";
static bool s_http_server_initialized = false;

// Task configuration constants
#define SENSOR_PUBLISH_TASK_STACK_SIZE    4096
#define SENSOR_PUBLISH_TASK_PRIORITY      3  // Reduced from 5 to 3 - avoid priority inversion with HTTP/WiFi tasks
#define SENSOR_PUBLISH_INTERVAL_MS        30000  // 30 seconds

/**
 * @brief Callback para comandos de riego recibidos via MQTT
 *
 * Ejecuta comandos START/STOP/EMERGENCY_STOP con validaciones de seguridad.
 * Llamado automáticamente cuando se recibe un mensaje en topic "irrigation/control/{mac}"
 */
static void mqtt_irrigation_command_handler(irrigation_command_t command,
                                           uint16_t duration_minutes,
                                           void* user_data);

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

        // 2. LOG DE DATOS LEÍDOS - SIEMPRE (independiente de MQTT)
        // FIX: Mover logs ANTES del check MQTT para visibilidad en modo offline
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
        if (!mqtt_client_is_connected()) {
            if (cycle_count % 10 == 0) {
                ESP_LOGW(TAG, "Cycle %" PRIu32 ": MQTT not connected, skipping data publish (sensors OK)", cycle_count);
            }
            continue;  // No publicar si MQTT desconectado, pero sensores ya fueron logueados
        }

        // 4. PUBLICAR VÍA MQTT usando nuevo componente mqtt_client
        // mqtt_client acepta sensor_reading_t directamente (incluye ambient + soil data)
        ret = mqtt_client_publish_sensor_data(&reading);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Cycle %" PRIu32 ": MQTT publish failed: %s", cycle_count, esp_err_to_name(ret));
            continue;
        }

        // Éxito - log solo cada 10 ciclos (5 minutos)
        if (cycle_count % 10 == 0) {
            ESP_LOGI(TAG, "Cycle %" PRIu32 ": Data published successfully to MQTT", cycle_count);
        }
    }
}

/**
 * @brief Handler de comandos MQTT para control de riego
 *
 * Recibe comandos via MQTT y los ejecuta a través del irrigation_controller.
 * Comandos soportados:
 * - START: Inicia riego con duración especificada (default 15 min)
 * - STOP: Detiene riego normalmente
 * - EMERGENCY_STOP: Detiene riego y activa safety lock
 *
 * @param command Tipo de comando (START/STOP/EMERGENCY_STOP)
 * @param duration_minutes Duración en minutos (solo para START, 0=default)
 * @param user_data Datos de usuario (no utilizado)
 */
static void mqtt_irrigation_command_handler(irrigation_command_t command,
                                           uint16_t duration_minutes,
                                           void* user_data)
{
    // Nombres de comandos para logging
    const char* cmd_names[] = {"START", "STOP", "EMERGENCY_STOP", "PAUSE", "RESUME"};

    ESP_LOGI(TAG, "MQTT irrigation command received: %s (duration: %d min)",
             cmd_names[command], duration_minutes);

    // Ejecutar comando via irrigation_controller
    esp_err_t ret = irrigation_controller_execute_command(command, duration_minutes);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Irrigation command executed successfully: %s", cmd_names[command]);
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Irrigation command rejected (safety check failed): %s", cmd_names[command]);
        ESP_LOGW(TAG, "Possible reasons: safety lock active, min interval not met, max daily duration reached");
    } else {
        ESP_LOGE(TAG, "Irrigation command failed: %s - %s", cmd_names[command], esp_err_to_name(ret));
    }
}

/**
 * @brief Manejador de eventos WiFi para la aplicación principal
 */
static void main_wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_MANAGER_EVENTS) {
        switch (event_id) {
            case WIFI_MANAGER_EVENT_INIT_COMPLETE:
                ESP_LOGI(TAG, "WiFi manager inicializado");
                break;

            case WIFI_MANAGER_EVENT_PROVISIONING_STARTED:
                ESP_LOGI(TAG, "Modo de aprovisionamiento WiFi iniciado");
                ESP_LOGI(TAG, "Conectar a red 'Liwaisi-Config' para configurar WiFi");
                break;

            case WIFI_MANAGER_EVENT_PROVISIONING_COMPLETED:
                ESP_LOGI(TAG, "Aprovisionamiento WiFi completado");
                break;

            case WIFI_MANAGER_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Conexión WiFi establecida");
                break;

            case WIFI_MANAGER_EVENT_IP_OBTAINED: {
                esp_ip4_addr_t ip;
                if (wifi_manager_get_ip(&ip) == ESP_OK) {
                    ESP_LOGI(TAG, "Dirección IP obtenida: " IPSTR, IP2STR(&ip));
                }

                // Initialize HTTP server when IP is obtained (both after provisioning and normal connections)
                if (!s_http_server_initialized) {
                    ESP_LOGI(TAG, "Inicializando servidor HTTP tras obtener IP...");
                    // Small delay to ensure any provisioning server is fully stopped
                    vTaskDelay(pdMS_TO_TICKS(1000));

                    // Use default HTTP server configuration
                    esp_err_t ret = http_server_init(NULL);  // NULL = use defaults
                    if (ret == ESP_OK) {
                        // FIX: Start HTTP server (init only configures, start actually opens port)
                        ret = http_server_start();
                        if (ret == ESP_OK) {
                            s_http_server_initialized = true;
                            ESP_LOGI(TAG, "Servidor HTTP arrancado correctamente en IP: " IPSTR " puerto 80", IP2STR(&ip));
                        } else {
                            ESP_LOGE(TAG, "Error al arrancar servidor HTTP: %s", esp_err_to_name(ret));
                        }
                    } else {
                        ESP_LOGE(TAG, "Error al inicializar servidor HTTP: %s", esp_err_to_name(ret));
                    }
                }
                break;
            }

            case WIFI_MANAGER_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "Conexión WiFi perdida - reintentando...");
                break;

            case WIFI_MANAGER_EVENT_RESET_REQUESTED:
                ESP_LOGW(TAG, "Patrón de reinicio detectado - iniciando aprovisionamiento");
                break;

            case WIFI_MANAGER_EVENT_CONNECTION_FAILED:
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
 * Inicializa todos los componentes del sistema siguiendo arquitectura component-based.
 * Componentes migrados de hexagonal a component-based architecture.
 */
void app_main(void)
{
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "Liwaisi Tech - https://liwaisi.tech");
    ESP_LOGI(TAG, "Sistema de Riego Inteligente v2.0.0");
    ESP_LOGI(TAG, "Component-Based Architecture");
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

    // Inicializar componente sensor_reader (DHT22 + sensores de suelo)
    ESP_LOGI(TAG, "Inicializando componente sensor_reader...");
    sensor_config_t sensor_cfg = {
        .soil_sensor_count = 3,
        .enable_soil_filtering = true,
        .filter_window_size = 5,
        .dht22_gpio = GPIO_DHT22,           // Definido en common_types.h como 18
        .dht22_read_timeout_ms = 2000,
        .adc_attenuation = ADC_ATTEN_DB_12,
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
    ESP_ERROR_CHECK(device_config_init());

    // 2. Inicialización de componentes de conectividad
    ESP_LOGI(TAG, "Inicializando componentes de conectividad...");

    // Registrar manejador de eventos WiFi
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_MANAGER_EVENTS, ESP_EVENT_ANY_ID, &main_wifi_event_handler, NULL));

    // Inicializar y arrancar WiFi manager
    ESP_LOGI(TAG, "Inicializando WiFi manager...");
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(wifi_manager_start());

    // Mostrar estado del dispositivo
    wifi_manager_status_t wifi_status;
    if (wifi_manager_get_status(&wifi_status) == ESP_OK) {
        ESP_LOGI(TAG, "Estado WiFi: %s",
                 wifi_status.provisioned ? "Aprovisionado" : "No aprovisionado");

        ESP_LOGI(TAG, "Dirección MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 wifi_status.mac_address[0], wifi_status.mac_address[1],
                 wifi_status.mac_address[2], wifi_status.mac_address[3],
                 wifi_status.mac_address[4], wifi_status.mac_address[5]);
    }

    // DO NOT initialize HTTP server here - it conflicts with provisioning server
    // HTTP server will be initialized after WiFi connection is established
    ESP_LOGI(TAG, "HTTP server initialization deferred until WiFi connection");

    // Inicializar cliente MQTT
    ESP_LOGI(TAG, "Inicializando cliente MQTT...");
    ESP_ERROR_CHECK(mqtt_client_init());

    // Inicializar controlador de riego (Phase 5)
    ESP_LOGI(TAG, "Inicializando controlador de riego...");
    irrigation_controller_config_t irrig_cfg = IRRIGATION_CONTROLLER_DEFAULT_CONFIG();
    ret = irrigation_controller_init(&irrig_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al inicializar irrigation_controller: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "WARNING: Sistema continuará sin control de riego automático");
    } else {
        ESP_LOGI(TAG, "Irrigation controller inicializado correctamente");
        ESP_LOGI(TAG, "  - Soil threshold: %.1f%% (start) / %.1f%% (stop)",
                 irrig_cfg.soil_threshold_critical, irrig_cfg.soil_threshold_optimal);
        ESP_LOGI(TAG, "  - Max duration: %d minutes", irrig_cfg.max_duration_minutes);
        ESP_LOGI(TAG, "  - Evaluation: 60s (online) / 2h (offline)");
    }

    // Registrar callback para comandos MQTT de riego
    ESP_LOGI(TAG, "Registrando callback de comandos MQTT para riego...");
    ret = mqtt_client_register_command_callback(mqtt_irrigation_command_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo registrar callback MQTT: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "Control remoto de riego NO disponible (solo modo automático)");
    } else {
        ESP_LOGI(TAG, "Callback MQTT registrado - control remoto de riego ACTIVO");
    }

    // 3. Creación de tareas de aplicación
    ESP_LOGI(TAG, "Creando tareas de aplicación...");

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
    ESP_LOGI(TAG, "Componentes activos:");
    ESP_LOGI(TAG, "  - WiFi manager: OK");
    ESP_LOGI(TAG, "  - MQTT client: OK");
    ESP_LOGI(TAG, "  - HTTP server: Deferred (will start after IP obtained)");
    ESP_LOGI(TAG, "  - Sensor reader: OK");
    // Check irrigation controller status (if init succeeded, state won't be uninitialized)
    irrigation_controller_status_t irrig_status;
    bool irrig_ok = (irrigation_controller_get_status(&irrig_status) == ESP_OK);
    ESP_LOGI(TAG, "  - Irrigation controller: %s",
             irrig_ok ? "OK (automatic + remote control)" : "DISABLED");
    ESP_LOGI(TAG, "Memoria libre: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "==============================================");

    // 5. Loop principal - mantener el sistema funcionando
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000)); // 10 segundos
        ESP_LOGI(TAG, "Sistema funcionando - Memoria libre: %" PRIu32 " bytes", 
                esp_get_free_heap_size());
    }
}
