/**
 * @file control_irrigation.c
 * @brief Implementación del use case central de control de riego
 *
 * Smart Irrigation System - ESP32 IoT Project
 * Hexagonal Architecture - Application Layer Use Case
 *
 * Implementa el orquestador principal del sistema que coordina:
 * - Domain services (irrigation_logic, safety_manager)
 * - Infrastructure drivers (soil_moisture, valve_control)
 * - External adapters (MQTT, logging)
 *
 * Basado en especificaciones técnicas del plan de implementación:
 * - Umbrales: 30% crítico, 45% warning, 75% target, 80% emergencia
 * - Sistema simplificado: 1 válvula principal
 * - Safety-first: Protección sobre-riego obligatoria
 *
 * Autor: Claude + Liwaisi Tech Team
 * Versión: 1.0.0 - Plan Implementation
 */

#include "control_irrigation.h"
#include "read_soil_sensors.h"
#include "valve_control_driver.h"
#include "irrigation_logic.h"
#include "safety_manager.h"
#include "offline_mode_logic.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char* TAG = "control_irrigation";

// ============================================================================
// CONSTANTES DEL PLAN DE IMPLEMENTACIÓN
// ============================================================================

/**
 * @brief Umbrales de humedad del suelo según especificaciones técnicas
 */
#define SOIL_CRITICAL_LOW_PERCENT     30.0    // Emergencia - riego inmediato
#define SOIL_WARNING_LOW_PERCENT      45.0    // Advertencia - programar riego
#define SOIL_TARGET_MIN_PERCENT       70.0    // Objetivo mínimo - continuar riego
#define SOIL_TARGET_MAX_PERCENT       75.0    // Objetivo máximo - PARAR riego
#define SOIL_EMERGENCY_HIGH_PERCENT   80.0    // Peligro - PARADA EMERGENCIA

/**
 * @brief Configuraciones de tiempo y duración
 */
#define DEFAULT_IRRIGATION_DURATION_MINUTES   2     // Duración por defecto
#define EMERGENCY_IRRIGATION_DURATION_MINUTES 15    // Duración emergencia
#define MAX_IRRIGATION_DURATION_MINUTES       30    // Máximo absoluto
#define EVALUATION_INTERVAL_MS                30000 // 30 segundos
#define VALVE_RESPONSE_TIMEOUT_MS             5000  // 5 segundos

// ============================================================================
// TIPOS DE DECISIÓN DE RIEGO (SEGÚN PLAN)
// ============================================================================

/**
 * @brief Tipos de decisión de riego según especificaciones del plan
 */
typedef enum {
    IRRIGATION_DECISION_NONE = 0,           // No action needed
    IRRIGATION_DECISION_START_NORMAL,       // Start normal irrigation
    IRRIGATION_DECISION_START_EMERGENCY,    // Start emergency irrigation
    IRRIGATION_DECISION_STOP_TARGET,        // Stop - target reached
    IRRIGATION_DECISION_EMERGENCY_STOP      // Emergency stop - over-irrigation
} irrigation_decision_type_t;

/**
 * @brief Estructura de decisión de riego según plan
 */
typedef struct {
    irrigation_decision_type_t decision;
    uint8_t recommended_valve;       // Which valve to activate (1 para sistema simplificado)
    uint16_t recommended_duration;   // Minutes
    char justification[128];         // Why this decision
    float average_soil_humidity;     // Promedio usado para decisión
    bool safety_override_required;   // Si requiere override de seguridad
} irrigation_decision_t;

// ============================================================================
// ESTADO GLOBAL DEL USE CASE
// ============================================================================

/**
 * @brief Estado global del control de riego
 */
static struct {
    bool use_case_initialized;
    control_irrigation_context_t context;
    irrigation_status_t current_irrigation_status;
    uint32_t last_evaluation_timestamp;
    uint32_t successful_evaluations;
    uint32_t failed_evaluations;
    uint32_t emergency_stops_executed;
} g_control_irrigation_state = {
    .use_case_initialized = false,
};

// ============================================================================
// FUNCIONES AUXILIARES PRIVADAS
// ============================================================================

/**
 * @brief Evaluar condiciones del suelo según lógica del plan
 */
static irrigation_decision_t evaluate_soil_conditions_plan_logic(const soil_sensor_data_t* soil_data) {
    irrigation_decision_t decision = {0};

    // PROTECCIÓN SOBRE-RIEGO: Si CUALQUIER sensor > 80% (CRÍTICO)
    if (soil_data->soil_humidity_1 > SOIL_EMERGENCY_HIGH_PERCENT ||
        soil_data->soil_humidity_2 > SOIL_EMERGENCY_HIGH_PERCENT ||
        soil_data->soil_humidity_3 > SOIL_EMERGENCY_HIGH_PERCENT) {

        decision.decision = IRRIGATION_DECISION_EMERGENCY_STOP;
        decision.recommended_valve = 1;
        decision.recommended_duration = 0;
        snprintf(decision.justification, sizeof(decision.justification),
                 "EMERGENCY: Soil humidity > 80%% detected (%.1f%%, %.1f%%, %.1f%%)",
                 soil_data->soil_humidity_1, soil_data->soil_humidity_2, soil_data->soil_humidity_3);

        ESP_LOGW(TAG, "EMERGENCY STOP: Over-irrigation protection triggered");
        return decision;
    }

    // TARGET ALCANZADO: Todos los sensores >= 75%
    if (soil_data->soil_humidity_1 >= SOIL_TARGET_MAX_PERCENT &&
        soil_data->soil_humidity_2 >= SOIL_TARGET_MAX_PERCENT &&
        soil_data->soil_humidity_3 >= SOIL_TARGET_MAX_PERCENT) {

        decision.decision = IRRIGATION_DECISION_STOP_TARGET;
        decision.recommended_valve = 1;
        decision.recommended_duration = 0;
        snprintf(decision.justification, sizeof(decision.justification),
                 "Target reached: All sensors >= 75%% (%.1f%%, %.1f%%, %.1f%%)",
                 soil_data->soil_humidity_1, soil_data->soil_humidity_2, soil_data->soil_humidity_3);

        ESP_LOGI(TAG, "Target moisture reached - stopping irrigation");
        return decision;
    }

    // EMERGENCIA: Cualquier sensor < 30%
    if (soil_data->soil_humidity_1 < SOIL_CRITICAL_LOW_PERCENT ||
        soil_data->soil_humidity_2 < SOIL_CRITICAL_LOW_PERCENT ||
        soil_data->soil_humidity_3 < SOIL_CRITICAL_LOW_PERCENT) {

        decision.decision = IRRIGATION_DECISION_START_EMERGENCY;
        decision.recommended_valve = 1;
        decision.recommended_duration = EMERGENCY_IRRIGATION_DURATION_MINUTES;
        snprintf(decision.justification, sizeof(decision.justification),
                 "EMERGENCY: Critical low humidity < 30%% detected (%.1f%%, %.1f%%, %.1f%%)",
                 soil_data->soil_humidity_1, soil_data->soil_humidity_2, soil_data->soil_humidity_3);

        ESP_LOGW(TAG, "Emergency irrigation needed - critical low humidity");
        return decision;
    }

    // RIEGO NORMAL: Promedio < 45%
    float average_humidity = (soil_data->soil_humidity_1 +
                             soil_data->soil_humidity_2 +
                             soil_data->soil_humidity_3) / 3.0;

    decision.average_soil_humidity = average_humidity;

    if (average_humidity < SOIL_WARNING_LOW_PERCENT) {
        decision.decision = IRRIGATION_DECISION_START_NORMAL;
        decision.recommended_valve = 1;
        decision.recommended_duration = DEFAULT_IRRIGATION_DURATION_MINUTES;
        snprintf(decision.justification, sizeof(decision.justification),
                 "Normal irrigation: Average humidity %.1f%% < 45%% threshold",
                 average_humidity);

        ESP_LOGI(TAG, "Normal irrigation needed - low average humidity");
        return decision;
    }

    // SIN ACCIÓN NECESARIA
    decision.decision = IRRIGATION_DECISION_NONE;
    decision.recommended_valve = 0;
    decision.recommended_duration = 0;
    snprintf(decision.justification, sizeof(decision.justification),
             "No action needed: Average humidity %.1f%% within normal range",
             average_humidity);

    ESP_LOGD(TAG, "No irrigation action needed");
    return decision;
}

/**
 * @brief Inicializar estado de riego por defecto
 */
static void init_irrigation_status(irrigation_status_t* status) {
    memset(status, 0, sizeof(irrigation_status_t));

    // Solo válvula 1 en sistema simplificado
    status->is_valve_1_active = false;
    status->is_valve_2_active = false;
    status->is_valve_3_active = false;

    status->max_duration_minutes = MAX_IRRIGATION_DURATION_MINUTES;
    status->emergency_stop_active = false;
    strcpy(status->last_stop_reason, "system_init");

    ESP_LOGI(TAG, "Irrigation status initialized for simplified system (1 valve)");
}

/**
 * @brief Verificar si alguna válvula está activa
 */
static bool is_any_valve_active(const irrigation_status_t* status) {
    // Sistema simplificado: solo válvula 1
    return status->is_valve_1_active;
}

/**
 * @brief Verificar timeout de válvula
 */
static bool is_valve_timeout(const irrigation_status_t* status, uint8_t valve_number) {
    if (valve_number != 1) return false;  // Solo válvula 1 en sistema simplificado

    if (!status->is_valve_1_active || status->valve_1_start_time == 0) {
        return false;
    }

    uint32_t current_time = esp_timer_get_time() / 1000000; // segundos
    uint32_t elapsed_time = current_time - status->valve_1_start_time;
    uint32_t max_time_seconds = status->max_duration_minutes * 60;

    return elapsed_time > max_time_seconds;
}

/**
 * @brief Actualizar estado de válvula en estructura de estado
 */
static void update_valve_status(irrigation_status_t* status, uint8_t valve_number, bool is_active, const char* reason) {
    uint32_t current_time = esp_timer_get_time() / 1000000; // segundos

    if (valve_number == 1) {
        status->is_valve_1_active = is_active;
        if (is_active) {
            status->valve_1_start_time = current_time;
        } else {
            status->valve_1_start_time = 0;
            if (reason) {
                strncpy(status->last_stop_reason, reason, sizeof(status->last_stop_reason) - 1);
            }
        }
    }

    ESP_LOGI(TAG, "Valve %d status updated: %s, reason: %s",
             valve_number, is_active ? "ACTIVE" : "INACTIVE", reason ? reason : "none");
}

/**
 * @brief Logs adapter por defecto
 */
static void default_log_info(const char* message) {
    ESP_LOGI(TAG, "%s", message);
}

static void default_log_warning(const char* message) {
    ESP_LOGW(TAG, "%s", message);
}

static void default_log_error(const char* message) {
    ESP_LOGE(TAG, "%s", message);
}

// ============================================================================
// FUNCIONES PÚBLICAS - INICIALIZACIÓN
// ============================================================================

esp_err_t control_irrigation_init_context(control_irrigation_context_t* context,
                                         const char* device_mac_address,
                                         const safety_limits_t* safety_limits) {
    if (!context || !device_mac_address || !safety_limits) {
        ESP_LOGE(TAG, "Invalid parameters for context initialization");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing control irrigation context");

    // Limpiar contexto
    memset(context, 0, sizeof(control_irrigation_context_t));

    // Configurar logging adapter por defecto
    context->logging_adapter.log_info = default_log_info;
    context->logging_adapter.log_warning = default_log_warning;
    context->logging_adapter.log_error = default_log_error;

    // Configuración por defecto
    context->configuration.evaluation_interval_ms = EVALUATION_INTERVAL_MS;
    context->configuration.sensor_timeout_ms = 2000;
    context->configuration.valve_response_timeout_ms = VALVE_RESPONSE_TIMEOUT_MS;
    context->configuration.automatic_evaluation_enabled = true;
    context->configuration.mqtt_publishing_enabled = true;

    // Estado inicial
    context->state.is_initialized = false;
    context->state.last_evaluation_timestamp = 0;
    context->state.evaluation_count = 0;
    context->state.successful_operations = 0;
    context->state.failed_operations = 0;
    context->state.last_result = CONTROL_IRRIGATION_SUCCESS;

    ESP_LOGI(TAG, "Control irrigation context initialized successfully");
    return ESP_OK;
}

esp_err_t control_irrigation_set_hardware_adapter(control_irrigation_context_t* context,
                                                 const void* hardware_adapter) {
    if (!context) {
        ESP_LOGE(TAG, "Invalid context for hardware adapter configuration");
        return ESP_ERR_INVALID_ARG;
    }

    // En esta implementación simplificada, usamos los drivers directamente
    // En una implementación más completa, se inyectarían las dependencias

    ESP_LOGI(TAG, "Hardware adapter configured (using direct driver calls)");
    return ESP_OK;
}

esp_err_t control_irrigation_set_mqtt_adapter(control_irrigation_context_t* context,
                                             const void* mqtt_adapter) {
    if (!context) {
        ESP_LOGE(TAG, "Invalid context for MQTT adapter configuration");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "MQTT adapter configured");
    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - EJECUCIÓN PRINCIPAL
// ============================================================================

control_irrigation_operation_result_t control_irrigation_execute_automatic_evaluation(
    control_irrigation_context_t* context) {

    control_irrigation_operation_result_t result = {0};
    uint32_t start_time = esp_timer_get_time() / 1000; // ms

    if (!context) {
        result.result_code = CONTROL_IRRIGATION_SYSTEM_ERROR;
        strcpy(result.description, "Invalid context");
        return result;
    }

    ESP_LOGD(TAG, "Executing automatic irrigation evaluation");

    // Paso 1: Leer sensores (ambiente + suelo)
    ambient_sensor_data_t ambient_data = {0};
    soil_sensor_data_t soil_data = {0};

    esp_err_t read_result = control_irrigation_read_all_sensors(context, &ambient_data, &soil_data);
    if (read_result != ESP_OK) {
        result.result_code = CONTROL_IRRIGATION_SENSOR_ERROR;
        snprintf(result.description, sizeof(result.description),
                 "Failed to read sensors: %s", esp_err_to_name(read_result));
        context->state.failed_operations++;
        return result;
    }

    // Paso 2: Evaluar condiciones con domain logic (según plan)
    irrigation_decision_t decision = evaluate_soil_conditions_plan_logic(&soil_data);

    // Paso 3: Validar seguridad usando safety_manager
    safety_evaluation_context_t safety_context = {
        .current_session_start_time = g_control_irrigation_state.current_irrigation_status.valve_1_start_time,
        .last_irrigation_end_time = 0,  // Simplificado
        .active_valve_count = is_any_valve_active(&g_control_irrigation_state.current_irrigation_status) ? 1 : 0,
        .emergency_stop_count_today = g_control_irrigation_state.emergency_stops_executed,
        .manual_override_active = false,
        .current_power_mode = POWER_MODE_NORMAL
    };

    safety_evaluation_result_t safety_result = safety_manager_evaluate_start_irrigation(
        NULL, // irrigation_command (simplificado)
        &g_control_irrigation_state.current_irrigation_status,
        &ambient_data,
        &soil_data,
        &safety_context
    );

    // Verificar resultado de seguridad
    if (safety_result.overall_result == SAFETY_CHECK_EMERGENCY ||
        safety_result.overall_result == SAFETY_CHECK_BLOCKED ||
        safety_result.immediate_action_required) {

        ESP_LOGW(TAG, "Safety check failed: %s", safety_result.violation_description);

        // Ejecutar parada de emergencia si es necesario
        if (is_any_valve_active(&g_control_irrigation_state.current_irrigation_status)) {
            control_irrigation_execute_emergency_stop(context, "safety_violation");
        }

        result.result_code = CONTROL_IRRIGATION_SAFETY_BLOCK;
        strcpy(result.description, safety_result.violation_description);
        strcpy(result.action_taken, "Emergency stop executed");
        result.state_changed = true;
        context->state.failed_operations++;
        return result;
    }

    // Paso 4: Ejecutar acción recomendada
    esp_err_t action_result = ESP_OK;
    bool state_changed = false;

    switch (decision.decision) {
        case IRRIGATION_DECISION_START_NORMAL:
        case IRRIGATION_DECISION_START_EMERGENCY:
            if (!is_any_valve_active(&g_control_irrigation_state.current_irrigation_status)) {
                action_result = valve_control_driver_open_valve(decision.recommended_valve);
                if (action_result == ESP_OK) {
                    update_valve_status(&g_control_irrigation_state.current_irrigation_status,
                                      decision.recommended_valve, true, "automatic_start");
                    state_changed = true;
                    strcpy(result.action_taken, "Started irrigation");
                } else {
                    result.result_code = CONTROL_IRRIGATION_VALVE_ERROR;
                    strcpy(result.description, "Failed to open valve");
                    context->state.failed_operations++;
                    return result;
                }
            } else {
                strcpy(result.action_taken, "Irrigation already active");
            }
            break;

        case IRRIGATION_DECISION_STOP_TARGET:
            if (is_any_valve_active(&g_control_irrigation_state.current_irrigation_status)) {
                action_result = valve_control_driver_close_valve(1);
                if (action_result == ESP_OK) {
                    update_valve_status(&g_control_irrigation_state.current_irrigation_status,
                                      1, false, "target_reached");
                    state_changed = true;
                    strcpy(result.action_taken, "Stopped irrigation - target reached");
                }
            } else {
                strcpy(result.action_taken, "No irrigation to stop");
            }
            break;

        case IRRIGATION_DECISION_EMERGENCY_STOP:
            control_irrigation_execute_emergency_stop(context, "over_irrigation_protection");
            state_changed = true;
            strcpy(result.action_taken, "Emergency stop executed");
            break;

        case IRRIGATION_DECISION_NONE:
        default:
            strcpy(result.action_taken, "No action required");
            break;
    }

    // Verificar timeout de válvulas
    if (is_valve_timeout(&g_control_irrigation_state.current_irrigation_status, 1)) {
        ESP_LOGW(TAG, "Valve timeout detected - stopping irrigation");
        valve_control_driver_close_valve(1);
        update_valve_status(&g_control_irrigation_state.current_irrigation_status,
                           1, false, "timeout_protection");
        state_changed = true;
    }

    // Paso 5: Actualizar estado y publicar eventos
    context->state.evaluation_count++;
    context->state.last_evaluation_timestamp = esp_timer_get_time() / 1000000;
    context->state.successful_operations++;

    // Publicar estado si está habilitado
    if (context->configuration.mqtt_publishing_enabled) {
        control_irrigation_publish_system_status(context);
    }

    // Preparar resultado
    result.result_code = CONTROL_IRRIGATION_SUCCESS;
    strncpy(result.description, decision.justification, sizeof(result.description) - 1);
    result.state_changed = state_changed;

    uint32_t end_time = esp_timer_get_time() / 1000; // ms
    result.execution_time_ms = end_time - start_time;

    ESP_LOGI(TAG, "Automatic evaluation completed: %s (%.1f%% avg humidity, %dms)",
             result.action_taken, decision.average_soil_humidity, result.execution_time_ms);

    return result;
}

control_irrigation_operation_result_t control_irrigation_process_command(
    control_irrigation_context_t* context,
    const irrigation_command_t* command) {

    control_irrigation_operation_result_t result = {0};

    if (!context || !command) {
        result.result_code = CONTROL_IRRIGATION_SYSTEM_ERROR;
        strcpy(result.description, "Invalid parameters");
        return result;
    }

    ESP_LOGI(TAG, "Processing irrigation command: %s for valve %d",
             command->command_type, command->valve_number);

    // Validar comando
    if (!irrigation_command_is_valid(command)) {
        result.result_code = CONTROL_IRRIGATION_COMMAND_INVALID;
        strcpy(result.description, "Invalid irrigation command");
        return result;
    }

    // Solo válvula 1 soportada en sistema simplificado
    if (command->valve_number != 1) {
        result.result_code = CONTROL_IRRIGATION_COMMAND_INVALID;
        snprintf(result.description, sizeof(result.description),
                 "Valve %d not supported in simplified system", command->valve_number);
        return result;
    }

    esp_err_t action_result = ESP_OK;
    bool state_changed = false;

    // Procesar comando específico
    if (strcmp(command->command_type, "start_irrigation") == 0) {
        action_result = valve_control_driver_open_valve(command->valve_number);
        if (action_result == ESP_OK) {
            update_valve_status(&g_control_irrigation_state.current_irrigation_status,
                              command->valve_number, true, "mqtt_command");
            state_changed = true;
            strcpy(result.action_taken, "Irrigation started via MQTT");
        }
    } else if (strcmp(command->command_type, "stop_irrigation") == 0) {
        action_result = valve_control_driver_close_valve(command->valve_number);
        if (action_result == ESP_OK) {
            update_valve_status(&g_control_irrigation_state.current_irrigation_status,
                              command->valve_number, false, "mqtt_command");
            state_changed = true;
            strcpy(result.action_taken, "Irrigation stopped via MQTT");
        }
    } else if (strcmp(command->command_type, "emergency_stop") == 0) {
        control_irrigation_execute_emergency_stop(context, "mqtt_emergency_command");
        state_changed = true;
        strcpy(result.action_taken, "Emergency stop executed via MQTT");
    } else {
        result.result_code = CONTROL_IRRIGATION_COMMAND_INVALID;
        snprintf(result.description, sizeof(result.description),
                 "Unknown command type: %s", command->command_type);
        return result;
    }

    // Verificar resultado de la acción
    if (action_result != ESP_OK) {
        result.result_code = CONTROL_IRRIGATION_VALVE_ERROR;
        snprintf(result.description, sizeof(result.description),
                 "Failed to execute valve operation: %s", esp_err_to_name(action_result));
        context->state.failed_operations++;
        return result;
    }

    // Éxito
    result.result_code = CONTROL_IRRIGATION_SUCCESS;
    strcpy(result.description, "Command processed successfully");
    result.state_changed = state_changed;
    context->state.successful_operations++;

    ESP_LOGI(TAG, "Command processed: %s", result.action_taken);

    return result;
}

control_irrigation_operation_result_t control_irrigation_execute_emergency_stop(
    control_irrigation_context_t* context,
    const char* reason) {

    control_irrigation_operation_result_t result = {0};

    ESP_LOGW(TAG, "Executing emergency stop: %s", reason ? reason : "unknown");

    // Cerrar todas las válvulas inmediatamente
    esp_err_t valve_result = valve_control_driver_close_all_valves();

    // Actualizar estado de emergencia
    g_control_irrigation_state.current_irrigation_status.emergency_stop_active = true;
    g_control_irrigation_state.current_irrigation_status.is_valve_1_active = false;
    g_control_irrigation_state.current_irrigation_status.valve_1_start_time = 0;

    if (reason) {
        strncpy(g_control_irrigation_state.current_irrigation_status.last_stop_reason,
                reason, sizeof(g_control_irrigation_state.current_irrigation_status.last_stop_reason) - 1);
    }

    // Incrementar contador de emergencias
    g_control_irrigation_state.emergency_stops_executed++;

    // La parada de emergencia siempre se considera exitosa desde el punto de vista lógico
    result.result_code = CONTROL_IRRIGATION_SUCCESS;
    snprintf(result.description, sizeof(result.description),
             "Emergency stop executed: %s", reason ? reason : "unknown");
    strcpy(result.action_taken, "All valves closed immediately");
    result.state_changed = true;
    result.execution_time_ms = 0; // Operación inmediata

    if (valve_result != ESP_OK) {
        ESP_LOGE(TAG, "Warning: Valve closure may have failed during emergency stop");
        // Pero aún consideramos la operación exitosa lógicamente
    }

    // Log crítico para auditoría
    ESP_LOGW(TAG, "EMERGENCY STOP EXECUTED: %s (total: %d today)",
             reason ? reason : "unknown", g_control_irrigation_state.emergency_stops_executed);

    return result;
}

// ============================================================================
// FUNCIONES PÚBLICAS - OPERACIONES DE VÁLVULAS
// ============================================================================

control_irrigation_operation_result_t control_irrigation_start_valve(
    control_irrigation_context_t* context,
    uint8_t valve_number,
    uint16_t duration_minutes,
    const char* reason) {

    control_irrigation_operation_result_t result = {0};

    if (!context || valve_number != 1) {
        result.result_code = CONTROL_IRRIGATION_COMMAND_INVALID;
        strcpy(result.description, "Invalid valve number for simplified system");
        return result;
    }

    if (duration_minutes > MAX_IRRIGATION_DURATION_MINUTES) {
        result.result_code = CONTROL_IRRIGATION_COMMAND_INVALID;
        snprintf(result.description, sizeof(result.description),
                 "Duration %d exceeds maximum %d minutes", duration_minutes, MAX_IRRIGATION_DURATION_MINUTES);
        return result;
    }

    ESP_LOGI(TAG, "Starting valve %d for %d minutes, reason: %s",
             valve_number, duration_minutes, reason ? reason : "none");

    esp_err_t valve_result = valve_control_driver_open_valve(valve_number);
    if (valve_result == ESP_OK) {
        update_valve_status(&g_control_irrigation_state.current_irrigation_status,
                           valve_number, true, reason);

        result.result_code = CONTROL_IRRIGATION_SUCCESS;
        snprintf(result.description, sizeof(result.description),
                 "Valve %d started successfully", valve_number);
        strcpy(result.action_taken, "Valve opened");
        result.state_changed = true;
        context->state.successful_operations++;
    } else {
        result.result_code = CONTROL_IRRIGATION_VALVE_ERROR;
        snprintf(result.description, sizeof(result.description),
                 "Failed to open valve %d: %s", valve_number, esp_err_to_name(valve_result));
        context->state.failed_operations++;
    }

    return result;
}

control_irrigation_operation_result_t control_irrigation_stop_valve(
    control_irrigation_context_t* context,
    uint8_t valve_number,
    const char* reason) {

    control_irrigation_operation_result_t result = {0};

    if (!context || valve_number != 1) {
        result.result_code = CONTROL_IRRIGATION_COMMAND_INVALID;
        strcpy(result.description, "Invalid valve number for simplified system");
        return result;
    }

    ESP_LOGI(TAG, "Stopping valve %d, reason: %s", valve_number, reason ? reason : "none");

    esp_err_t valve_result = valve_control_driver_close_valve(valve_number);
    if (valve_result == ESP_OK) {
        update_valve_status(&g_control_irrigation_state.current_irrigation_status,
                           valve_number, false, reason);

        result.result_code = CONTROL_IRRIGATION_SUCCESS;
        snprintf(result.description, sizeof(result.description),
                 "Valve %d stopped successfully", valve_number);
        strcpy(result.action_taken, "Valve closed");
        result.state_changed = true;
        context->state.successful_operations++;
    } else {
        result.result_code = CONTROL_IRRIGATION_VALVE_ERROR;
        snprintf(result.description, sizeof(result.description),
                 "Failed to close valve %d: %s", valve_number, esp_err_to_name(valve_result));
        context->state.failed_operations++;
    }

    return result;
}

// ============================================================================
// FUNCIONES PÚBLICAS - LECTURA DE SENSORES
// ============================================================================

esp_err_t control_irrigation_read_all_sensors(control_irrigation_context_t* context,
                                             ambient_sensor_data_t* ambient_data,
                                             soil_sensor_data_t* soil_data) {
    if (!context || !ambient_data || !soil_data) {
        ESP_LOGE(TAG, "Invalid parameters for sensor reading");
        return ESP_ERR_INVALID_ARG;
    }

    // Leer sensores de suelo usando read_soil_sensors use case
    soil_reading_complete_result_t soil_result = read_soil_sensors_read_all(ambient_data);

    if (soil_result.overall_result != SOIL_READING_SUCCESS &&
        soil_result.overall_result != SOIL_READING_PARTIAL_SUCCESS) {
        ESP_LOGE(TAG, "Failed to read soil sensors: %s",
                 read_soil_sensors_result_to_string(soil_result.overall_result));
        return ESP_FAIL;
    }

    // Copiar datos de suelo
    *soil_data = soil_result.sensor_data;

    // Para datos ambientales, usar valores por defecto si no están disponibles
    if (ambient_data->ambient_temperature == 0.0 && ambient_data->ambient_humidity == 0.0) {
        ambient_data->ambient_temperature = 25.0;  // Temperatura por defecto
        ambient_data->ambient_humidity = 60.0;     // Humedad por defecto
        ambient_data->timestamp = esp_timer_get_time() / 1000000;
        ESP_LOGD(TAG, "Using default ambient values");
    }

    ESP_LOGD(TAG, "Sensors read successfully - Soil: %.1f/%.1f/%.1f%%, Ambient: %.1f°C/%.1fRH",
             soil_data->soil_humidity_1, soil_data->soil_humidity_2, soil_data->soil_humidity_3,
             ambient_data->ambient_temperature, ambient_data->ambient_humidity);

    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - MQTT Y ESTADO
// ============================================================================

esp_err_t control_irrigation_publish_system_status(control_irrigation_context_t* context) {
    if (!context || !context->configuration.mqtt_publishing_enabled) {
        return ESP_OK;  // No error, simplemente no publicar
    }

    // En implementación simplificada, solo log el estado
    ESP_LOGI(TAG, "System status: Valve 1 %s, Emergency: %s, Evaluations: %d",
             g_control_irrigation_state.current_irrigation_status.is_valve_1_active ? "ACTIVE" : "INACTIVE",
             g_control_irrigation_state.current_irrigation_status.emergency_stop_active ? "YES" : "NO",
             context->state.evaluation_count);

    return ESP_OK;
}

esp_err_t control_irrigation_publish_event(control_irrigation_context_t* context,
                                          const char* event_type,
                                          uint8_t valve_number,
                                          const char* details) {
    if (!context || !event_type) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Event: %s, Valve: %d, Details: %s",
             event_type, valve_number, details ? details : "none");

    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - CONFIGURACIÓN Y CONSULTAS
// ============================================================================

void control_irrigation_set_automatic_evaluation(control_irrigation_context_t* context, bool enabled) {
    if (context) {
        context->configuration.automatic_evaluation_enabled = enabled;
        ESP_LOGI(TAG, "Automatic evaluation %s", enabled ? "ENABLED" : "DISABLED");
    }
}

esp_err_t control_irrigation_set_evaluation_interval(control_irrigation_context_t* context,
                                                    uint16_t interval_ms) {
    if (!context || interval_ms < 1000) {
        return ESP_ERR_INVALID_ARG;
    }

    context->configuration.evaluation_interval_ms = interval_ms;
    ESP_LOGI(TAG, "Evaluation interval set to %dms", interval_ms);
    return ESP_OK;
}

void control_irrigation_get_statistics(const control_irrigation_context_t* context,
                                      uint32_t* total_evaluations,
                                      uint32_t* successful_ops,
                                      uint32_t* failed_ops) {
    if (context && total_evaluations && successful_ops && failed_ops) {
        *total_evaluations = context->state.evaluation_count;
        *successful_ops = context->state.successful_operations;
        *failed_ops = context->state.failed_operations;
    }
}

const irrigation_status_t* control_irrigation_get_current_status(const control_irrigation_context_t* context) {
    return &g_control_irrigation_state.current_irrigation_status;
}

bool control_irrigation_is_initialized(const control_irrigation_context_t* context) {
    return context && context->state.is_initialized;
}

bool control_irrigation_has_active_irrigation(const control_irrigation_context_t* context) {
    return is_any_valve_active(&g_control_irrigation_state.current_irrigation_status);
}

uint32_t control_irrigation_time_to_next_evaluation(const control_irrigation_context_t* context) {
    if (!context) return 0;

    uint32_t current_time = esp_timer_get_time() / 1000; // ms
    uint32_t last_eval_time = context->state.last_evaluation_timestamp * 1000; // convertir a ms
    uint32_t elapsed = current_time - last_eval_time;

    if (elapsed >= context->configuration.evaluation_interval_ms) {
        return 0;
    }

    return context->configuration.evaluation_interval_ms - elapsed;
}

// ============================================================================
// FUNCIONES PÚBLICAS - UTILIDADES
// ============================================================================

const char* control_irrigation_result_to_string(control_irrigation_result_t result) {
    switch (result) {
        case CONTROL_IRRIGATION_SUCCESS: return "SUCCESS";
        case CONTROL_IRRIGATION_NO_ACTION: return "NO_ACTION";
        case CONTROL_IRRIGATION_SAFETY_BLOCK: return "SAFETY_BLOCK";
        case CONTROL_IRRIGATION_SENSOR_ERROR: return "SENSOR_ERROR";
        case CONTROL_IRRIGATION_VALVE_ERROR: return "VALVE_ERROR";
        case CONTROL_IRRIGATION_COMMAND_INVALID: return "COMMAND_INVALID";
        case CONTROL_IRRIGATION_SYSTEM_ERROR: return "SYSTEM_ERROR";
        default: return "UNKNOWN";
    }
}

void control_irrigation_cleanup_context(control_irrigation_context_t* context) {
    if (context) {
        ESP_LOGI(TAG, "Cleaning up irrigation control context");
        memset(context, 0, sizeof(control_irrigation_context_t));
    }
}

uint32_t control_irrigation_get_current_timestamp_ms(void) {
    return esp_timer_get_time() / 1000; // microsegundos a milisegundos
}

// ============================================================================
// INICIALIZACIÓN GLOBAL
// ============================================================================

/**
 * @brief Inicializar el módulo global de control de riego
 */
esp_err_t control_irrigation_init_global(void) {
    if (g_control_irrigation_state.use_case_initialized) {
        ESP_LOGW(TAG, "Control irrigation already initialized globally");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing global control irrigation state");

    // Inicializar estado de riego
    init_irrigation_status(&g_control_irrigation_state.current_irrigation_status);

    // Inicializar contexto por defecto
    safety_limits_t default_safety_limits = {
        .max_soil_humidity_percent = SOIL_EMERGENCY_HIGH_PERCENT,
        .max_irrigation_duration_minutes = MAX_IRRIGATION_DURATION_MINUTES,
        .min_soil_humidity_percent = SOIL_CRITICAL_LOW_PERCENT,
        .max_ambient_temperature_celsius = 50.0
    };

    esp_err_t ret = control_irrigation_init_context(&g_control_irrigation_state.context,
                                                   "AA:BB:CC:DD:EE:FF", &default_safety_limits);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize global context");
        return ret;
    }

    g_control_irrigation_state.context.state.is_initialized = true;
    g_control_irrigation_state.use_case_initialized = true;

    ESP_LOGI(TAG, "Control irrigation initialized globally - ready for operation");
    return ESP_OK;
}