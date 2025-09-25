/**
 * @file process_mqtt_commands.c
 * @brief Implementación del procesamiento de comandos MQTT de riego
 *
 * Smart Irrigation System - ESP32 IoT Project
 * Hexagonal Architecture - Application Layer Use Case
 *
 * Procesa comandos MQTT de control de riego con validaciones de seguridad,
 * autorización y auditoría completa. Integra con el sistema de control
 * de riego existente manteniendo la arquitectura hexagonal.
 *
 * Autor: Claude + Liwaisi Tech Team
 * Versión: 1.4.0 - MQTT Command Processing Integration
 */

#include "process_mqtt_commands.h"
#include "control_irrigation.h"
#include "irrigation_logic.h"
#include "safety_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>
#include <inttypes.h>

static const char* TAG = "process_mqtt_commands";

// ============================================================================
// CONFIGURACIÓN Y ESTADO GLOBAL
// ============================================================================

/**
 * @brief Configuración por defecto del procesador de comandos
 */
static const mqtt_command_processing_config_t DEFAULT_CONFIG = {
    .strict_mac_validation = true,
    .command_timeout_ms = 5000,
    .duplicate_window_seconds = 30,
    .allow_offline_commands = false,
    .enable_command_logging = true,
    .max_concurrent_commands = 3
};

/**
 * @brief Estado global del procesador de comandos
 */
typedef struct {
    bool is_initialized;
    char device_mac_address[18];
    safety_limits_t safety_limits;
    mqtt_command_processing_config_t config;
    mqtt_command_statistics_t statistics;

    // Control de comandos duplicados
    irrigation_command_t last_commands[5];
    mqtt_command_context_t last_contexts[5];
    uint8_t command_history_index;

    // Control de concurrencia
    uint8_t active_commands_count;
    uint32_t processing_start_times[3];
} mqtt_command_processor_state_t;

static mqtt_command_processor_state_t g_processor_state = {0};

// ============================================================================
// FUNCIONES AUXILIARES INTERNAS
// ============================================================================

/**
 * @brief Obtener timestamp actual en segundos
 */
uint32_t process_mqtt_commands_get_current_timestamp(void) {
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

/**
 * @brief Extraer MAC address de tópico MQTT
 */
static bool extract_mac_from_topic(const char* topic, char* mac_buffer, size_t buffer_size) {
    if (!topic || !mac_buffer || buffer_size < 18) {
        return false;
    }

    // Buscar patrón: irrigation/control/{mac_address}
    const char* mac_start = strstr(topic, "irrigation/control/");
    if (!mac_start) {
        return false;
    }

    mac_start += strlen("irrigation/control/");

    // Validar formato MAC (AA:BB:CC:DD:EE:FF)
    if (strlen(mac_start) < 17) {
        return false;
    }

    strncpy(mac_buffer, mac_start, 17);
    mac_buffer[17] = '\0';

    // Validar formato MAC básico
    for (int i = 0; i < 17; i++) {
        if (i % 3 == 2) {
            if (mac_buffer[i] != ':') return false;
        } else {
            if (!((mac_buffer[i] >= '0' && mac_buffer[i] <= '9') ||
                  (mac_buffer[i] >= 'A' && mac_buffer[i] <= 'F') ||
                  (mac_buffer[i] >= 'a' && mac_buffer[i] <= 'f'))) {
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Parsear comando JSON a estructura irrigation_command_t
 */
static mqtt_command_validation_t parse_irrigation_command_json(
    const cJSON* json,
    irrigation_command_t* command,
    mqtt_command_type_t* command_type) {

    if (!json || !command || !command_type) {
        return MQTT_VALIDATION_INVALID_JSON;
    }

    // Parsear tipo de comando
    cJSON* command_type_json = cJSON_GetObjectItem(json, "command");
    if (!cJSON_IsString(command_type_json)) {
        return MQTT_VALIDATION_MISSING_FIELDS;
    }

    const char* command_str = command_type_json->valuestring;
    if (strcmp(command_str, "start_irrigation") == 0) {
        *command_type = MQTT_COMMAND_TYPE_START_IRRIGATION;
        command->command_type = IRRIGATION_COMMAND_START;
    } else if (strcmp(command_str, "stop_irrigation") == 0) {
        *command_type = MQTT_COMMAND_TYPE_STOP_IRRIGATION;
        command->command_type = IRRIGATION_COMMAND_STOP;
    } else if (strcmp(command_str, "emergency_stop") == 0) {
        *command_type = MQTT_COMMAND_TYPE_EMERGENCY_STOP;
        command->command_type = IRRIGATION_COMMAND_EMERGENCY_STOP;
    } else if (strcmp(command_str, "get_status") == 0) {
        *command_type = MQTT_COMMAND_TYPE_GET_STATUS;
        command->command_type = IRRIGATION_COMMAND_GET_STATUS;
    } else {
        return MQTT_VALIDATION_UNKNOWN_COMMAND;
    }

    // Parsear MAC address
    cJSON* mac_json = cJSON_GetObjectItem(json, "mac_address");
    if (!cJSON_IsString(mac_json)) {
        return MQTT_VALIDATION_MISSING_FIELDS;
    }
    strncpy(command->device_mac_address, mac_json->valuestring, 17);
    command->device_mac_address[17] = '\0';

    // Parsear timestamp
    cJSON* timestamp_json = cJSON_GetObjectItem(json, "timestamp");
    if (cJSON_IsNumber(timestamp_json)) {
        command->timestamp = (uint32_t)timestamp_json->valueint;
    } else {
        command->timestamp = process_mqtt_commands_get_current_timestamp();
    }

    // Parsear campos específicos del comando de riego
    if (*command_type == MQTT_COMMAND_TYPE_START_IRRIGATION) {
        // Válvula a controlar
        cJSON* valve_json = cJSON_GetObjectItem(json, "valve_number");
        if (cJSON_IsNumber(valve_json)) {
            command->valve_number = (uint8_t)valve_json->valueint;
        } else {
            command->valve_number = 1; // Default
        }

        // Duración en minutos
        cJSON* duration_json = cJSON_GetObjectItem(json, "duration_minutes");
        if (cJSON_IsNumber(duration_json)) {
            command->duration_minutes = (uint16_t)duration_json->valueint;
        } else {
            command->duration_minutes = 15; // Default 15 minutos
        }

        // Validar rango de duración
        if (command->duration_minutes == 0 || command->duration_minutes > 60) {
            return MQTT_VALIDATION_MISSING_FIELDS;
        }
    } else if (*command_type == MQTT_COMMAND_TYPE_STOP_IRRIGATION) {
        // Para stop, parsear válvula específica si se proporciona
        cJSON* valve_json = cJSON_GetObjectItem(json, "valve_number");
        if (cJSON_IsNumber(valve_json)) {
            command->valve_number = (uint8_t)valve_json->valueint;
        } else {
            command->valve_number = 0; // 0 = todas las válvulas
        }
    }

    // Parsear sequence number para detección de duplicados
    cJSON* sequence_json = cJSON_GetObjectItem(json, "sequence");
    if (cJSON_IsNumber(sequence_json)) {
        command->sequence_number = (uint16_t)sequence_json->valueint;
    } else {
        command->sequence_number = 0;
    }

    return MQTT_VALIDATION_SUCCESS;
}

/**
 * @brief Agregar comando al historial para detección de duplicados
 */
static void add_to_command_history(const irrigation_command_t* command, const mqtt_command_context_t* context) {
    if (!command || !context) return;

    uint8_t index = g_processor_state.command_history_index;

    memcpy(&g_processor_state.last_commands[index], command, sizeof(irrigation_command_t));
    memcpy(&g_processor_state.last_contexts[index], context, sizeof(mqtt_command_context_t));

    g_processor_state.command_history_index = (index + 1) % 5;
}

// ============================================================================
// FUNCIONES PÚBLICAS - INICIALIZACIÓN
// ============================================================================

esp_err_t process_mqtt_commands_init(const char* device_mac_address, const safety_limits_t* safety_limits) {
    if (!device_mac_address || !safety_limits) {
        ESP_LOGE(TAG, "Invalid parameters for initialization");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing MQTT command processor for device: %s", device_mac_address);

    // Limpiar estado
    memset(&g_processor_state, 0, sizeof(mqtt_command_processor_state_t));

    // Copiar configuración
    strncpy(g_processor_state.device_mac_address, device_mac_address, 17);
    g_processor_state.device_mac_address[17] = '\0';
    memcpy(&g_processor_state.safety_limits, safety_limits, sizeof(safety_limits_t));
    memcpy(&g_processor_state.config, &DEFAULT_CONFIG, sizeof(mqtt_command_processing_config_t));

    // Inicializar estadísticas
    memset(&g_processor_state.statistics, 0, sizeof(mqtt_command_statistics_t));

    g_processor_state.is_initialized = true;

    ESP_LOGI(TAG, "MQTT command processor initialized successfully");
    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - PROCESAMIENTO DE COMANDOS
// ============================================================================

mqtt_processing_details_t process_mqtt_commands_handle_message(
    const char* mqtt_message,
    size_t message_length,
    const char* topic,
    const mqtt_command_context_t* context) {

    mqtt_processing_details_t result = {0};
    result.processing_time_ms = esp_timer_get_time() / 1000;

    if (!g_processor_state.is_initialized) {
        result.result_code = MQTT_PROCESSING_SYSTEM_ERROR;
        strncpy(result.error_description, "Processor not initialized", sizeof(result.error_description) - 1);
        ESP_LOGE(TAG, "Processor not initialized");
        return result;
    }

    g_processor_state.statistics.total_commands_received++;

    ESP_LOGI(TAG, "Processing MQTT command from topic: %s", topic ? topic : "unknown");
    ESP_LOGD(TAG, "Message payload (%zu bytes): %.*s", message_length, (int)message_length, mqtt_message);

    // Verificar concurrencia
    if (g_processor_state.active_commands_count >= g_processor_state.config.max_concurrent_commands) {
        result.result_code = MQTT_PROCESSING_TIMEOUT;
        result.validation_result = MQTT_VALIDATION_SYSTEM_BUSY;
        strncpy(result.error_description, "System busy processing other commands", sizeof(result.error_description) - 1);
        g_processor_state.statistics.failed_commands++;
        ESP_LOGW(TAG, "System busy - rejecting command");
        return result;
    }

    g_processor_state.active_commands_count++;

    // Parsear mensaje JSON
    irrigation_command_t command = {0};
    mqtt_command_type_t command_type;

    result.validation_result = process_mqtt_commands_parse_message(
        mqtt_message, message_length, &command, &command_type);

    if (result.validation_result != MQTT_VALIDATION_SUCCESS) {
        result.result_code = MQTT_PROCESSING_VALIDATION_FAILED;
        snprintf(result.error_description, sizeof(result.error_description),
                 "Validation failed: %s", process_mqtt_commands_validation_to_string(result.validation_result));
        g_processor_state.statistics.failed_commands++;
        g_processor_state.statistics.validation_failures++;
        g_processor_state.active_commands_count--;
        ESP_LOGE(TAG, "Command validation failed: %s", result.error_description);
        return result;
    }

    // Verificar autorización
    if (!process_mqtt_commands_is_authorized(&command, context)) {
        result.result_code = MQTT_PROCESSING_VALIDATION_FAILED;
        result.validation_result = MQTT_VALIDATION_UNAUTHORIZED;
        strncpy(result.error_description, "Command not authorized", sizeof(result.error_description) - 1);
        g_processor_state.statistics.failed_commands++;
        g_processor_state.statistics.validation_failures++;
        g_processor_state.active_commands_count--;
        ESP_LOGW(TAG, "Unauthorized command from MAC: %s", command.device_mac_address);
        return result;
    }

    // Verificar duplicados
    if (process_mqtt_commands_is_duplicate(&command, context)) {
        result.result_code = MQTT_PROCESSING_PARTIAL_SUCCESS;
        strncpy(result.error_description, "Duplicate command ignored", sizeof(result.error_description) - 1);
        strncpy(result.response_message, "Command already processed", sizeof(result.response_message) - 1);
        g_processor_state.statistics.duplicate_commands++;
        g_processor_state.active_commands_count--;
        ESP_LOGI(TAG, "Duplicate command detected and ignored");
        return result;
    }

    // Ejecutar comando según el tipo
    esp_err_t execution_result = ESP_FAIL;

    switch (command_type) {
        case MQTT_COMMAND_TYPE_START_IRRIGATION:
            execution_result = process_mqtt_commands_execute_start_irrigation(&command, context);
            break;

        case MQTT_COMMAND_TYPE_STOP_IRRIGATION:
            execution_result = process_mqtt_commands_execute_stop_irrigation(&command, context);
            break;

        case MQTT_COMMAND_TYPE_EMERGENCY_STOP:
            execution_result = process_mqtt_commands_execute_emergency_stop(context, "MQTT emergency command");
            break;

        case MQTT_COMMAND_TYPE_GET_STATUS:
            execution_result = process_mqtt_commands_execute_get_status(context);
            result.requires_response = true;
            break;

        default:
            result.result_code = MQTT_PROCESSING_VALIDATION_FAILED;
            result.validation_result = MQTT_VALIDATION_UNKNOWN_COMMAND;
            strncpy(result.error_description, "Unknown command type", sizeof(result.error_description) - 1);
            execution_result = ESP_ERR_NOT_SUPPORTED;
            break;
    }

    // Procesar resultado de ejecución
    if (execution_result == ESP_OK) {
        result.result_code = MQTT_PROCESSING_SUCCESS;
        snprintf(result.response_message, sizeof(result.response_message),
                 "Command '%s' executed successfully", process_mqtt_commands_type_to_string(command_type));
        g_processor_state.statistics.successful_commands++;

        // Agregar al historial
        add_to_command_history(&command, context);

        ESP_LOGI(TAG, "Command executed successfully: %s", process_mqtt_commands_type_to_string(command_type));
    } else {
        result.result_code = MQTT_PROCESSING_EXECUTION_FAILED;
        snprintf(result.error_description, sizeof(result.error_description),
                 "Execution failed: %s", esp_err_to_name(execution_result));
        g_processor_state.statistics.failed_commands++;
        ESP_LOGE(TAG, "Command execution failed: %s", esp_err_to_name(execution_result));
    }

    // Calcular tiempo de procesamiento
    result.processing_time_ms = (esp_timer_get_time() / 1000) - result.processing_time_ms;

    // Actualizar estadísticas de tiempo promedio
    uint32_t total_successful = g_processor_state.statistics.successful_commands;
    if (total_successful > 0) {
        g_processor_state.statistics.average_processing_time_ms =
            ((g_processor_state.statistics.average_processing_time_ms * (total_successful - 1)) +
             result.processing_time_ms) / total_successful;
    }

    // Log del evento
    if (g_processor_state.config.enable_command_logging) {
        process_mqtt_commands_log_event(&command, context, &result);
    }

    g_processor_state.active_commands_count--;

    ESP_LOGD(TAG, "Command processing completed in %" PRIu32 "ms", result.processing_time_ms);
    return result;
}

mqtt_command_validation_t process_mqtt_commands_parse_message(
    const char* mqtt_message,
    size_t message_length,
    irrigation_command_t* command,
    mqtt_command_type_t* command_type) {

    if (!mqtt_message || message_length == 0 || !command || !command_type) {
        return MQTT_VALIDATION_INVALID_JSON;
    }

    // Parsear JSON
    cJSON* json = cJSON_ParseWithLength(mqtt_message, message_length);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON message");
        return MQTT_VALIDATION_INVALID_JSON;
    }

    mqtt_command_validation_t validation_result = parse_irrigation_command_json(json, command, command_type);

    cJSON_Delete(json);
    return validation_result;
}

mqtt_command_validation_t process_mqtt_commands_validate_command(
    const irrigation_command_t* command,
    mqtt_command_type_t command_type,
    const mqtt_command_context_t* context,
    const irrigation_status_t* current_status) {

    if (!command || !context) {
        return MQTT_VALIDATION_MISSING_FIELDS;
    }

    // Validar MAC address
    if (g_processor_state.config.strict_mac_validation) {
        if (strcmp(command->device_mac_address, g_processor_state.device_mac_address) != 0) {
            return MQTT_VALIDATION_INVALID_MAC;
        }
    }

    // Validar timestamp (no muy antiguo)
    uint32_t current_time = process_mqtt_commands_get_current_timestamp();
    if (current_time > command->timestamp &&
        (current_time - command->timestamp) > 300) { // 5 minutos máximo
        return MQTT_VALIDATION_SAFETY_BLOCKED;
    }

    // Validaciones específicas por tipo de comando
    if (command_type == MQTT_COMMAND_TYPE_START_IRRIGATION) {
        // Validar número de válvula
        if (command->valve_number < 1 || command->valve_number > 3) {
            return MQTT_VALIDATION_MISSING_FIELDS;
        }

        // Validar duración
        if (command->duration_minutes > g_processor_state.safety_limits.max_irrigation_duration_minutes) {
            return MQTT_VALIDATION_SAFETY_BLOCKED;
        }

        // Verificar si el sistema permite comandos en modo offline
        if (context->is_offline_mode && !g_processor_state.config.allow_offline_commands) {
            return MQTT_VALIDATION_SYSTEM_BUSY;
        }
    }

    return MQTT_VALIDATION_SUCCESS;
}

// ============================================================================
// FUNCIONES PÚBLICAS - EJECUCIÓN DE COMANDOS
// ============================================================================

esp_err_t process_mqtt_commands_execute_start_irrigation(
    const irrigation_command_t* command,
    const mqtt_command_context_t* context) {

    if (!command || !context) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Executing start irrigation: valve=%d, duration=%d min",
             command->valve_number, command->duration_minutes);

    // Crear parámetros para el control de riego
    irrigation_request_t irrigation_request = {
        .valve_number = command->valve_number,
        .duration_minutes = command->duration_minutes,
        .irrigation_mode = IRRIGATION_MODE_MANUAL,
        .request_source = IRRIGATION_SOURCE_MQTT,
        .safety_override = false,
        .force_start = false
    };

    // Llamar al use case de control de riego existente
    esp_err_t result = control_irrigation_start_irrigation(&irrigation_request);

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Irrigation started successfully via MQTT command");
    } else {
        ESP_LOGE(TAG, "Failed to start irrigation via MQTT: %s", esp_err_to_name(result));
    }

    return result;
}

esp_err_t process_mqtt_commands_execute_stop_irrigation(
    const irrigation_command_t* command,
    const mqtt_command_context_t* context) {

    if (!command || !context) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Executing stop irrigation: valve=%d", command->valve_number);

    // Parámetros para parar el riego
    irrigation_stop_request_t stop_request = {
        .valve_number = command->valve_number, // 0 = todas las válvulas
        .stop_reason = "MQTT stop command",
        .immediate_stop = false,
        .request_source = IRRIGATION_SOURCE_MQTT
    };

    // Llamar al use case de control de riego existente
    esp_err_t result = control_irrigation_stop_irrigation(&stop_request);

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Irrigation stopped successfully via MQTT command");
    } else {
        ESP_LOGE(TAG, "Failed to stop irrigation via MQTT: %s", esp_err_to_name(result));
    }

    return result;
}

esp_err_t process_mqtt_commands_execute_emergency_stop(
    const mqtt_command_context_t* context,
    const char* reason) {

    if (!context) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGW(TAG, "Executing emergency stop: %s", reason ? reason : "MQTT emergency command");

    // Incrementar contador de emergencias
    g_processor_state.statistics.emergency_stops_received++;

    // Llamar al use case de control de riego para parada de emergencia
    esp_err_t result = control_irrigation_emergency_stop(reason ? reason : "MQTT emergency stop");

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Emergency stop executed successfully via MQTT command");
    } else {
        ESP_LOGE(TAG, "Failed to execute emergency stop via MQTT: %s", esp_err_to_name(result));
    }

    return result;
}

esp_err_t process_mqtt_commands_execute_get_status(const mqtt_command_context_t* context) {
    if (!context) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Executing get status request");

    // Este comando será implementado cuando se integre con el adaptador MQTT
    // para generar y enviar la respuesta de estado

    ESP_LOGI(TAG, "Status request processed - response will be sent via MQTT");
    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - VALIDACIONES Y UTILIDADES
// ============================================================================

bool process_mqtt_commands_is_duplicate(
    const irrigation_command_t* command,
    const mqtt_command_context_t* context) {

    if (!command || !context) {
        return false;
    }

    uint32_t current_time = process_mqtt_commands_get_current_timestamp();
    uint32_t window_seconds = g_processor_state.config.duplicate_window_seconds;

    // Revisar historial de comandos
    for (int i = 0; i < 5; i++) {
        const irrigation_command_t* last_cmd = &g_processor_state.last_commands[i];
        const mqtt_command_context_t* last_ctx = &g_processor_state.last_contexts[i];

        // Verificar si está dentro de la ventana de tiempo
        if (current_time > last_ctx->received_timestamp &&
            (current_time - last_ctx->received_timestamp) > window_seconds) {
            continue;
        }

        // Comparar comandos
        if (strcmp(command->device_mac_address, last_cmd->device_mac_address) == 0 &&
            command->command_type == last_cmd->command_type &&
            command->valve_number == last_cmd->valve_number &&
            command->sequence_number == last_cmd->sequence_number &&
            abs((int)command->timestamp - (int)last_cmd->timestamp) < 5) {

            return true;
        }
    }

    return false;
}

bool process_mqtt_commands_is_authorized(
    const irrigation_command_t* command,
    const mqtt_command_context_t* context) {

    if (!command || !context) {
        return false;
    }

    // Validación básica de MAC address
    if (strcmp(command->device_mac_address, g_processor_state.device_mac_address) != 0) {
        return false;
    }

    // Validación de contexto temporal
    uint32_t current_time = process_mqtt_commands_get_current_timestamp();
    if (current_time > command->timestamp &&
        (current_time - command->timestamp) > 600) { // 10 minutos máximo
        return false;
    }

    // Todos los comandos están autorizados por ahora
    // En el futuro se puede implementar un sistema de permisos más sofisticado
    return true;
}

void process_mqtt_commands_log_event(
    const irrigation_command_t* command,
    const mqtt_command_context_t* context,
    const mqtt_processing_details_t* result) {

    if (!command || !context || !result) {
        return;
    }

    ESP_LOGI(TAG, "AUDIT: CMD=%s, MAC=%s, RESULT=%s, TIME=%" PRIu32 "ms",
             process_mqtt_commands_type_to_string((mqtt_command_type_t)command->command_type),
             command->device_mac_address,
             process_mqtt_commands_processing_result_to_string(result->result_code),
             result->processing_time_ms);
}

// ============================================================================
// FUNCIONES PÚBLICAS - CONFIGURACIÓN Y ESTADÍSTICAS
// ============================================================================

esp_err_t process_mqtt_commands_set_config(const mqtt_command_processing_config_t* config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&g_processor_state.config, config, sizeof(mqtt_command_processing_config_t));
    ESP_LOGI(TAG, "Configuration updated");
    return ESP_OK;
}

void process_mqtt_commands_get_config(mqtt_command_processing_config_t* config) {
    if (config) {
        memcpy(config, &g_processor_state.config, sizeof(mqtt_command_processing_config_t));
    }
}

void process_mqtt_commands_get_statistics(mqtt_command_statistics_t* stats) {
    if (stats) {
        memcpy(stats, &g_processor_state.statistics, sizeof(mqtt_command_statistics_t));
    }
}

void process_mqtt_commands_reset_statistics(void) {
    memset(&g_processor_state.statistics, 0, sizeof(mqtt_command_statistics_t));
    ESP_LOGI(TAG, "Statistics reset");
}

// ============================================================================
// FUNCIONES PÚBLICAS - CONVERSIONES A STRING
// ============================================================================

const char* process_mqtt_commands_type_to_string(mqtt_command_type_t command_type) {
    switch (command_type) {
        case MQTT_COMMAND_TYPE_START_IRRIGATION: return "start_irrigation";
        case MQTT_COMMAND_TYPE_STOP_IRRIGATION: return "stop_irrigation";
        case MQTT_COMMAND_TYPE_EMERGENCY_STOP: return "emergency_stop";
        case MQTT_COMMAND_TYPE_GET_STATUS: return "get_status";
        case MQTT_COMMAND_TYPE_SET_CONFIG: return "set_config";
        case MQTT_COMMAND_TYPE_HEALTH_CHECK: return "health_check";
        case MQTT_COMMAND_TYPE_UNKNOWN: return "unknown";
        default: return "invalid";
    }
}

const char* process_mqtt_commands_validation_to_string(mqtt_command_validation_t validation_result) {
    switch (validation_result) {
        case MQTT_VALIDATION_SUCCESS: return "success";
        case MQTT_VALIDATION_INVALID_JSON: return "invalid_json";
        case MQTT_VALIDATION_MISSING_FIELDS: return "missing_fields";
        case MQTT_VALIDATION_INVALID_MAC: return "invalid_mac";
        case MQTT_VALIDATION_UNKNOWN_COMMAND: return "unknown_command";
        case MQTT_VALIDATION_UNAUTHORIZED: return "unauthorized";
        case MQTT_VALIDATION_SAFETY_BLOCKED: return "safety_blocked";
        case MQTT_VALIDATION_SYSTEM_BUSY: return "system_busy";
        default: return "validation_error";
    }
}

const char* process_mqtt_commands_processing_result_to_string(mqtt_processing_result_t processing_result) {
    switch (processing_result) {
        case MQTT_PROCESSING_SUCCESS: return "success";
        case MQTT_PROCESSING_VALIDATION_FAILED: return "validation_failed";
        case MQTT_PROCESSING_EXECUTION_FAILED: return "execution_failed";
        case MQTT_PROCESSING_PARTIAL_SUCCESS: return "partial_success";
        case MQTT_PROCESSING_SAFETY_ABORT: return "safety_abort";
        case MQTT_PROCESSING_TIMEOUT: return "timeout";
        case MQTT_PROCESSING_SYSTEM_ERROR: return "system_error";
        default: return "processing_error";
    }
}

// ============================================================================
// FUNCIONES PÚBLICAS - RESPUESTAS Y COMUNICACIÓN
// ============================================================================

esp_err_t process_mqtt_commands_generate_response(
    const mqtt_processing_details_t* processing_result,
    const irrigation_command_t* original_command,
    char* response_buffer,
    size_t buffer_size) {

    if (!processing_result || !original_command || !response_buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON* response = cJSON_CreateObject();
    if (!response) {
        return ESP_ERR_NO_MEM;
    }

    // Campos básicos de respuesta
    cJSON_AddStringToObject(response, "response_type", "command_result");
    cJSON_AddStringToObject(response, "mac_address", original_command->device_mac_address);
    cJSON_AddNumberToObject(response, "timestamp", process_mqtt_commands_get_current_timestamp());
    cJSON_AddStringToObject(response, "result", process_mqtt_commands_processing_result_to_string(processing_result->result_code));

    // Agregar detalles del resultado
    if (strlen(processing_result->response_message) > 0) {
        cJSON_AddStringToObject(response, "message", processing_result->response_message);
    }

    if (strlen(processing_result->error_description) > 0) {
        cJSON_AddStringToObject(response, "error", processing_result->error_description);
    }

    cJSON_AddNumberToObject(response, "processing_time_ms", processing_result->processing_time_ms);

    // Convertir a string
    char* json_string = cJSON_Print(response);
    if (!json_string) {
        cJSON_Delete(response);
        return ESP_ERR_NO_MEM;
    }

    size_t json_len = strlen(json_string);
    if (json_len >= buffer_size) {
        free(json_string);
        cJSON_Delete(response);
        return ESP_ERR_INVALID_SIZE;
    }

    strncpy(response_buffer, json_string, buffer_size - 1);
    response_buffer[buffer_size - 1] = '\0';

    free(json_string);
    cJSON_Delete(response);

    return ESP_OK;
}

esp_err_t process_mqtt_commands_publish_response(
    const char* response_message,
    const mqtt_command_context_t* context) {

    if (!response_message || !context) {
        return ESP_ERR_INVALID_ARG;
    }

    // Esta función se implementará cuando se integre con el adaptador MQTT
    // Por ahora solo registra que se debe enviar una respuesta

    ESP_LOGI(TAG, "Response ready to publish: %s", response_message);

    // TODO: Integrar con mqtt_adapter para enviar respuesta
    // return mqtt_adapter_publish_response(response_message, context);

    return ESP_OK;
}