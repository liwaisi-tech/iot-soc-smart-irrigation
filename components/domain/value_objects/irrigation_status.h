#ifndef DOMAIN_VALUE_OBJECTS_IRRIGATION_STATUS_H
#define DOMAIN_VALUE_OBJECTS_IRRIGATION_STATUS_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"

/**
 * @file irrigation_status.h
 * @brief Value Object para estado del sistema de riego
 *
 * Estructura de datos para el estado actual del sistema de riego siguiendo
 * principios DDD. Representa el estado inmutable del sistema en un momento dado.
 *
 * Especificaciones Técnicas - Estado de Riego:
 * - Tipo de datos: struct compacta
 * - Total de memoria: 96 bytes
 * - Precisión timestamp: segundos Unix
 * - Estados: 3 válvulas independientes + estado sistema
 * - Límites de seguridad: duración máxima, cooldown entre sesiones
 */

/**
 * @brief Estados posibles del sistema de riego
 */
typedef enum {
    IRRIGATION_SYSTEM_IDLE = 0,           // Sistema inactivo
    IRRIGATION_SYSTEM_ACTIVE,             // Al menos una válvula activa
    IRRIGATION_SYSTEM_EMERGENCY_STOP,     // Parada de emergencia activa
    IRRIGATION_SYSTEM_OFFLINE,            // Modo offline activo
    IRRIGATION_SYSTEM_ERROR,              // Error en el sistema
    IRRIGATION_SYSTEM_MAINTENANCE,        // Modo mantenimiento
    IRRIGATION_SYSTEM_MAX
} irrigation_system_state_t;

/**
 * @brief Estados posibles de una válvula individual
 */
typedef enum {
    VALVE_STATE_CLOSED = 0,               // Válvula cerrada
    VALVE_STATE_OPEN,                     // Válvula abierta
    VALVE_STATE_OPENING,                  // Válvula abriéndose
    VALVE_STATE_CLOSING,                  // Válvula cerrándose
    VALVE_STATE_ERROR,                    // Error en válvula
    VALVE_STATE_MAINTENANCE,              // Válvula en mantenimiento
    VALVE_STATE_MAX
} valve_state_t;

/**
 * @brief Información de estado de una válvula individual
 */
typedef struct {
    valve_state_t state;                  // Estado actual de la válvula
    uint32_t start_timestamp;             // Cuando se activó (Unix timestamp)
    uint32_t planned_stop_timestamp;      // Cuando debe parar (Unix timestamp)
    uint16_t current_duration_minutes;    // Duración actual en minutos
    uint16_t max_duration_minutes;        // Duración máxima permitida
    uint32_t last_stop_timestamp;         // Última vez que se cerró
    char last_stop_reason[32];            // Razón de la última parada
} valve_status_t;

/**
 * @brief Value Object para estado completo del sistema de riego
 *
 * Estructura inmutable que encapsula el estado completo del sistema
 * de riego, incluyendo todas las válvulas y el estado general.
 */
typedef struct {
    irrigation_system_state_t system_state;    // Estado general del sistema
    valve_status_t valve_1;                    // Estado válvula 1
    valve_status_t valve_2;                    // Estado válvula 2
    valve_status_t valve_3;                    // Estado válvula 3
    uint32_t system_start_timestamp;           // Cuando se inició el sistema
    uint32_t last_sensor_reading_timestamp;    // Última lectura de sensores
    uint16_t active_valve_count;               // Número de válvulas activas
    bool emergency_stop_active;                // Flag de parada de emergencia
    bool offline_mode_active;                  // Flag de modo offline
    char system_status_message[64];            // Mensaje de estado del sistema
} irrigation_status_t;

/**
 * @brief Constantes para límites de seguridad
 */
#define IRRIGATION_STATUS_MAX_DURATION_MINUTES      30
#define IRRIGATION_STATUS_COOLDOWN_MINUTES          240    // 4 horas
#define IRRIGATION_STATUS_MAX_VALVES                3
#define IRRIGATION_STATUS_MESSAGE_MAX_LENGTH        63

/**
 * @brief Strings para estados del sistema
 */
#define IRRIGATION_SYSTEM_STATE_IDLE_STR           "idle"
#define IRRIGATION_SYSTEM_STATE_ACTIVE_STR         "active"
#define IRRIGATION_SYSTEM_STATE_EMERGENCY_STR      "emergency_stop"
#define IRRIGATION_SYSTEM_STATE_OFFLINE_STR        "offline"
#define IRRIGATION_SYSTEM_STATE_ERROR_STR          "error"
#define IRRIGATION_SYSTEM_STATE_MAINTENANCE_STR    "maintenance"

/**
 * @brief Strings para estados de válvula
 */
#define VALVE_STATE_CLOSED_STR                     "closed"
#define VALVE_STATE_OPEN_STR                       "open"
#define VALVE_STATE_OPENING_STR                    "opening"
#define VALVE_STATE_CLOSING_STR                    "closing"
#define VALVE_STATE_ERROR_STR                      "error"
#define VALVE_STATE_MAINTENANCE_STR                "maintenance"

/**
 * @brief Inicializar estado de riego con valores por defecto
 *
 * Configura un estado de riego limpio con todas las válvulas cerradas
 * y el sistema en estado idle.
 *
 * @param status Puntero al estado a inicializar
 */
void irrigation_status_init_default(irrigation_status_t* status);

/**
 * @brief Verificar si alguna válvula está activa
 *
 * @param status Puntero al estado del sistema
 * @return true si al menos una válvula está abierta
 */
bool irrigation_status_is_any_valve_active(const irrigation_status_t* status);

/**
 * @brief Verificar si una válvula específica está activa
 *
 * @param status Puntero al estado del sistema
 * @param valve_number Número de válvula (1-3)
 * @return true si la válvula está abierta
 */
bool irrigation_status_is_valve_active(const irrigation_status_t* status, uint8_t valve_number);

/**
 * @brief Verificar si una válvula ha excedido su duración máxima
 *
 * @param status Puntero al estado del sistema
 * @param valve_number Número de válvula (1-3)
 * @return true si la válvula ha excedido el tiempo límite
 */
bool irrigation_status_is_valve_timeout(const irrigation_status_t* status, uint8_t valve_number);

/**
 * @brief Verificar si una válvula está en período de cooldown
 *
 * Valida si ha pasado suficiente tiempo desde la última sesión de riego
 * para permitir una nueva activación.
 *
 * @param status Puntero al estado del sistema
 * @param valve_number Número de válvula (1-3)
 * @return true si la válvula está en cooldown
 */
bool irrigation_status_is_valve_in_cooldown(const irrigation_status_t* status, uint8_t valve_number);

/**
 * @brief Activar una válvula específica
 *
 * Cambia el estado de una válvula a activa y actualiza timestamps.
 *
 * @param status Puntero al estado del sistema
 * @param valve_number Número de válvula (1-3)
 * @param duration_minutes Duración planificada en minutos
 * @return ESP_OK en éxito, ESP_ERR_INVALID_ARG si parámetros inválidos
 */
esp_err_t irrigation_status_activate_valve(irrigation_status_t* status,
                                         uint8_t valve_number,
                                         uint16_t duration_minutes);

/**
 * @brief Desactivar una válvula específica
 *
 * Cambia el estado de una válvula a cerrada y registra la razón.
 *
 * @param status Puntero al estado del sistema
 * @param valve_number Número de válvula (1-3)
 * @param reason Razón de la desactivación
 * @return ESP_OK en éxito, ESP_ERR_INVALID_ARG si parámetros inválidos
 */
esp_err_t irrigation_status_deactivate_valve(irrigation_status_t* status,
                                           uint8_t valve_number,
                                           const char* reason);

/**
 * @brief Activar parada de emergencia
 *
 * Cierra todas las válvulas inmediatamente y activa el flag de emergencia.
 *
 * @param status Puntero al estado del sistema
 * @param reason Razón de la emergencia
 */
void irrigation_status_activate_emergency_stop(irrigation_status_t* status, const char* reason);

/**
 * @brief Desactivar parada de emergencia
 *
 * Limpia el flag de emergencia y permite operación normal.
 *
 * @param status Puntero al estado del sistema
 */
void irrigation_status_deactivate_emergency_stop(irrigation_status_t* status);

/**
 * @brief Activar modo offline
 *
 * @param status Puntero al estado del sistema
 */
void irrigation_status_activate_offline_mode(irrigation_status_t* status);

/**
 * @brief Desactivar modo offline
 *
 * @param status Puntero al estado del sistema
 */
void irrigation_status_deactivate_offline_mode(irrigation_status_t* status);

/**
 * @brief Actualizar timestamp de última lectura de sensores
 *
 * @param status Puntero al estado del sistema
 */
void irrigation_status_update_sensor_timestamp(irrigation_status_t* status);

/**
 * @brief Actualizar estado general del sistema
 *
 * Recalcula el estado general basado en el estado de las válvulas individuales.
 *
 * @param status Puntero al estado del sistema
 */
void irrigation_status_update_system_state(irrigation_status_t* status);

/**
 * @brief Obtener estado de una válvula específica
 *
 * @param status Puntero al estado del sistema
 * @param valve_number Número de válvula (1-3)
 * @return Puntero al estado de la válvula, NULL si número inválido
 */
valve_status_t* irrigation_status_get_valve(irrigation_status_t* status, uint8_t valve_number);

/**
 * @brief Obtener tiempo restante de una válvula activa
 *
 * @param status Puntero al estado del sistema
 * @param valve_number Número de válvula (1-3)
 * @return Minutos restantes, 0 si la válvula no está activa
 */
uint16_t irrigation_status_get_remaining_time(const irrigation_status_t* status, uint8_t valve_number);

/**
 * @brief Convertir estado del sistema a string
 *
 * @param state Estado del sistema
 * @return String correspondiente al estado
 */
const char* irrigation_status_system_state_to_string(irrigation_system_state_t state);

/**
 * @brief Convertir string a estado del sistema
 *
 * @param state_str String del estado
 * @return Estado del sistema correspondiente
 */
irrigation_system_state_t irrigation_status_string_to_system_state(const char* state_str);

/**
 * @brief Convertir estado de válvula a string
 *
 * @param state Estado de válvula
 * @return String correspondiente al estado
 */
const char* irrigation_status_valve_state_to_string(valve_state_t state);

/**
 * @brief Convertir string a estado de válvula
 *
 * @param state_str String del estado
 * @return Estado de válvula correspondiente
 */
valve_state_t irrigation_status_string_to_valve_state(const char* state_str);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en segundos desde epoch Unix
 */
uint32_t irrigation_status_get_current_timestamp(void);

#endif // DOMAIN_VALUE_OBJECTS_IRRIGATION_STATUS_H