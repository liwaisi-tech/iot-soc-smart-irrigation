/**
 * @file valve_driver.c
 * @brief Valve Driver Implementation - GPIO control for irrigation valves
 *
 * Provides thread-safe, low-level control of irrigation valves.
 * Phase 5: LED simulator (GPIO_NUM_21)
 * Phase 6: Relay modules (GPIO_NUM_2, 4, 5)
 *
 * Thread-Safety:
 * - Spinlock protects valve_states array
 * - All GPIO operations atomic
 * - Init/deinit protected
 *
 * @author Liwaisi Tech
 * @date 2025-10-21
 * @version 1.0.0
 */

#include "valve_driver.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

/* ============================ PRIVATE STATE ============================ */

static const char *TAG = "valve_driver";

/**
 * @brief Valve state tracking
 *
 * Array indices correspond to valve numbers:
 * - valve_states[0] = Valve 1 (GPIO_NUM_21)
 * - valve_states[1] = Valve 2 (GPIO_NUM_4)
 */
static bool valve_states[MAX_VALVES] = {false, false};

/**
 * @brief Spinlock for thread-safe access to valve_states
 *
 * Protects concurrent access to valve_states array
 * and GPIO operations from multiple tasks
 */
static portMUX_TYPE valve_spinlock = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief Initialization flag
 */
static bool is_initialized = false;

/* ============================ PRIVATE FUNCTIONS ============================ */

/**
 * @brief Map valve number to GPIO pin
 *
 * @param valve_num Valve number (1-2)
 * @return GPIO number, or -1 if invalid valve number
 */
static int _valve_num_to_gpio(uint8_t valve_num)
{
    switch (valve_num) {
        case 1:
            return VALVE_1_GPIO;
        case 2:
            return VALVE_2_GPIO;
        default:
            return -1;
    }
}

/**
 * @brief Map valve number to array index
 *
 * @param valve_num Valve number (1-2)
 * @return Array index (0-1), or -1 if invalid
 */
static int _valve_num_to_index(uint8_t valve_num)
{
    if (valve_num >= 1 && valve_num <= MAX_VALVES) {
        return valve_num - 1;  // Convert 1-2 to 0-1
    }
    return -1;
}

/* ============================ PUBLIC API ============================ */

esp_err_t valve_driver_init(void)
{
    if (is_initialized) {
        ESP_LOGW(TAG, "Valve driver already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing valve driver");

    // Configure GPIO pins for both valves
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << VALVE_1_GPIO) | (1ULL << VALVE_2_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,  // Safe: closed on boot
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize both valves to LOW (closed state)
    portENTER_CRITICAL(&valve_spinlock);
    {
        gpio_set_level(VALVE_1_GPIO, VALVE_STATE_OFF);
        gpio_set_level(VALVE_2_GPIO, VALVE_STATE_OFF);
        valve_states[0] = false;
        valve_states[1] = false;
    }
    portEXIT_CRITICAL(&valve_spinlock);

    is_initialized = true;
    ESP_LOGI(TAG, "Valve driver initialized: GPIO %d (valve 1), GPIO %d (valve 2)",
             VALVE_1_GPIO, VALVE_2_GPIO);

    return ESP_OK;
}

esp_err_t valve_driver_deinit(void)
{
    if (!is_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing valve driver");

    // Close all valves for safety
    esp_err_t ret = valve_driver_emergency_close_all();

    // Mark as uninitialized
    is_initialized = false;

    return ret;
}

esp_err_t valve_driver_open(uint8_t valve_num)
{
    if (!is_initialized) {
        ESP_LOGE(TAG, "Valve driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    int index = _valve_num_to_index(valve_num);
    if (index < 0) {
        ESP_LOGE(TAG, "Invalid valve number: %d", valve_num);
        return ESP_ERR_INVALID_ARG;
    }

    int gpio = _valve_num_to_gpio(valve_num);

    portENTER_CRITICAL(&valve_spinlock);
    {
        gpio_set_level(gpio, VALVE_STATE_ON);
        valve_states[index] = true;
    }
    portEXIT_CRITICAL(&valve_spinlock);

    ESP_LOGI(TAG, "Valve %d opened (GPIO %d HIGH)", valve_num, gpio);
    return ESP_OK;
}

esp_err_t valve_driver_close(uint8_t valve_num)
{
    if (!is_initialized) {
        ESP_LOGE(TAG, "Valve driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    int index = _valve_num_to_index(valve_num);
    if (index < 0) {
        ESP_LOGE(TAG, "Invalid valve number: %d", valve_num);
        return ESP_ERR_INVALID_ARG;
    }

    int gpio = _valve_num_to_gpio(valve_num);

    portENTER_CRITICAL(&valve_spinlock);
    {
        gpio_set_level(gpio, VALVE_STATE_OFF);
        valve_states[index] = false;
    }
    portEXIT_CRITICAL(&valve_spinlock);

    ESP_LOGI(TAG, "Valve %d closed (GPIO %d LOW)", valve_num, gpio);
    return ESP_OK;
}

bool valve_driver_is_open(uint8_t valve_num)
{
    if (!is_initialized) {
        ESP_LOGE(TAG, "Valve driver not initialized");
        return false;
    }

    int index = _valve_num_to_index(valve_num);
    if (index < 0) {
        ESP_LOGE(TAG, "Invalid valve number: %d", valve_num);
        return false;
    }

    bool state;
    portENTER_CRITICAL(&valve_spinlock);
    {
        state = valve_states[index];
    }
    portEXIT_CRITICAL(&valve_spinlock);

    return state;
}

esp_err_t valve_driver_emergency_close_all(void)
{
    if (!is_initialized) {
        ESP_LOGE(TAG, "Valve driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGE(TAG, "EMERGENCY CLOSE ALL VALVES");

    portENTER_CRITICAL(&valve_spinlock);
    {
        // Close all valves immediately
        gpio_set_level(VALVE_1_GPIO, VALVE_STATE_OFF);
        gpio_set_level(VALVE_2_GPIO, VALVE_STATE_OFF);
        valve_states[0] = false;
        valve_states[1] = false;
    }
    portEXIT_CRITICAL(&valve_spinlock);

    return ESP_OK;
}

uint8_t valve_driver_get_state_bitmask(void)
{
    if (!is_initialized) {
        return 0;
    }

    uint8_t bitmask = 0;

    portENTER_CRITICAL(&valve_spinlock);
    {
        if (valve_states[0]) bitmask |= (1 << 0);  // Bit 0: Valve 1
        if (valve_states[1]) bitmask |= (1 << 1);  // Bit 1: Valve 2
    }
    portEXIT_CRITICAL(&valve_spinlock);

    return bitmask;
}
