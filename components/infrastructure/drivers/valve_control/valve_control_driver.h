#ifndef INFRASTRUCTURE_DRIVERS_VALVE_CONTROL_DRIVER_H
#define INFRASTRUCTURE_DRIVERS_VALVE_CONTROL_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

/**
 * @file valve_control_driver.h
 * @brief Driver para control de válvulas de riego
 *
 * Implementa control de válvulas solenoides a través de relés
 * con protecciones de seguridad, monitoreo de estado y gestión
 * de energía optimizada para operación con batería.
 *
 * Especificaciones Técnicas - Valve Control Driver:
 * - Hardware: Relés 5V/12V para válvulas solenoides 12V/24V
 * - GPIO Control: 3 salidas digitales configurables
 * - Protecciones: Timeout automático, detección cortocircuito
 * - Monitoreo: Estado real vs esperado, corriente de operación
 * - Safety: Parada de emergencia, interlock entre válvulas
 */

/**
 * @brief Configuración de pines GPIO para válvulas
 */
#define VALVE_1_GPIO_PIN        GPIO_NUM_2    // Válvula 1
#define VALVE_2_GPIO_PIN        GPIO_NUM_4    // Válvula 2
#define VALVE_3_GPIO_PIN        GPIO_NUM_5    // Válvula 3

/**
 * @brief Estados posibles de válvula
 */
typedef enum {
    VALVE_STATE_CLOSED = 0,                   // Válvula cerrada
    VALVE_STATE_OPENING,                      // Válvula abriéndose
    VALVE_STATE_OPEN,                         // Válvula abierta
    VALVE_STATE_CLOSING,                      // Válvula cerrándose
    VALVE_STATE_ERROR,                        // Error en válvula
    VALVE_STATE_UNKNOWN,                      // Estado desconocido
    VALVE_STATE_MAX
} valve_state_t;

/**
 * @brief Tipos de error de válvula
 */
typedef enum {
    VALVE_ERROR_NONE = 0,                     // Sin error
    VALVE_ERROR_TIMEOUT,                      // Timeout operación
    VALVE_ERROR_SHORT_CIRCUIT,                // Cortocircuito detectado
    VALVE_ERROR_OPEN_CIRCUIT,                 // Circuito abierto
    VALVE_ERROR_OVERVOLTAGE,                  // Sobrevoltaje
    VALVE_ERROR_OVERCURRENT,                  // Sobrecorriente
    VALVE_ERROR_MECHANICAL_JAM,               // Atasco mecánico
    VALVE_ERROR_RELAY_FAILURE,                // Fallo del relé
    VALVE_ERROR_GPIO_FAILURE,                 // Fallo GPIO
    VALVE_ERROR_MAX
} valve_error_type_t;

/**
 * @brief Estado completo de válvula individual
 */
typedef struct {
    uint8_t valve_number;                     // Número válvula (1-3)
    gpio_num_t gpio_pin;                      // Pin GPIO asignado
    valve_state_t current_state;              // Estado actual
    valve_state_t target_state;               // Estado objetivo
    valve_error_type_t last_error;            // Último error detectado

    // Tiempos de operación
    uint32_t state_change_timestamp;          // Última cambio de estado
    uint32_t total_open_time_ms;              // Tiempo total abierta
    uint32_t current_session_start_time;      // Inicio sesión actual

    // Estadísticas de operación
    uint32_t open_operations_count;           // Número de aperturas
    uint32_t close_operations_count;          // Número de cierres
    uint32_t error_count;                     // Número de errores
    uint32_t successful_operations;           // Operaciones exitosas

    // Estado de hardware
    bool is_initialized;                      // Si está inicializada
    bool is_relay_energized;                  // Si el relé está energizado
    uint16_t response_time_ms;                // Tiempo respuesta última operación
} valve_status_t;

/**
 * @brief Configuración del sistema de válvulas
 */
typedef struct {
    // Configuración temporal
    uint16_t valve_response_timeout_ms;       // 5000ms - Timeout respuesta válvula
    uint16_t relay_settling_time_ms;          // 100ms - Tiempo asentamiento relé
    uint16_t interlock_delay_ms;              // 200ms - Delay entre válvulas

    // Configuración de seguridad
    uint32_t max_operation_duration_ms;       // 1800000ms (30min) - Duración máxima
    bool enable_interlock_protection;         // true - Protección interlock
    bool enable_timeout_protection;           // true - Protección timeout

    // Configuración de monitoreo
    bool enable_state_monitoring;             // true - Monitoreo estado
    uint16_t monitoring_interval_ms;          // 1000ms - Intervalo monitoreo

    // Configuración de energía
    bool enable_power_optimization;           // true - Optimización energía
    uint16_t relay_hold_reduction_delay_ms;   // 5000ms - Reducir corriente hold
} valve_control_config_t;

/**
 * @brief Estadísticas globales del sistema de válvulas
 */
typedef struct {
    uint32_t total_open_commands;             // Total comandos apertura
    uint32_t total_close_commands;            // Total comandos cierre
    uint32_t successful_operations;           // Operaciones exitosas
    uint32_t failed_operations;               // Operaciones fallidas
    uint32_t timeout_errors;                  // Errores por timeout
    uint32_t hardware_errors;                 // Errores de hardware
    uint32_t safety_stops;                    // Paradas de seguridad
    float average_response_time_ms;           // Tiempo promedio respuesta
    uint32_t total_irrigation_time_ms;        // Tiempo total de riego
    uint32_t power_consumption_estimates_mah; // Consumo estimado energía
} valve_system_statistics_t;

/**
 * @brief Inicializar sistema de control de válvulas
 *
 * Configura GPIOs, inicializa relés y establece estado seguro.
 *
 * @param config Configuración del sistema
 * @return ESP_OK en éxito, ESP_ERR_* en error
 */
esp_err_t valve_control_driver_init(const valve_control_config_t* config);

/**
 * @brief Desinicializar sistema de válvulas
 *
 * Cierra todas las válvulas y libera recursos GPIO.
 *
 * @return ESP_OK en éxito
 */
esp_err_t valve_control_driver_deinit(void);

/**
 * @brief Abrir válvula específica
 *
 * @param valve_number Número de válvula (1-3)
 * @return ESP_OK en éxito, ESP_ERR_* en error
 */
esp_err_t valve_control_driver_open_valve(uint8_t valve_number);

/**
 * @brief Cerrar válvula específica
 *
 * @param valve_number Número de válvula (1-3)
 * @return ESP_OK en éxito, ESP_ERR_* en error
 */
esp_err_t valve_control_driver_close_valve(uint8_t valve_number);

/**
 * @brief Cerrar todas las válvulas inmediatamente
 *
 * Función de seguridad para parada de emergencia.
 *
 * @return ESP_OK en éxito
 */
esp_err_t valve_control_driver_close_all_valves(void);

/**
 * @brief Verificar estado actual de válvula
 *
 * @param valve_number Número de válvula (1-3)
 * @return Estado actual de la válvula
 */
valve_state_t valve_control_driver_get_valve_state(uint8_t valve_number);

/**
 * @brief Verificar si válvula está abierta
 *
 * @param valve_number Número de válvula (1-3)
 * @return true si válvula está abierta
 */
bool valve_control_driver_is_valve_open(uint8_t valve_number);

/**
 * @brief Verificar si válvula está cerrada
 *
 * @param valve_number Número de válvula (1-3)
 * @return true si válvula está cerrada
 */
bool valve_control_driver_is_valve_closed(uint8_t valve_number);

/**
 * @brief Obtener estado completo de válvula
 *
 * @param valve_number Número de válvula (1-3)
 * @param status Puntero donde escribir estado completo
 * @return ESP_OK en éxito, ESP_ERR_INVALID_ARG si válvula inválida
 */
esp_err_t valve_control_driver_get_valve_status(uint8_t valve_number, valve_status_t* status);

/**
 * @brief Verificar si hay válvulas abiertas
 *
 * @return true si hay al menos una válvula abierta
 */
bool valve_control_driver_has_open_valves(void);

/**
 * @brief Obtener número de válvulas abiertas
 *
 * @return Número de válvulas actualmente abiertas (0-3)
 */
uint8_t valve_control_driver_get_open_valve_count(void);

/**
 * @brief Ejecutar parada de emergencia
 *
 * Cierra todas las válvulas y bloquea operaciones futuras
 * hasta reset del sistema.
 *
 * @param reason Razón de la parada de emergencia
 * @return ESP_OK siempre (operación crítica)
 */
esp_err_t valve_control_driver_emergency_stop(const char* reason);

/**
 * @brief Verificar si sistema está en parada de emergencia
 *
 * @return true si está en parada de emergencia
 */
bool valve_control_driver_is_emergency_stopped(void);

/**
 * @brief Resetear parada de emergencia
 *
 * Permite operaciones normales después de parada de emergencia.
 *
 * @return ESP_OK en éxito
 */
esp_err_t valve_control_driver_reset_emergency_stop(void);

/**
 * @brief Ejecutar test de válvulas
 *
 * Realiza secuencia de test abriendo y cerrando cada válvula.
 *
 * @param test_duration_ms Duración del test por válvula
 * @return ESP_OK si todas las válvulas pasan el test
 */
esp_err_t valve_control_driver_run_valve_test(uint16_t test_duration_ms);

/**
 * @brief Calibrar tiempo de respuesta de válvula
 *
 * @param valve_number Número de válvula (1-3)
 * @return ESP_OK en éxito
 */
esp_err_t valve_control_driver_calibrate_response_time(uint8_t valve_number);

/**
 * @brief Configurar interlock entre válvulas
 *
 * Previene apertura simultánea si está habilitado.
 *
 * @param enable true para habilitar interlock
 * @return ESP_OK en éxito
 */
esp_err_t valve_control_driver_set_interlock_mode(bool enable);

/**
 * @brief Configurar timeout de operación
 *
 * @param timeout_ms Nuevo timeout en milisegundos
 * @return ESP_OK en éxito
 */
esp_err_t valve_control_driver_set_operation_timeout(uint16_t timeout_ms);

/**
 * @brief Obtener tiempo de operación de válvula
 *
 * @param valve_number Número de válvula (1-3)
 * @return Tiempo en milisegundos que lleva abierta la válvula
 */
uint32_t valve_control_driver_get_valve_open_time(uint8_t valve_number);

/**
 * @brief Verificar salud del sistema de válvulas
 *
 * Realiza checks de salud de hardware y estado del sistema.
 *
 * @return ESP_OK si sistema está saludable
 */
esp_err_t valve_control_driver_health_check(void);

/**
 * @brief Obtener estadísticas del sistema
 *
 * @param stats Puntero donde escribir estadísticas
 */
void valve_control_driver_get_statistics(valve_system_statistics_t* stats);

/**
 * @brief Resetear estadísticas del sistema
 */
void valve_control_driver_reset_statistics(void);

/**
 * @brief Obtener configuración actual
 *
 * @param config Puntero donde escribir configuración
 */
void valve_control_driver_get_config(valve_control_config_t* config);

/**
 * @brief Actualizar configuración
 *
 * @param config Nueva configuración
 * @return ESP_OK en éxito
 */
esp_err_t valve_control_driver_set_config(const valve_control_config_t* config);

/**
 * @brief Habilitar/deshabilitar monitoreo de estado
 *
 * @param enable true para habilitar monitoreo
 * @return ESP_OK en éxito
 */
esp_err_t valve_control_driver_set_monitoring(bool enable);

/**
 * @brief Callback para manejo de eventos de válvulas
 *
 * @param valve_number Válvula que generó evento
 * @param event_type Tipo de evento
 * @param event_data Datos adicionales del evento
 */
typedef void (*valve_event_callback_t)(uint8_t valve_number, const char* event_type, void* event_data);

/**
 * @brief Registrar callback para eventos de válvulas
 *
 * @param callback Función callback a registrar
 * @return ESP_OK en éxito
 */
esp_err_t valve_control_driver_register_callback(valve_event_callback_t callback);

/**
 * @brief Convertir estado de válvula a string
 *
 * @param state Estado de válvula
 * @return String correspondiente al estado
 */
const char* valve_control_driver_state_to_string(valve_state_t state);

/**
 * @brief Convertir error de válvula a string
 *
 * @param error_type Tipo de error
 * @return String correspondiente al error
 */
const char* valve_control_driver_error_to_string(valve_error_type_t error_type);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en milisegundos
 */
uint32_t valve_control_driver_get_current_timestamp_ms(void);

#endif // INFRASTRUCTURE_DRIVERS_VALVE_CONTROL_DRIVER_H