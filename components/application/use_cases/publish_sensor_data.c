#include "publish_sensor_data.h"
#include "dht_sensor.h"
#include "mqtt_adapter.h"
#include "ambient_sensor_data.h"
#include "esp_log.h"

static const char *TAG = "publish_sensor_data_use_case";

esp_err_t publish_sensor_data_use_case(void)
{
    ESP_LOGI(TAG, "Starting sensor data publishing use case");
    
    // Check if DHT sensor is initialized
    if (!dht_sensor_is_initialized()) {
        ESP_LOGW(TAG, "DHT sensor not initialized, skipping sensor data publishing");
        return ESP_OK; // Return OK to continue task execution
    }
    
    // Read sensor data
    ambient_sensor_data_t sensor_data = {0};
    esp_err_t sensor_ret = dht_sensor_read(&sensor_data);
    if (sensor_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read sensor data: %s", esp_err_to_name(sensor_ret));
        ESP_LOGW(TAG, "Skipping sensor data publishing, will retry in next cycle");
        return ESP_OK; // Return OK to continue task execution
    }
    
    ESP_LOGI(TAG, "Sensor readings - Temperature: %.2f°C, Humidity: %.1f%%", 
             sensor_data.ambient_temperature, sensor_data.ambient_humidity);
    
    // Check MQTT connection before attempting to publish
    if (!mqtt_adapter_is_connected()) {
        ESP_LOGW(TAG, "MQTT adapter not connected, skipping sensor data publishing");
        return ESP_OK; // Return OK to continue task execution
    }
    
    // Publish sensor data via MQTT
    esp_err_t publish_ret = mqtt_adapter_publish_sensor_data(&sensor_data);
    if (publish_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to publish sensor data via MQTT: %s", esp_err_to_name(publish_ret));
        ESP_LOGW(TAG, "Will retry in next publishing cycle");
        return ESP_OK; // Return OK to continue task execution
    }
    
    ESP_LOGI(TAG, "Sensor data published successfully");
    return ESP_OK;
}