#ifndef APPLICATION_USE_CASES_PROCESS_MQTT_COMMANDS_H
#define APPLICATION_USE_CASES_PROCESS_MQTT_COMMANDS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "irrigation_command.h"
#include "irrigation_status.h"
#include "ambient_sensor_data.h"
#include "soil_sensor_data.h"
#include "safety_limits.h"

/**
 * @file process_mqtt_commands.h
 * @brief Use Case para procesamiento de comandos MQTT de riego
 *
 * Maneja la recepción, validación y ejecución de comandos de riego
 * recibidos por MQTT siguiendo las validaciones de seguridad y
 * el contexto actual del sistema.
 *
 * Especificaciones Técnicas - Process MQTT Commands Use Case:
 * - Parsing seguro: Validación JSON y estructura de comandos
 * - Autorización: Verificación MAC address y contexto válido
 * - Safety first: Validación completa antes de ejecutar
 * - Idempotencia: Mismo comando no debe ejecutar múltiples veces
 * - Audit trail: Log completo de comandos recibidos y procesados
 */

/**
 * @brief Tipos de comandos MQTT soportados
 */
typedef enum {
    MQTT_COMMAND_TYPE_START_IRRIGATION = 0,    // Iniciar riego específico
    MQTT_COMMAND_TYPE_STOP_IRRIGATION,         // Detener riego específico
    MQTT_COMMAND_TYPE_EMERGENCY_STOP,          // Parada de emergencia
    MQTT_COMMAND_TYPE_GET_STATUS,              // Solicitar estado actual
    MQTT_COMMAND_TYPE_SET_CONFIG,              // Actualizar configuración
    MQTT_COMMAND_TYPE_HEALTH_CHECK,            // Check de salud del sistema
    MQTT_COMMAND_TYPE_UNKNOWN,                 // Comando no reconocido
    MQTT_COMMAND_TYPE_MAX
} mqtt_command_type_t;

/**
 * @brief Estados de validación de comandos MQTT
 */
typedef enum {
    MQTT_VALIDATION_SUCCESS = 0,           // Comando válido y autorizado
    MQTT_VALIDATION_INVALID_JSON,          // JSON malformado
    MQTT_VALIDATION_MISSING_FIELDS,        // Campos requeridos faltantes
    MQTT_VALIDATION_INVALID_MAC,           // MAC address incorrecto
    MQTT_VALIDATION_UNKNOWN_COMMAND,       // Tipo de comando desconocido
    MQTT_VALIDATION_UNAUTHORIZED,          // Comando no autorizado
    MQTT_VALIDATION_SAFETY_BLOCKED,        // Bloqueado por seguridad
    MQTT_VALIDATION_SYSTEM_BUSY,           // Sistema ocupado
    MQTT_VALIDATION_MAX
} mqtt_command_validation_t;

/**
 * @brief Resultado del procesamiento de comando MQTT
 */
typedef enum {
    MQTT_PROCESSING_SUCCESS = 0,           // Comando procesado exitosamente
    MQTT_PROCESSING_VALIDATION_FAILED,     // Falló validación
    MQTT_PROCESSING_EXECUTION_FAILED,      // Falló ejecución
    MQTT_PROCESSING_PARTIAL_SUCCESS,       // Éxito parcial con advertencias
    MQTT_PROCESSING_SAFETY_ABORT,          // Abortado por seguridad
    MQTT_PROCESSING_TIMEOUT,               // Timeout en procesamiento
    MQTT_PROCESSING_SYSTEM_ERROR,          // Error general del sistema
    MQTT_PROCESSING_MAX
} mqtt_processing_result_t;

/**
 * @brief Contexto de procesamiento de comando MQTT
 */
typedef struct {
    char device_mac_address[18];           // MAC address del dispositivo
    char source_topic[64];                 // Tópico MQTT de origen
    uint32_t command_timestamp;            // Timestamp del comando
    uint32_t received_timestamp;           // Cuándo se recibió
    bool is_offline_mode;                  // Si está en modo offline
    uint32_t last_command_timestamp;       // Último comando procesado
    uint8_t command_sequence_number;       // Número de secuencia
} mqtt_command_context_t;

/**
 * @brief Resultado detallado de procesamiento
 */
typedef struct {
    mqtt_processing_result_t result_code;  // Código de resultado
    mqtt_command_validation_t validation_result; // Resultado de validación
    char error_description[128];           // Descripción del error
    char response_message[256];            // Mensaje de respuesta
    uint32_t processing_time_ms;           // Tiempo de procesamiento
    bool requires_status_update;           // Si requiere actualizar estado
    bool requires_response;                // Si requiere respuesta MQTT
} mqtt_processing_details_t;

/**
 * @brief Configuración para procesamiento de comandos MQTT
 */
typedef struct {
    bool strict_mac_validation;           // Validación estricta de MAC
    uint16_t command_timeout_ms;           // Timeout para procesar comando
    uint16_t duplicate_window_seconds;     // Ventana para detectar duplicados
    bool allow_offline_commands;          // Permitir comandos en modo offline
    bool enable_command_logging;          // Habilitar logging de comandos
    uint8_t max_concurrent_commands;       // Máximo comandos concurrentes
} mqtt_command_processing_config_t;

/**
 * @brief Estadísticas de procesamiento de comandos
 */
typedef struct {
    uint32_t total_commands_received;      // Total comandos recibidos
    uint32_t successful_commands;          // Comandos exitosos
    uint32_t failed_commands;              // Comandos fallidos
    uint32_t validation_failures;          // Fallas de validación
    uint32_t safety_blocks;                // Bloqueados por seguridad
    uint32_t duplicate_commands;           // Comandos duplicados detectados
    uint32_t emergency_stops_received;     // Paradas de emergencia recibidas
    float average_processing_time_ms;      // Tiempo promedio de procesamiento
} mqtt_command_statistics_t;

/**
 * @brief Inicializar use case de procesamiento de comandos MQTT
 *
 * @param device_mac_address MAC address del dispositivo
 * @param safety_limits Límites de seguridad del sistema
 * @return ESP_OK en éxito
 */
esp_err_t process_mqtt_commands_init(
    const char* device_mac_address,
    const safety_limits_t* safety_limits
);

/**
 * @brief Procesar comando MQTT recibido
 *
 * Función principal que recibe un mensaje MQTT crudo, lo valida,
 * parsea y ejecuta según el tipo de comando.
 *
 * @param mqtt_message Mensaje MQTT crudo (JSON)
 * @param message_length Longitud del mensaje
 * @param topic Tópico MQTT donde se recibió
 * @param context Contexto del comando
 * @return Resultado detallado del procesamiento
 */
mqtt_processing_details_t process_mqtt_commands_handle_message(
    const char* mqtt_message,
    size_t message_length,
    const char* topic,
    const mqtt_command_context_t* context
);

/**
 * @brief Parsear mensaje MQTT a comando de riego
 *
 * Convierte JSON crudo a estructura de comando validada.
 *
 * @param mqtt_message Mensaje MQTT en formato JSON
 * @param message_length Longitud del mensaje
 * @param command Puntero donde escribir comando parseado
 * @param command_type Puntero donde escribir tipo de comando
 * @return Resultado de validación
 */
mqtt_command_validation_t process_mqtt_commands_parse_message(
    const char* mqtt_message,
    size_t message_length,
    irrigation_command_t* command,
    mqtt_command_type_t* command_type
);

/**
 * @brief Validar comando MQTT
 *
 * Verifica autorización, integridad y contexto del comando.
 *
 * @param command Comando de riego a validar
 * @param command_type Tipo de comando
 * @param context Contexto del comando
 * @param current_status Estado actual del sistema
 * @return Resultado de validación
 */
mqtt_command_validation_t process_mqtt_commands_validate_command(
    const irrigation_command_t* command,
    mqtt_command_type_t command_type,
    const mqtt_command_context_t* context,
    const irrigation_status_t* current_status
);

/**
 * @brief Ejecutar comando de inicio de riego
 *
 * @param command Comando de riego validado
 * @param context Contexto del comando
 * @return ESP_OK en éxito
 */
esp_err_t process_mqtt_commands_execute_start_irrigation(
    const irrigation_command_t* command,
    const mqtt_command_context_t* context
);

/**
 * @brief Ejecutar comando de parada de riego
 *
 * @param command Comando de riego validado
 * @param context Contexto del comando
 * @return ESP_OK en éxito
 */
esp_err_t process_mqtt_commands_execute_stop_irrigation(
    const irrigation_command_t* command,
    const mqtt_command_context_t* context
);

/**
 * @brief Ejecutar comando de parada de emergencia
 *
 * @param context Contexto del comando
 * @param reason Razón de la emergencia
 * @return ESP_OK en éxito
 */
esp_err_t process_mqtt_commands_execute_emergency_stop(
    const mqtt_command_context_t* context,
    const char* reason
);

/**
 * @brief Ejecutar comando de solicitud de estado
 *
 * @param context Contexto del comando
 * @return ESP_OK en éxito
 */
esp_err_t process_mqtt_commands_execute_get_status(
    const mqtt_command_context_t* context
);

/**
 * @brief Verificar si el comando es duplicado
 *
 * Detecta comandos duplicados basándose en timestamp y secuencia.
 *
 * @param command Comando a verificar
 * @param context Contexto del comando
 * @return true si es comando duplicado
 */
bool process_mqtt_commands_is_duplicate(
    const irrigation_command_t* command,
    const mqtt_command_context_t* context
);

/**
 * @brief Verificar autorización del comando
 *
 * Valida MAC address y permisos para el comando específico.
 *
 * @param command Comando a autorizar
 * @param context Contexto del comando
 * @return true si está autorizado
 */
bool process_mqtt_commands_is_authorized(
    const irrigation_command_t* command,
    const mqtt_command_context_t* context
);

/**
 * @brief Generar respuesta MQTT para comando procesado
 *
 * @param processing_result Resultado del procesamiento
 * @param original_command Comando original
 * @param response_buffer Buffer para respuesta
 * @param buffer_size Tamaño del buffer
 * @return ESP_OK en éxito
 */
esp_err_t process_mqtt_commands_generate_response(
    const mqtt_processing_details_t* processing_result,
    const irrigation_command_t* original_command,
    char* response_buffer,
    size_t buffer_size
);

/**
 * @brief Publicar respuesta MQTT
 *
 * @param response_message Mensaje de respuesta
 * @param context Contexto original del comando
 * @return ESP_OK en éxito
 */
esp_err_t process_mqtt_commands_publish_response(
    const char* response_message,
    const mqtt_command_context_t* context
);

/**
 * @brief Registrar evento de comando para auditoría
 *
 * @param command Comando procesado
 * @param context Contexto del comando
 * @param result Resultado del procesamiento
 */
void process_mqtt_commands_log_event(
    const irrigation_command_t* command,
    const mqtt_command_context_t* context,
    const mqtt_processing_details_t* result
);

/**
 * @brief Configurar procesamiento de comandos
 *
 * @param config Nueva configuración
 * @return ESP_OK en éxito
 */
esp_err_t process_mqtt_commands_set_config(const mqtt_command_processing_config_t* config);

/**
 * @brief Obtener configuración actual
 *
 * @param config Puntero donde escribir configuración
 */
void process_mqtt_commands_get_config(mqtt_command_processing_config_t* config);

/**
 * @brief Obtener estadísticas de procesamiento
 *
 * @param stats Puntero donde escribir estadísticas
 */
void process_mqtt_commands_get_statistics(mqtt_command_statistics_t* stats);

/**
 * @brief Resetear estadísticas
 */
void process_mqtt_commands_reset_statistics(void);

/**
 * @brief Convertir tipo de comando a string
 *
 * @param command_type Tipo de comando
 * @return String correspondiente
 */
const char* process_mqtt_commands_type_to_string(mqtt_command_type_t command_type);

/**
 * @brief Convertir resultado de validación a string
 *
 * @param validation_result Resultado de validación
 * @return String correspondiente
 */
const char* process_mqtt_commands_validation_to_string(mqtt_command_validation_t validation_result);

/**
 * @brief Convertir resultado de procesamiento a string
 *
 * @param processing_result Resultado de procesamiento
 * @return String correspondiente
 */
const char* process_mqtt_commands_processing_result_to_string(mqtt_processing_result_t processing_result);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en segundos desde epoch Unix
 */
uint32_t process_mqtt_commands_get_current_timestamp(void);

#endif // APPLICATION_USE_CASES_PROCESS_MQTT_COMMANDS_H