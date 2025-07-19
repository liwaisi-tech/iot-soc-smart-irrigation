#include "http_adapter.h"
#include "http/server.h"
#include "esp_log.h"

static const char *TAG = "HTTP_ADAPTER";

esp_err_t http_adapter_init(void)
{
    ESP_LOGI(TAG, "Inicializando adaptador HTTP...");
    
    esp_err_t ret = http_server_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al inicializar servidor HTTP: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Adaptador HTTP inicializado correctamente");
    return ESP_OK;
}

esp_err_t http_adapter_stop(void)
{
    ESP_LOGI(TAG, "Deteniendo adaptador HTTP...");
    
    esp_err_t ret = http_server_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al detener servidor HTTP: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Adaptador HTTP detenido correctamente");
    return ESP_OK;
}

bool http_adapter_is_running(void)
{
    return http_server_get_handle() != NULL;
}