#include "dht_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include <string.h>

static const char *TAG = "DHT_SENSOR";

// DHT22/AM2301 specific constants
#define DHT_TYPE_AM2301_DELAY_US    20000
#define DHT_TIMER_INTERVAL          2
#define DHT_DATA_BITS               40
#define DHT_DATA_BYTES              (DHT_DATA_BITS / 8)

// Static variables for sensor state
static gpio_num_t sensor_pin = GPIO_NUM_NC;
static bool sensor_initialized = false;

// Critical section mutex for ESP32
static portMUX_TYPE sensor_mutex = portMUX_INITIALIZER_UNLOCKED;

#define SENSOR_ENTER_CRITICAL()  portENTER_CRITICAL(&sensor_mutex)
#define SENSOR_EXIT_CRITICAL()   portEXIT_CRITICAL(&sensor_mutex)

/**
 * @brief Wait for GPIO pin to reach expected state with timeout
 * 
 * @param pin GPIO pin to monitor
 * @param timeout_us Timeout in microseconds
 * @param expected_state Expected GPIO level (0 or 1)
 * @param duration Pointer to store actual duration (optional)
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout
 */
static esp_err_t dht_await_pin_state(gpio_num_t pin, uint32_t timeout_us,
                                     int expected_state, uint32_t *duration)
{
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    
    for (uint32_t elapsed = 0; elapsed < timeout_us; elapsed += DHT_TIMER_INTERVAL) {
        ets_delay_us(DHT_TIMER_INTERVAL);
        
        if (gpio_get_level(pin) == expected_state) {
            if (duration) {
                *duration = elapsed;
            }
            return ESP_OK;
        }
    }
    
    return ESP_ERR_TIMEOUT;
}

/**
 * @brief Read raw data bits from DHT sensor
 * 
 * @param pin GPIO pin connected to sensor
 * @param data Array to store raw data bytes (5 bytes)
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t dht_read_raw_data(gpio_num_t pin, uint8_t data[DHT_DATA_BYTES])
{
    uint32_t low_duration;
    uint32_t high_duration;
    
    // Clear data array
    memset(data, 0, DHT_DATA_BYTES);
    
    // Phase A: MCU pulls signal low to initiate communication
    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(pin, 0);
    ets_delay_us(DHT_TYPE_AM2301_DELAY_US);  // 20ms for DHT22/AM2301
    gpio_set_level(pin, 1);
    
    // Phase B: Wait for DHT response (40us)
    esp_err_t ret = dht_await_pin_state(pin, 40, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Phase B timeout - DHT not responding");
        return ret;
    }
    
    // Phase C: DHT pulls low for ~80us
    ret = dht_await_pin_state(pin, 88, 1, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Phase C timeout - DHT initialization error");
        return ret;
    }
    
    // Phase D: DHT pulls high for ~80us
    ret = dht_await_pin_state(pin, 88, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Phase D timeout - DHT initialization error");
        return ret;
    }
    
    // Read 40 data bits
    for (int i = 0; i < DHT_DATA_BITS; i++) {
        // Wait for start of bit (low signal)
        ret = dht_await_pin_state(pin, 65, 1, &low_duration);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Timeout waiting for bit %d LOW signal", i);
            return ESP_ERR_TIMEOUT;
        }
        
        // Read bit value (high signal duration determines 0 or 1)
        ret = dht_await_pin_state(pin, 75, 0, &high_duration);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Timeout waiting for bit %d HIGH signal", i);
            return ESP_ERR_TIMEOUT;
        }
        
        // Store bit in data array
        uint8_t byte_index = i / 8;
        uint8_t bit_index = i % 8;
        
        if (bit_index == 0) {
            data[byte_index] = 0;  // Clear byte on first bit
        }
        
        // If high duration > low duration, it's a '1' bit
        if (high_duration > low_duration) {
            data[byte_index] |= (1 << (7 - bit_index));
        }
    }
    
    return ESP_OK;
}

/**
 * @brief Convert raw DHT22 data bytes to temperature value
 * 
 * @param msb Most significant byte
 * @param lsb Least significant byte
 * @return Temperature in degrees Celsius * 10 (divide by 10 for actual value)
 */
static int16_t dht_convert_temperature(uint8_t msb, uint8_t lsb)
{
    int16_t temperature = msb & 0x7F;  // Remove sign bit
    temperature <<= 8;
    temperature |= lsb;
    
    // Check sign bit and apply if negative
    if (msb & 0x80) {
        temperature = -temperature;
    }
    
    return temperature;
}

/**
 * @brief Convert raw DHT22 data bytes to humidity value
 * 
 * @param msb Most significant byte
 * @param lsb Least significant byte
 * @return Humidity in percent * 10 (divide by 10 for actual value)
 */
static int16_t dht_convert_humidity(uint8_t msb, uint8_t lsb)
{
    int16_t humidity = msb;
    humidity <<= 8;
    humidity |= lsb;
    
    return humidity;
}

esp_err_t dht_sensor_init(gpio_num_t pin)
{
    if (pin < 0 || pin >= GPIO_NUM_MAX) {
        ESP_LOGE(TAG, "Invalid GPIO pin number: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Configure GPIO pin as open-drain output initially
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pin_bit_mask = (1ULL << pin),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE  // Enable internal pull-up
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", pin, esp_err_to_name(ret));
        return ret;
    }
    
    // Set pin high initially
    gpio_set_level(pin, 1);
    
    // Store pin configuration
    sensor_pin = pin;
    sensor_initialized = true;
    
    ESP_LOGI(TAG, "DHT sensor initialized on GPIO %d", pin);
    return ESP_OK;
}

esp_err_t dht_sensor_read(ambient_sensor_data_t* data)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "Data pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!sensor_initialized) {
        ESP_LOGE(TAG, "Sensor not initialized. Call dht_sensor_init() first");
        return ESP_ERR_INVALID_STATE;
    }
    
    uint8_t raw_data[DHT_DATA_BYTES] = {0};
    esp_err_t ret;
    
    SENSOR_ENTER_CRITICAL();
    
    // Set pin high before reading
    gpio_set_direction(sensor_pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(sensor_pin, 1);
    
    // Read raw sensor data
    ret = dht_read_raw_data(sensor_pin, raw_data);
    
    // Restore pin state
    gpio_set_direction(sensor_pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(sensor_pin, 1);
    
    SENSOR_EXIT_CRITICAL();
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read raw sensor data: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Verify checksum
    uint8_t checksum = raw_data[0] + raw_data[1] + raw_data[2] + raw_data[3];
    if (checksum != raw_data[4]) {
        ESP_LOGE(TAG, "Checksum mismatch: calculated=0x%02X, received=0x%02X", 
                 checksum, raw_data[4]);
        return ESP_ERR_INVALID_CRC;
    }
    
    // Convert raw data to actual values
    int16_t humidity_raw = dht_convert_humidity(raw_data[0], raw_data[1]);
    int16_t temperature_raw = dht_convert_temperature(raw_data[2], raw_data[3]);
    
    // Convert to floats (DHT22 returns values * 10)
    data->ambient_humidity = humidity_raw / 10.0f;
    data->ambient_temperature = temperature_raw / 10.0f;
    
    // Validate ranges
    if (data->ambient_temperature < -40.0f || data->ambient_temperature > 85.0f) {
        ESP_LOGW(TAG, "Temperature out of range: %.1f°C", data->ambient_temperature);
    }
    
    if (data->ambient_humidity < 0.0f || data->ambient_humidity > 100.0f) {
        ESP_LOGW(TAG, "Humidity out of range: %.1f%%", data->ambient_humidity);
    }
    
    ESP_LOGD(TAG, "Sensor reading: T=%.1f°C, H=%.1f%%", 
             data->ambient_temperature, data->ambient_humidity);
    
    return ESP_OK;
}

gpio_num_t dht_sensor_get_pin(void)
{
    return sensor_pin;
}

bool dht_sensor_is_initialized(void)
{
    return sensor_initialized;
}