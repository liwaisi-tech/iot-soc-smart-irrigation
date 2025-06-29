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
// TODO: Uncomment when implementations are created
// #include "use_cases/device_registration.h"
// #include "use_cases/read_sensors.h"
// #include "use_cases/control_irrigation.h"
// #include "adapters/wifi_adapter.h"
// #include "adapters/mqtt_adapter.h"
// #include "adapters/http_adapter.h"

static const char *TAG = "SMART_IRRIGATION_MAIN";

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
    ESP_LOGI(TAG, "Iniciando Sistema de Riego Inteligente v1.0.0");
    ESP_LOGI(TAG, "Arquitectura: Hexagonal (Puertos y Adaptadores)");
    ESP_LOGI(TAG, "Target: ESP32");
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

    // 2. Inicialización de la capa de infraestructura
    ESP_LOGI(TAG, "Inicializando capa de Infraestructura...");
    
    // TODO: Implementar inicialización de adaptadores
    // wifi_adapter_init();
    // mqtt_adapter_init();
    // http_adapter_init();

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
