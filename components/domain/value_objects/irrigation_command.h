#ifndef DOMAIN_VALUE_OBJECTS_IRRIGATION_COMMAND_H
#define DOMAIN_VALUE_OBJECTS_IRRIGATION_COMMAND_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"

/**
 * @file irrigation_command.h
 * @brief Value Object para comandos de riego MQTT
 *
 * Estructura de datos para comandos de riego siguiendo principios DDD.
 * Representa un comando inmutable de control de riego recibido via MQTT.
 *
 * Especificaciones Técnicas - Comandos de Riego:
 * - Tipo de datos: struct compacta
 * - Total de memoria: 112 bytes
 * - Compatibilidad: JSON MQTT, validación integrada
 * - Comandos soportados: start_irrigation, stop_irrigation, emergency_stop
 * - Límites de seguridad: duración máxima 30 minutos, válvulas 1-3
 */

/**
 * @brief Tipos de comandos de riego soportados
 */
typedef enum {
    IRRIGATION_ACTION_UNKNOWN = 0,
    IRRIGATION_ACTION_START,
    IRRIGATION_ACTION_STOP,
    IRRIGATION_ACTION_EMERGENCY_STOP,
    IRRIGATION_ACTION_MAX
} irrigation_action_t;

/**
 * @brief Value Object para comandos de riego
 *
 * Estructura inmutable que encapsula un comando de control de riego
 * recibido via MQTT. Incluye validación y parsing de JSON.
 */
typedef struct {
    irrigation_action_t action;           // Tipo de acción a ejecutar
    uint8_t valve_number;                 // Número de válvula (1-3)
    uint16_t duration_minutes;            // Duración en minutos (0-30)
    bool override_safety;                 // Permitir override de seguridad
    uint64_t timestamp;                   // Timestamp del comando
    char reason[64];                      // Razón del comando
    char source_mac[18];                  // MAC address del dispositivo objetivo
} irrigation_command_t;

/**
 * @brief Constantes de validación para comandos de riego
 */
#define IRRIGATION_COMMAND_MAX_DURATION_MINUTES     30
#define IRRIGATION_COMMAND_MIN_VALVE_NUMBER         1
#define IRRIGATION_COMMAND_MAX_VALVE_NUMBER         3
#define IRRIGATION_COMMAND_REASON_MAX_LENGTH        63
#define IRRIGATION_COMMAND_MAC_LENGTH               17

/**
 * @brief Strings de comandos válidos para parsing JSON
 */
#define IRRIGATION_COMMAND_START_STR        "start_irrigation"
#define IRRIGATION_COMMAND_STOP_STR         "stop_irrigation"
#define IRRIGATION_COMMAND_EMERGENCY_STR    "emergency_stop"

/**
 * @brief Validar si un comando de riego es válido
 *
 * Verifica que todos los campos del comando estén dentro de rangos
 * seguros y cumplan con las especificaciones del sistema.
 *
 * @param cmd Puntero al comando de riego a validar
 * @return true si el comando es válido, false en caso contrario
 */
bool irrigation_command_is_valid(const irrigation_command_t* cmd);

/**
 * @brief Validar número de válvula
 *
 * @param valve_number Número de válvula a validar
 * @return true si el número de válvula es válido (1-3)
 */
bool irrigation_command_is_valve_valid(uint8_t valve_number);

/**
 * @brief Validar duración de riego
 *
 * @param duration_minutes Duración en minutos a validar
 * @return true si la duración está dentro del rango permitido (0-30)
 */
bool irrigation_command_is_duration_valid(uint16_t duration_minutes);

/**
 * @brief Validar dirección MAC
 *
 * @param mac_address String de dirección MAC a validar
 * @return true si la dirección MAC tiene formato válido
 */
bool irrigation_command_is_mac_valid(const char* mac_address);

/**
 * @brief Parsear comando de riego desde JSON
 *
 * Convierte un string JSON en una estructura irrigation_command_t.
 * Realiza validación completa de todos los campos.
 *
 * Formato JSON esperado:
 * {
 *   "command": "start_irrigation",
 *   "valve_number": 1,
 *   "duration_minutes": 15,
 *   "override_safety": false,
 *   "reason": "manual_irrigation",
 *   "mac_address": "AA:BB:CC:DD:EE:FF",
 *   "timestamp": 1640995200
 * }
 *
 * @param json_str String JSON del comando
 * @param cmd Puntero al comando donde almacenar el resultado
 * @return ESP_OK en éxito, ESP_ERR_INVALID_ARG si el JSON es inválido
 */
esp_err_t irrigation_command_from_json(const char* json_str, irrigation_command_t* cmd);

/**
 * @brief Convertir comando de riego a JSON
 *
 * Serializa una estructura irrigation_command_t a JSON string.
 *
 * @param cmd Puntero al comando de riego
 * @param json_buffer Buffer donde escribir el JSON
 * @param buffer_size Tamaño del buffer JSON
 * @return ESP_OK en éxito, ESP_ERR_INVALID_SIZE si el buffer es pequeño
 */
esp_err_t irrigation_command_to_json(const irrigation_command_t* cmd,
                                   char* json_buffer,
                                   size_t buffer_size);

/**
 * @brief Convertir acción de riego a string
 *
 * @param action Acción de riego
 * @return String correspondiente a la acción, "unknown" si es inválida
 */
const char* irrigation_command_action_to_string(irrigation_action_t action);

/**
 * @brief Convertir string a acción de riego
 *
 * @param action_str String de la acción
 * @return Acción de riego correspondiente, IRRIGATION_ACTION_UNKNOWN si es inválida
 */
irrigation_action_t irrigation_command_string_to_action(const char* action_str);

/**
 * @brief Crear comando de riego con valores por defecto
 *
 * Inicializa un comando de riego con valores seguros por defecto.
 *
 * @param cmd Puntero al comando a inicializar
 */
void irrigation_command_init_default(irrigation_command_t* cmd);

/**
 * @brief Crear comando de inicio de riego
 *
 * Helper function para crear comandos de inicio de riego con parámetros comunes.
 *
 * @param cmd Puntero al comando a crear
 * @param valve_number Número de válvula (1-3)
 * @param duration_minutes Duración en minutos (1-30)
 * @param reason Razón del comando
 * @param mac_address MAC address del dispositivo
 * @return ESP_OK en éxito, ESP_ERR_INVALID_ARG si los parámetros son inválidos
 */
esp_err_t irrigation_command_create_start(irrigation_command_t* cmd,
                                        uint8_t valve_number,
                                        uint16_t duration_minutes,
                                        const char* reason,
                                        const char* mac_address);

/**
 * @brief Crear comando de parada de riego
 *
 * Helper function para crear comandos de parada de riego.
 *
 * @param cmd Puntero al comando a crear
 * @param valve_number Número de válvula (1-3)
 * @param reason Razón del comando
 * @param mac_address MAC address del dispositivo
 * @return ESP_OK en éxito, ESP_ERR_INVALID_ARG si los parámetros son inválidos
 */
esp_err_t irrigation_command_create_stop(irrigation_command_t* cmd,
                                       uint8_t valve_number,
                                       const char* reason,
                                       const char* mac_address);

/**
 * @brief Crear comando de parada de emergencia
 *
 * Helper function para crear comandos de parada de emergencia.
 *
 * @param cmd Puntero al comando a crear
 * @param reason Razón de la emergencia
 * @param mac_address MAC address del dispositivo
 * @return ESP_OK en éxito, ESP_ERR_INVALID_ARG si los parámetros son inválidos
 */
esp_err_t irrigation_command_create_emergency_stop(irrigation_command_t* cmd,
                                                 const char* reason,
                                                 const char* mac_address);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en segundos desde epoch Unix
 */
uint64_t irrigation_command_get_current_timestamp(void);

#endif // DOMAIN_VALUE_OBJECTS_IRRIGATION_COMMAND_H