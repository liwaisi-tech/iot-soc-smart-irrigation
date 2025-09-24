#ifndef APPLICATION_USE_CASES_CONTROL_IRRIGATION_H
#define APPLICATION_USE_CASES_CONTROL_IRRIGATION_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "irrigation_command.h"
#include "irrigation_status.h"
#include "irrigation.h"
#include "ambient_sensor_data.h"
#include "soil_sensor_data.h"
#include "safety_limits.h"

/**
 * @file control_irrigation.h
 * @brief Use Case principal para control de riego
 *
 * Implementa el caso de uso central del sistema que orquesta todas las
 * operaciones de riego siguiendo los principios de arquitectura hexagonal.
 * Coordina domain services y adapters de infraestructura.
 *
 * Especificaciones Técnicas - Control Irrigation Use Case:
 * - Orquestador principal: Coordina domain logic + infrastructure
 * - Evaluación continua: Cada 30 segundos en modo online
 * - Seguridad first: Validación safety antes de cualquier acción
 * - Estado consistente: Transiciones atómicas, rollback en fallos
 * - Dependency injection: Interfaces para testabilidad
 */

/**
 * @brief Resultado de ejecución del use case
 */
typedef enum {
    CONTROL_IRRIGATION_SUCCESS = 0,          // Operación exitosa
    CONTROL_IRRIGATION_NO_ACTION,            // No se requirió acción
    CONTROL_IRRIGATION_SAFETY_BLOCK,         // Bloqueado por seguridad
    CONTROL_IRRIGATION_SENSOR_ERROR,         // Error en sensores
    CONTROL_IRRIGATION_VALVE_ERROR,          // Error en válvulas
    CONTROL_IRRIGATION_COMMAND_INVALID,      // Comando inválido
    CONTROL_IRRIGATION_SYSTEM_ERROR,         // Error general del sistema
    CONTROL_IRRIGATION_MAX
} control_irrigation_result_t;

/**
 * @brief Contexto de ejecución del use case
 *
 * Contiene todas las dependencias y estado necesario para
 * la ejecución del caso de uso de control de riego.
 */
typedef struct {
    // Entidad principal de dominio
    irrigation_entity_t* irrigation_entity;

    // Dependencias de infraestructura (interfaces)
    struct {
        esp_err_t (*read_ambient_sensors)(ambient_sensor_data_t* data);
        esp_err_t (*read_soil_sensors)(soil_sensor_data_t* data);
        esp_err_t (*open_valve)(uint8_t valve_number);
        esp_err_t (*close_valve)(uint8_t valve_number);
        esp_err_t (*close_all_valves)(void);
        bool (*is_valve_open)(uint8_t valve_number);
    } hardware_adapter;

    struct {
        esp_err_t (*publish_status)(const irrigation_status_t* status);
        esp_err_t (*publish_event)(const char* event_type, const char* details);
    } mqtt_adapter;

    struct {
        void (*log_info)(const char* message);
        void (*log_warning)(const char* message);
        void (*log_error)(const char* message);
    } logging_adapter;

    // Configuración del use case
    struct {
        uint16_t evaluation_interval_ms;     // Intervalo evaluación (30000ms)
        uint16_t sensor_timeout_ms;          // Timeout lectura sensores (2000ms)
        uint16_t valve_response_timeout_ms;  // Timeout respuesta válvulas (5000ms)
        bool automatic_evaluation_enabled;   // Si está activa evaluación automática
        bool mqtt_publishing_enabled;        // Si está activa publicación MQTT
    } configuration;

    // Estado del use case
    struct {
        bool is_initialized;                 // Si el use case está inicializado
        uint32_t last_evaluation_timestamp;  // Última evaluación
        uint32_t evaluation_count;           // Número de evaluaciones realizadas
        uint32_t successful_operations;      // Operaciones exitosas
        uint32_t failed_operations;          // Operaciones fallidas
        control_irrigation_result_t last_result; // Resultado última operación
    } state;

} control_irrigation_context_t;

/**
 * @brief Resultado detallado de operación
 */
typedef struct {
    control_irrigation_result_t result_code;
    char description[128];                   // Descripción del resultado
    char action_taken[64];                   // Acción realizada
    uint32_t execution_time_ms;              // Tiempo de ejecución
    bool state_changed;                      // Si cambió el estado del sistema
} control_irrigation_operation_result_t;

/**
 * @brief Inicializar contexto del use case
 *
 * Configura todas las dependencias e inicializa el estado del use case.
 *
 * @param context Puntero al contexto a inicializar
 * @param device_mac_address MAC address del dispositivo
 * @param safety_limits Límites de seguridad del sistema
 * @return ESP_OK en éxito, ESP_ERR_* en error
 */
esp_err_t control_irrigation_init_context(
    control_irrigation_context_t* context,
    const char* device_mac_address,
    const safety_limits_t* safety_limits
);

/**
 * @brief Configurar adapters de hardware
 *
 * Inyecta las dependencias de hardware (sensores, válvulas).
 *
 * @param context Puntero al contexto
 * @param hardware_adapter Adapter de hardware configurado
 * @return ESP_OK en éxito
 */
esp_err_t control_irrigation_set_hardware_adapter(
    control_irrigation_context_t* context,
    const void* hardware_adapter
);

/**
 * @brief Configurar adapter MQTT
 *
 * @param context Puntero al contexto
 * @param mqtt_adapter Adapter MQTT configurado
 * @return ESP_OK en éxito
 */
esp_err_t control_irrigation_set_mqtt_adapter(
    control_irrigation_context_t* context,
    const void* mqtt_adapter
);

/**
 * @brief Ejecutar evaluación automática del sistema
 *
 * Función principal del use case que realiza evaluación completa
 * del estado del sistema y ejecuta acciones necesarias.
 *
 * Workflow:
 * 1. Leer sensores (ambiente + suelo)
 * 2. Evaluar condiciones con domain logic
 * 3. Validar seguridad
 * 4. Ejecutar acción recomendada
 * 5. Actualizar estado y publicar eventos
 *
 * @param context Contexto del use case
 * @return Resultado detallado de la operación
 */
control_irrigation_operation_result_t control_irrigation_execute_automatic_evaluation(
    control_irrigation_context_t* context
);

/**
 * @brief Procesar comando de riego recibido
 *
 * Procesa un comando específico de riego (start/stop/emergency_stop)
 * con validaciones de seguridad completas.
 *
 * @param context Contexto del use case
 * @param command Comando de riego a procesar
 * @return Resultado detallado de la operación
 */
control_irrigation_operation_result_t control_irrigation_process_command(
    control_irrigation_context_t* context,
    const irrigation_command_t* command
);

/**
 * @brief Ejecutar parada de emergencia
 *
 * Operación crítica que detiene inmediatamente todas las válvulas
 * y activa el estado de emergencia del sistema.
 *
 * @param context Contexto del use case
 * @param reason Razón de la emergencia
 * @return Resultado de la operación (siempre SUCCESS en operación crítica)
 */
control_irrigation_operation_result_t control_irrigation_execute_emergency_stop(
    control_irrigation_context_t* context,
    const char* reason
);

/**
 * @brief Iniciar riego en válvula específica
 *
 * @param context Contexto del use case
 * @param valve_number Número de válvula (1-3)
 * @param duration_minutes Duración en minutos
 * @param reason Razón del inicio
 * @return Resultado detallado de la operación
 */
control_irrigation_operation_result_t control_irrigation_start_valve(
    control_irrigation_context_t* context,
    uint8_t valve_number,
    uint16_t duration_minutes,
    const char* reason
);

/**
 * @brief Detener riego en válvula específica
 *
 * @param context Contexto del use case
 * @param valve_number Número de válvula (1-3)
 * @param reason Razón de la parada
 * @return Resultado detallado de la operación
 */
control_irrigation_operation_result_t control_irrigation_stop_valve(
    control_irrigation_context_t* context,
    uint8_t valve_number,
    const char* reason
);

/**
 * @brief Sincronizar estado de válvulas físicas
 *
 * Verifica que el estado reportado por el sistema coincida
 * con el estado físico real de las válvulas.
 *
 * @param context Contexto del use case
 * @return Resultado de la sincronización
 */
control_irrigation_operation_result_t control_irrigation_sync_valve_states(
    control_irrigation_context_t* context
);

/**
 * @brief Realizar check de salud del sistema
 *
 * Verifica el estado de sensores, válvulas y conectividad.
 *
 * @param context Contexto del use case
 * @return Resultado del check de salud
 */
control_irrigation_operation_result_t control_irrigation_health_check(
    control_irrigation_context_t* context
);

/**
 * @brief Leer datos completos de sensores
 *
 * Lee tanto sensores ambientales como de suelo con manejo de errores.
 *
 * @param context Contexto del use case
 * @param ambient_data Puntero donde escribir datos ambientales
 * @param soil_data Puntero donde escribir datos de suelo
 * @return ESP_OK si todas las lecturas fueron exitosas
 */
esp_err_t control_irrigation_read_all_sensors(
    control_irrigation_context_t* context,
    ambient_sensor_data_t* ambient_data,
    soil_sensor_data_t* soil_data
);

/**
 * @brief Publicar estado actual del sistema
 *
 * Publica el estado completo del sistema via MQTT para monitoreo remoto.
 *
 * @param context Contexto del use case
 * @return ESP_OK en éxito
 */
esp_err_t control_irrigation_publish_system_status(
    control_irrigation_context_t* context
);

/**
 * @brief Publicar evento de riego
 *
 * @param context Contexto del use case
 * @param event_type Tipo de evento ("irrigation_started", "irrigation_stopped", etc.)
 * @param valve_number Válvula afectada
 * @param details Detalles adicionales del evento
 * @return ESP_OK en éxito
 */
esp_err_t control_irrigation_publish_event(
    control_irrigation_context_t* context,
    const char* event_type,
    uint8_t valve_number,
    const char* details
);

/**
 * @brief Activar/desactivar evaluación automática
 *
 * @param context Contexto del use case
 * @param enabled true para activar evaluación automática
 */
void control_irrigation_set_automatic_evaluation(
    control_irrigation_context_t* context,
    bool enabled
);

/**
 * @brief Configurar intervalo de evaluación
 *
 * @param context Contexto del use case
 * @param interval_ms Nuevo intervalo en milisegundos
 * @return ESP_OK en éxito
 */
esp_err_t control_irrigation_set_evaluation_interval(
    control_irrigation_context_t* context,
    uint16_t interval_ms
);

/**
 * @brief Obtener estadísticas del use case
 *
 * @param context Contexto del use case
 * @param total_evaluations Puntero donde escribir total evaluaciones
 * @param successful_ops Puntero donde escribir operaciones exitosas
 * @param failed_ops Puntero donde escribir operaciones fallidas
 */
void control_irrigation_get_statistics(
    const control_irrigation_context_t* context,
    uint32_t* total_evaluations,
    uint32_t* successful_ops,
    uint32_t* failed_ops
);

/**
 * @brief Obtener estado actual del sistema
 *
 * @param context Contexto del use case
 * @return Puntero al estado actual de irrigación
 */
const irrigation_status_t* control_irrigation_get_current_status(
    const control_irrigation_context_t* context
);

/**
 * @brief Verificar si el use case está inicializado
 *
 * @param context Contexto del use case
 * @return true si está inicializado correctamente
 */
bool control_irrigation_is_initialized(const control_irrigation_context_t* context);

/**
 * @brief Verificar si hay riego activo
 *
 * @param context Contexto del use case
 * @return true si hay al menos una válvula activa
 */
bool control_irrigation_has_active_irrigation(const control_irrigation_context_t* context);

/**
 * @brief Obtener tiempo hasta próxima evaluación
 *
 * @param context Contexto del use case
 * @return Milisegundos hasta próxima evaluación automática
 */
uint32_t control_irrigation_time_to_next_evaluation(const control_irrigation_context_t* context);

/**
 * @brief Convertir resultado a string
 *
 * @param result Resultado de operación
 * @return String correspondiente al resultado
 */
const char* control_irrigation_result_to_string(control_irrigation_result_t result);

/**
 * @brief Limpiar y desinicializar contexto
 *
 * @param context Contexto del use case
 */
void control_irrigation_cleanup_context(control_irrigation_context_t* context);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en milisegundos desde epoch Unix
 */
uint32_t control_irrigation_get_current_timestamp_ms(void);

#endif // APPLICATION_USE_CASES_CONTROL_IRRIGATION_H