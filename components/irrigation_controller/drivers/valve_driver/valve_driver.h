/**
 * @file valve_driver.h
 * @brief Valve Driver - GPIO control for irrigation valves
 *
 * Low-level driver for controlling irrigation valves via GPIO/relay.
 * Provides thread-safe valve control with state tracking.
 *
 * Phase 5: Using LED simulator (GPIO_NUM_21)
 * Phase 6: Switch to relay modules (GPIO_NUM_2, 4, 5)
 *
 * @author Liwaisi Tech
 * @date 2025-10-21
 * @version 1.0.0 - Phase 5 (LED Simulator)
 */

#ifndef VALVE_DRIVER_H
#define VALVE_DRIVER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ VALVE DRIVER API ============================ */

/**
 * @brief Initialize valve driver
 *
 * Configures GPIO pins for valve control with pull-down configuration
 * for safety (valves closed on boot/reset).
 * Sets up thread-safety spinlock.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t valve_driver_init(void);

/**
 * @brief Deinitialize valve driver
 *
 * Closes all valves and releases GPIO resources.
 *
 * @return ESP_OK on success
 */
esp_err_t valve_driver_deinit(void);

/**
 * @brief Open valve (activate GPIO HIGH)
 *
 * Activates the specified valve by setting GPIO HIGH.
 * Thread-safe operation with spinlock protection.
 *
 * @param valve_num Valve number (1 or 2)
 * @return ESP_OK if valve opened, ESP_ERR_INVALID_ARG if invalid valve number
 */
esp_err_t valve_driver_open(uint8_t valve_num);

/**
 * @brief Close valve (set GPIO LOW)
 *
 * Deactivates the specified valve by setting GPIO LOW.
 * Thread-safe operation with spinlock protection.
 *
 * @param valve_num Valve number (1 or 2)
 * @return ESP_OK if valve closed, ESP_ERR_INVALID_ARG if invalid valve number
 */
esp_err_t valve_driver_close(uint8_t valve_num);

/**
 * @brief Check if valve is open
 *
 * Returns current state of specified valve.
 * Thread-safe read with spinlock protection.
 *
 * @param valve_num Valve number (1 or 2)
 * @return true if valve is open, false if closed or error
 */
bool valve_driver_is_open(uint8_t valve_num);

/**
 * @brief Emergency close all valves
 *
 * Immediately closes ALL valves regardless of current state.
 * Used for safety interlock in emergency conditions.
 * Bypasses normal checks for maximum speed.
 *
 * @return ESP_OK on success
 */
esp_err_t valve_driver_emergency_close_all(void);

/**
 * @brief Get valve state (debugging)
 *
 * Returns bitmask of valve states for debugging/monitoring.
 * Bit 0: Valve 1 state (1=open, 0=closed)
 * Bit 1: Valve 2 state (1=open, 0=closed)
 *
 * @return Bitmask of valve states
 */
uint8_t valve_driver_get_state_bitmask(void);

/* ============================ GPIO DEFINITIONS ============================ */

/**
 * @brief GPIO pin for valve 1
 *
 * Phase 5: GPIO_NUM_21 (LED simulator for testing)
 * Phase 6: GPIO_NUM_2 (Relay IN1)
 *
 * GPIO_NUM_21 is safe for output (not boot-related)
 */
#define VALVE_1_GPIO     GPIO_NUM_21

/**
 * @brief GPIO pin for valve 2
 *
 * Phase 5: Not used (single LED simulator)
 * Phase 6: GPIO_NUM_4 (Relay IN2)
 *
 * Can be configured separately if needed
 */
#define VALVE_2_GPIO     GPIO_NUM_4

/**
 * @brief Valve relay logic (active HIGH)
 *
 * Relay modules typically expect HIGH pulse to activate:
 * - HIGH (1): Valve open/relay activated
 * - LOW (0): Valve closed/relay deactivated
 */
#define VALVE_STATE_OFF  0
#define VALVE_STATE_ON   1

/* ============================ CONFIGURATION ============================ */

/**
 * @brief Maximum number of valves
 */
#define MAX_VALVES 2

#ifdef __cplusplus
}
#endif

#endif // VALVE_DRIVER_H
