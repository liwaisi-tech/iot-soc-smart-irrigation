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

static const char *TAG = "SMART_IRRIGATION_MAIN";
static bool s_http_adapter_initialized = false;

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
                
                // Inicializar adaptador HTTP después de obtener IP
                if (!s_http_adapter_initialized) {
                    ESP_LOGI(TAG, "Inicializando adaptador HTTP tras conexión WiFi...");
                    esp_err_t ret = http_adapter_init();
                    if (ret == ESP_OK) {
                        s_http_adapter_initialized = true;
                        ESP_LOGI(TAG, "Adaptador HTTP inicializado correctamente");
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
    
    // Inicializar sensor DHT22 en GPIO_NUM_4
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
    
    // Inicializar adaptador HTTP solo si no estamos en modo de aprovisionamiento
    wifi_adapter_get_status(&wifi_status);
    
    if (wifi_status.provisioned && wifi_status.connected) {
        ESP_LOGI(TAG, "Inicializando adaptador HTTP...");
        ESP_ERROR_CHECK(http_adapter_init());
    } else {
        ESP_LOGI(TAG, "Saltando inicialización HTTP - modo aprovisionamiento activo");
        ESP_LOGI(TAG, "El servidor HTTP estará disponible después de la configuración WiFi");
    }
    
    // Inicializar adaptador MQTT después de HTTP
    ESP_LOGI(TAG, "Inicializando adaptador MQTT...");
    ESP_ERROR_CHECK(mqtt_adapter_init());

    // 3. Inicialización de la capa de aplicación (Use Cases)
    ESP_LOGI(TAG, "Inicializando capa de Aplicación...");
    
    // TODO: Implementar inicialización de use cases
    // device_registration_init();
    // read_sensors_init();
    // control_irrigation_init();

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
