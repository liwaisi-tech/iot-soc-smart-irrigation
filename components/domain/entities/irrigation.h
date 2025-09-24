#ifndef DOMAIN_ENTITIES_IRRIGATION_H
#define DOMAIN_ENTITIES_IRRIGATION_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "irrigation_status.h"
#include "irrigation_command.h"
#include "safety_limits.h"

/**
 * @file irrigation.h
 * @brief Entidad de dominio para el sistema de riego
 *
 * Representa la entidad central del sistema de riego siguiendo principios DDD.
 * Encapsula toda la lógica de negocio y el estado del sistema de irrigación.
 *
 * La entidad Irrigation mantiene identidad durante todo el ciclo de vida
 * del sistema y coordina las operaciones de riego de manera segura.
 *
 * Especificaciones Técnicas - Entidad Irrigation:
 * - Identidad única: MAC address del dispositivo
 * - Estado mutable: Controlado por reglas de negocio
 * - Invariantes: Límites de seguridad siempre respetados
 * - Operaciones: Solo cambios válidos según domain logic
 */

/**
 * @brief Estados posibles de la entidad de riego
 */
typedef enum {
    IRRIGATION_ENTITY_STATE_UNINITIALIZED = 0,
    IRRIGATION_ENTITY_STATE_IDLE,             // Sistema listo, sin riego activo
    IRRIGATION_ENTITY_STATE_EVALUATING,       // Analizando condiciones para riego
    IRRIGATION_ENTITY_STATE_IRRIGATING,       // Riego activo en progreso
    IRRIGATION_ENTITY_STATE_STOPPING,         // Deteniendo riego normalmente
    IRRIGATION_ENTITY_STATE_EMERGENCY_STOP,   // Parada de emergencia activa
    IRRIGATION_ENTITY_STATE_COOLDOWN,         // Período de espera post-riego
    IRRIGATION_ENTITY_STATE_MAINTENANCE,      // Modo mantenimiento
    IRRIGATION_ENTITY_STATE_ERROR,            // Estado de error
    IRRIGATION_ENTITY_STATE_MAX
} irrigation_entity_state_t;

/**
 * @brief Resultado de evaluación de riego
 */
typedef enum {
    IRRIGATION_EVALUATION_NO_ACTION = 0,      // No se requiere acción
    IRRIGATION_EVALUATION_START_NORMAL,       // Iniciar riego normal
    IRRIGATION_EVALUATION_START_EMERGENCY,    // Iniciar riego de emergencia
    IRRIGATION_EVALUATION_CONTINUE,           // Continuar riego actual
    IRRIGATION_EVALUATION_STOP_TARGET,        // Parar - objetivo alcanzado
    IRRIGATION_EVALUATION_STOP_TIMEOUT,       // Parar - tiempo límite
    IRRIGATION_EVALUATION_EMERGENCY_STOP,     // Parada de emergencia requerida
    IRRIGATION_EVALUATION_MAX
} irrigation_evaluation_result_t;

/**
 * @brief Historial de acciones de riego
 */
typedef struct {
    uint32_t timestamp;                       // Cuándo ocurrió la acción
    irrigation_action_t action;               // Qué acción se ejecutó
    uint8_t valve_number;                     // Válvula afectada
    uint16_t duration_minutes;                // Duración de la acción
    char reason[32];                          // Razón de la acción
} irrigation_action_history_t;

/**
 * @brief Entidad principal del sistema de riego
 *
 * Representa el sistema completo de riego como una entidad de dominio
 * que mantiene su identidad y estado a través del tiempo.
 */
typedef struct {
    // Identidad de la entidad
    char device_mac_address[18];              // Identidad única del dispositivo
    uint32_t entity_creation_timestamp;       // Cuándo se creó la entidad

    // Estado actual
    irrigation_entity_state_t current_state;  // Estado actual de la entidad
    irrigation_status_t status;               // Estado detallado del sistema
    safety_limits_t safety_limits;            // Límites operacionales

    // Última evaluación
    irrigation_evaluation_result_t last_evaluation; // Resultado última evaluación
    uint32_t last_evaluation_timestamp;      // Cuándo se hizo la última evaluación

    // Historial de acciones (circular buffer)
    irrigation_action_history_t action_history[10]; // Últimas 10 acciones
    uint8_t history_write_index;              // Índice para escribir en historial
    uint8_t history_count;                    // Número de entradas en historial

    // Contadores y estadísticas
    uint32_t total_irrigation_sessions;       // Total de sesiones de riego
    uint32_t total_irrigation_minutes;        // Total de minutos de riego
    uint32_t emergency_stop_count;            // Número de paradas de emergencia
    uint32_t last_session_start_time;         // Inicio de la última sesión

    // Flags de estado
    bool is_initialized;                      // Si la entidad está inicializada
    bool safety_check_enabled;               // Si están activos los checks de seguridad
    bool automatic_evaluation_enabled;       // Si está activa evaluación automática
    bool manual_override_active;             // Si hay override manual activo

} irrigation_entity_t;

/**
 * @brief Strings para estados de la entidad
 */
#define IRRIGATION_ENTITY_STATE_IDLE_STR           "idle"
#define IRRIGATION_ENTITY_STATE_EVALUATING_STR     "evaluating"
#define IRRIGATION_ENTITY_STATE_IRRIGATING_STR     "irrigating"
#define IRRIGATION_ENTITY_STATE_STOPPING_STR       "stopping"
#define IRRIGATION_ENTITY_STATE_EMERGENCY_STR      "emergency_stop"
#define IRRIGATION_ENTITY_STATE_COOLDOWN_STR       "cooldown"
#define IRRIGATION_ENTITY_STATE_MAINTENANCE_STR    "maintenance"
#define IRRIGATION_ENTITY_STATE_ERROR_STR          "error"

/**
 * @brief Constantes para la entidad de riego
 */
#define IRRIGATION_ENTITY_HISTORY_SIZE             10
#define IRRIGATION_ENTITY_MAC_ADDRESS_LENGTH       17

/**
 * @brief Crear e inicializar entidad de riego
 *
 * Crea una nueva entidad de riego con identidad única basada en
 * la dirección MAC del dispositivo.
 *
 * @param entity Puntero a la entidad a inicializar
 * @param device_mac_address Dirección MAC del dispositivo (identidad)
 * @return ESP_OK en éxito, ESP_ERR_INVALID_ARG si parámetros inválidos
 */
esp_err_t irrigation_entity_create(irrigation_entity_t* entity, const char* device_mac_address);

/**
 * @brief Inicializar entidad con configuración por defecto
 *
 * @param entity Puntero a la entidad
 * @return ESP_OK en éxito
 */
esp_err_t irrigation_entity_init_default(irrigation_entity_t* entity);

/**
 * @brief Evaluar condiciones actuales y determinar acción de riego
 *
 * Método principal de la entidad que evalúa las condiciones de sensores
 * y determina qué acción de riego tomar según las reglas de negocio.
 *
 * @param entity Puntero a la entidad
 * @param ambient_data Datos de sensores ambientales
 * @param soil_data Datos de sensores de suelo
 * @return Resultado de la evaluación
 */
irrigation_evaluation_result_t irrigation_entity_evaluate_conditions(
    irrigation_entity_t* entity,
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data
);

/**
 * @brief Ejecutar comando de riego recibido
 *
 * Procesa un comando de riego recibido via MQTT y ejecuta la acción
 * correspondiente si pasa las validaciones de seguridad.
 *
 * @param entity Puntero a la entidad
 * @param command Comando de riego a ejecutar
 * @return ESP_OK en éxito, ESP_ERR_* en error
 */
esp_err_t irrigation_entity_execute_command(irrigation_entity_t* entity,
                                          const irrigation_command_t* command);

/**
 * @brief Iniciar riego en una válvula específica
 *
 * @param entity Puntero a la entidad
 * @param valve_number Número de válvula (1-3)
 * @param duration_minutes Duración en minutos
 * @param reason Razón del inicio
 * @return ESP_OK en éxito, ESP_ERR_* en error
 */
esp_err_t irrigation_entity_start_irrigation(irrigation_entity_t* entity,
                                           uint8_t valve_number,
                                           uint16_t duration_minutes,
                                           const char* reason);

/**
 * @brief Detener riego en una válvula específica
 *
 * @param entity Puntero a la entidad
 * @param valve_number Número de válvula (1-3)
 * @param reason Razón de la parada
 * @return ESP_OK en éxito, ESP_ERR_* en error
 */
esp_err_t irrigation_entity_stop_irrigation(irrigation_entity_t* entity,
                                          uint8_t valve_number,
                                          const char* reason);

/**
 * @brief Activar parada de emergencia
 *
 * Detiene inmediatamente todas las válvulas y activa el estado de emergencia.
 * Esta operación tiene máxima prioridad y no puede ser bloqueada.
 *
 * @param entity Puntero a la entidad
 * @param reason Razón de la emergencia
 * @return ESP_OK siempre (operación crítica)
 */
esp_err_t irrigation_entity_emergency_stop(irrigation_entity_t* entity, const char* reason);

/**
 * @brief Limpiar estado de emergencia
 *
 * @param entity Puntero a la entidad
 * @return ESP_OK en éxito
 */
esp_err_t irrigation_entity_clear_emergency(irrigation_entity_t* entity);

/**
 * @brief Verificar límites de seguridad
 *
 * Valida que el estado actual de la entidad cumple con todos
 * los límites de seguridad establecidos.
 *
 * @param entity Puntero a la entidad
 * @param ambient_data Datos de sensores ambientales
 * @param soil_data Datos de sensores de suelo
 * @return true si todos los límites se cumplen
 */
bool irrigation_entity_check_safety_limits(const irrigation_entity_t* entity,
                                          const ambient_sensor_data_t* ambient_data,
                                          const soil_sensor_data_t* soil_data);

/**
 * @brief Actualizar estado de la entidad
 *
 * Actualiza el estado interno basado en el tiempo transcurrido y
 * las condiciones actuales del sistema.
 *
 * @param entity Puntero a la entidad
 */
void irrigation_entity_update_state(irrigation_entity_t* entity);

/**
 * @brief Verificar si la entidad puede iniciar riego
 *
 * @param entity Puntero a la entidad
 * @param valve_number Número de válvula
 * @return true si puede iniciar riego
 */
bool irrigation_entity_can_start_irrigation(const irrigation_entity_t* entity, uint8_t valve_number);

/**
 * @brief Verificar si la entidad está en estado de error
 *
 * @param entity Puntero a la entidad
 * @return true si está en estado de error
 */
bool irrigation_entity_is_in_error_state(const irrigation_entity_t* entity);

/**
 * @brief Verificar si hay riego activo
 *
 * @param entity Puntero a la entidad
 * @return true si hay al menos una válvula activa
 */
bool irrigation_entity_has_active_irrigation(const irrigation_entity_t* entity);

/**
 * @brief Obtener tiempo restante de riego activo
 *
 * @param entity Puntero a la entidad
 * @param valve_number Número de válvula
 * @return Minutos restantes, 0 si no está activa
 */
uint16_t irrigation_entity_get_remaining_time(const irrigation_entity_t* entity, uint8_t valve_number);

/**
 * @brief Agregar acción al historial
 *
 * @param entity Puntero a la entidad
 * @param action Acción realizada
 * @param valve_number Válvula afectada
 * @param duration_minutes Duración de la acción
 * @param reason Razón de la acción
 */
void irrigation_entity_add_to_history(irrigation_entity_t* entity,
                                     irrigation_action_t action,
                                     uint8_t valve_number,
                                     uint16_t duration_minutes,
                                     const char* reason);

/**
 * @brief Obtener última acción del historial
 *
 * @param entity Puntero a la entidad
 * @return Puntero a la última acción, NULL si no hay historial
 */
const irrigation_action_history_t* irrigation_entity_get_last_action(const irrigation_entity_t* entity);

/**
 * @brief Activar/desactivar override manual
 *
 * @param entity Puntero a la entidad
 * @param active true para activar override
 */
void irrigation_entity_set_manual_override(irrigation_entity_t* entity, bool active);

/**
 * @brief Activar/desactivar modo mantenimiento
 *
 * @param entity Puntero a la entidad
 * @param maintenance_active true para activar mantenimiento
 * @return ESP_OK en éxito
 */
esp_err_t irrigation_entity_set_maintenance_mode(irrigation_entity_t* entity, bool maintenance_active);

/**
 * @brief Obtener estadísticas de la entidad
 *
 * @param entity Puntero a la entidad
 * @param total_sessions Puntero donde escribir total de sesiones
 * @param total_minutes Puntero donde escribir total de minutos
 * @param emergency_stops Puntero donde escribir número de emergencias
 */
void irrigation_entity_get_statistics(const irrigation_entity_t* entity,
                                     uint32_t* total_sessions,
                                     uint32_t* total_minutes,
                                     uint32_t* emergency_stops);

/**
 * @brief Convertir estado de entidad a string
 *
 * @param state Estado de la entidad
 * @return String correspondiente al estado
 */
const char* irrigation_entity_state_to_string(irrigation_entity_state_t state);

/**
 * @brief Convertir string a estado de entidad
 *
 * @param state_str String del estado
 * @return Estado de entidad correspondiente
 */
irrigation_entity_state_t irrigation_entity_string_to_state(const char* state_str);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en segundos desde epoch Unix
 */
uint32_t irrigation_entity_get_current_timestamp(void);

#endif // DOMAIN_ENTITIES_IRRIGATION_H