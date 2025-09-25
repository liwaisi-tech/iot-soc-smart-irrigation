/**
 * @file offline_irrigation.c
 * @brief Implementación del use case principal de riego offline
 *
 * Smart Irrigation System - ESP32 IoT Project
 * Hexagonal Architecture - Application Layer Use Case
 *
 * Implementa el caso de uso crítico para operación autónoma sin conectividad,
 * coordinando domain services y infrastructure drivers para garantizar la
 * supervivencia del cultivo en condiciones rurales de Colombia.
 *
 * Características principales:
 * - Activación automática tras 5min sin WiFi o 3min sin MQTT
 * - Intervalos adaptativos: 2h → 1h → 30min → 15min según criticidad
 * - Algoritmos de supervivencia optimizados para batería
 * - Safety-first: Protecciones críticas siempre activas
 *
 * Autor: Claude + Liwaisi Tech Team
 * Versión: 1.0.0 - Autonomous Operation
 */

#include "offline_irrigation.h"
#include "control_irrigation.h"
#include "read_soil_sensors.h"
#include "offline_mode_logic.h"
#include "irrigation_logic.h"
#include "safety_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>
#include <inttypes.h>

static const char* TAG = "offline_irrigation";

// ============================================================================
// CONFIGURACIÓN SEGÚN PLAN DE IMPLEMENTACIÓN
// ============================================================================

/**
 * @brief Configuración por defecto para modo offline
 */
static const offline_irrigation_config_t DEFAULT_OFFLINE_CONFIG = {
    // Intervalos por modo (segundos)
    .normal_mode_interval_s = 7200,           // 2 horas
    .warning_mode_interval_s = 3600,          // 1 hora
    .critical_mode_interval_s = 1800,         // 30 minutos
    .emergency_mode_interval_s = 900,         // 15 minutos

    // Umbrales de activación offline
    .wifi_timeout_s = 300,                    // 5 minutos
    .mqtt_timeout_s = 180,                    // 3 minutos
    .network_failure_threshold = 3,           // 3 fallos consecutivos

    // Optimización de batería
    .minimum_battery_level_percent = 20,      // 20% mínimo
    .enable_solar_optimization = true,        // Optimizar para solar
    .enable_deep_sleep_offline = false,       // No usar deep sleep (simplificado)

    // Configuración de seguridad offline
    .enable_emergency_irrigation = true,      // Permitir riego emergencia
    .soil_emergency_threshold = 25.0,         // 25% emergencia crítica
    .max_offline_irrigation_minutes = 20,     // 20 min máximo offline
};

// ============================================================================
// ESTADO GLOBAL DEL MODO OFFLINE
// ============================================================================

/**
 * @brief Estado global del modo offline
 */
static struct {
    bool offline_initialized;
    bool offline_mode_active;
    offline_irrigation_config_t config;
    offline_irrigation_context_t current_context;
    offline_irrigation_statistics_t statistics;
    safety_limits_t safety_limits;
    char device_mac_address[18];
} g_offline_state = {
    .offline_initialized = false,
    .offline_mode_active = false,
};

// ============================================================================
// FUNCIONES AUXILIARES PRIVADAS
// ============================================================================

/**
 * @brief Calcular criticidad del cultivo según condiciones
 */
static float calculate_crop_criticality_level(const soil_sensor_data_t* soil_data,
                                            const ambient_sensor_data_t* ambient_data,
                                            const offline_irrigation_context_t* context) {
    if (!soil_data || !context) return 0.0;

    float criticality = 0.0;

    // Factor 1: Humedad del suelo (peso 60%)
    float min_soil_humidity = fmin(fmin(soil_data->soil_humidity_1, soil_data->soil_humidity_2),
                                   soil_data->soil_humidity_3);
    float avg_soil_humidity = (soil_data->soil_humidity_1 + soil_data->soil_humidity_2 +
                              soil_data->soil_humidity_3) / 3.0;

    // Criticidad basada en humedad mínima
    if (min_soil_humidity < g_offline_state.config.soil_emergency_threshold) {
        criticality += 0.8;  // Muy crítico
    } else if (min_soil_humidity < 35.0) {
        criticality += 0.6;  // Crítico
    } else if (avg_soil_humidity < 45.0) {
        criticality += 0.4;  // Moderado
    } else if (avg_soil_humidity < 60.0) {
        criticality += 0.2;  // Bajo
    }

    // Factor 2: Tiempo sin conectividad (peso 20%)
    uint32_t offline_duration = context->total_offline_duration_s;
    if (offline_duration > 86400) {         // >24 horas
        criticality += 0.3;
    } else if (offline_duration > 43200) {  // >12 horas
        criticality += 0.2;
    } else if (offline_duration > 14400) {  // >4 horas
        criticality += 0.1;
    }

    // Factor 3: Condiciones ambientales (peso 15%)
    if (ambient_data) {
        if (ambient_data->ambient_temperature > 35.0) {
            criticality += 0.15;  // Calor extremo
        } else if (ambient_data->ambient_temperature > 30.0) {
            criticality += 0.1;   // Calor alto
        }

        if (ambient_data->ambient_humidity < 40.0) {
            criticality += 0.1;   // Ambiente muy seco
        }
    }

    // Factor 4: Estado de batería (peso 5%)
    if (context->battery_level_percent < g_offline_state.config.minimum_battery_level_percent) {
        criticality += 0.05;
    }

    // Limitar al rango [0.0, 1.0]
    if (criticality > 1.0) criticality = 1.0;
    if (criticality < 0.0) criticality = 0.0;

    return criticality;
}

/**
 * @brief Determinar modo offline basado en criticidad
 */
static offline_mode_t determine_offline_mode_from_criticality(float criticality_level,
                                                            const offline_irrigation_context_t* context) {
    // Usar lógica del domain service offline_mode_logic
    soil_sensor_data_t dummy_soil = {
        .soil_humidity_1 = (1.0 - criticality_level) * 100.0,
        .soil_humidity_2 = (1.0 - criticality_level) * 100.0,
        .soil_humidity_3 = (1.0 - criticality_level) * 100.0,
        .timestamp = esp_timer_get_time() / 1000000
    };

    ambient_sensor_data_t dummy_ambient = {
        .ambient_temperature = 25.0 + (criticality_level * 15.0),
        .ambient_humidity = 60.0 - (criticality_level * 20.0),
        .timestamp = esp_timer_get_time() / 1000000
    };

    return offline_mode_logic_determine_mode(&dummy_ambient, &dummy_soil);
}

/**
 * @brief Calcular intervalo de evaluación según modo y batería
 */
static uint16_t calculate_evaluation_interval_optimized(offline_mode_t offline_mode,
                                                       uint8_t battery_level,
                                                       float criticality_level) {
    uint16_t base_interval;

    // Intervalo base según modo
    switch (offline_mode) {
        case OFFLINE_MODE_NORMAL:
            base_interval = g_offline_state.config.normal_mode_interval_s;
            break;
        case OFFLINE_MODE_WARNING:
            base_interval = g_offline_state.config.warning_mode_interval_s;
            break;
        case OFFLINE_MODE_CRITICAL:
            base_interval = g_offline_state.config.critical_mode_interval_s;
            break;
        case OFFLINE_MODE_EMERGENCY:
            base_interval = g_offline_state.config.emergency_mode_interval_s;
            break;
        default:
            base_interval = g_offline_state.config.normal_mode_interval_s;
            break;
    }

    // Ajustar por nivel de batería
    if (battery_level < g_offline_state.config.minimum_battery_level_percent) {
        base_interval *= 2;  // Duplicar intervalo para ahorrar batería
    } else if (battery_level < 40) {
        base_interval = (uint16_t)(base_interval * 1.5);  // 50% más intervalo
    }

    // Ajustar por criticidad (solo si no es crítico)
    if (criticality_level < 0.7 && battery_level > 30) {
        // En situación no crítica con batería decente, optimizar más
        base_interval = (uint16_t)(base_interval * (1.0 + (1.0 - criticality_level) * 0.3));
    }

    // Límites mínimos y máximos
    if (base_interval < 600) base_interval = 600;       // Mínimo 10 minutos
    if (base_interval > 14400) base_interval = 14400;   // Máximo 4 horas

    return base_interval;
}

/**
 * @brief Evaluar si debe iniciar riego basado en algoritmo autónomo
 */
static bool evaluate_autonomous_irrigation_need(const soil_sensor_data_t* soil_data,
                                               const ambient_sensor_data_t* ambient_data,
                                               const offline_irrigation_context_t* context) {
    if (!soil_data) return false;

    // Crear contexto de evaluación simple
    irrigation_evaluation_context_t eval_context = {0};

    // Usar lógica del domain service irrigation_logic
    irrigation_decision_t decision = irrigation_logic_evaluate_conditions(
        NULL, soil_data, &eval_context
    );

    // En modo offline, solo activar riego si es necesario o emergencia
    return (decision.decision == IRRIGATION_DECISION_START_NIGHT ||
            decision.decision == IRRIGATION_DECISION_START_THERMAL ||
            decision.decision == IRRIGATION_DECISION_START_EMERGENCY) &&
           decision.confidence_level > 0.6;
}

/**
 * @brief Estimar consumo de batería para operación offline
 */
static uint32_t estimate_offline_battery_consumption(offline_irrigation_decision_t decision,
                                                   uint16_t duration_minutes,
                                                   const offline_irrigation_context_t* context) {
    uint32_t estimated_mah = 0;

    // Consumo base del ESP32 en modo offline
    estimated_mah += 50;  // ~50mAh por evaluación (lecturas + procesamiento)

    // Consumo adicional según decisión
    switch (decision) {
        case OFFLINE_DECISION_START_IRRIGATION:
            // Válvula + relé durante duración especificada
            estimated_mah += duration_minutes * 10;  // ~10mAh por minuto de riego
            break;
        case OFFLINE_DECISION_EMERGENCY_STOP:
            estimated_mah += 20;  // Operación de parada
            break;
        case OFFLINE_DECISION_INCREASE_FREQUENCY:
            estimated_mah += 10;  // Mayor frecuencia = más evaluaciones
            break;
        default:
            break;
    }

    // Ajuste por temperatura (mayor consumo en calor)
    if (context && context->current_offline_mode == OFFLINE_MODE_EMERGENCY) {
        estimated_mah = (uint32_t)(estimated_mah * 1.2);
    }

    return estimated_mah;
}

/**
 * @brief Generar explicación detallada de decisión offline
 */
static void generate_decision_explanation(const offline_irrigation_result_t* result,
                                        const soil_sensor_data_t* soil_data,
                                        char* explanation_buffer, size_t buffer_size) {
    if (!result || !explanation_buffer || buffer_size == 0) return;

    switch (result->decision) {
        case OFFLINE_DECISION_START_IRRIGATION:
            snprintf(explanation_buffer, buffer_size,
                     "Autonomous irrigation started: avg soil %.1f%%, criticality %.1f, mode %d",
                     soil_data ? (soil_data->soil_humidity_1 + soil_data->soil_humidity_2 +
                                 soil_data->soil_humidity_3) / 3.0 : 0.0,
                     result->criticality_level,
                     result->recommended_mode);
            break;

        case OFFLINE_DECISION_EMERGENCY_STOP:
            snprintf(explanation_buffer, buffer_size,
                     "Emergency stop: Over-irrigation protection triggered offline");
            break;

        case OFFLINE_DECISION_NO_ACTION:
            snprintf(explanation_buffer, buffer_size,
                     "No action: Soil conditions adequate (criticality %.1f)",
                     result->criticality_level);
            break;

        case OFFLINE_DECISION_REDUCE_FREQUENCY:
            snprintf(explanation_buffer, buffer_size,
                     "Reduced frequency: Battery optimization (level %d%%, next check %ds)",
                     g_offline_state.current_context.battery_level_percent,
                     result->next_evaluation_interval_s);
            break;

        case OFFLINE_DECISION_INCREASE_FREQUENCY:
            snprintf(explanation_buffer, buffer_size,
                     "Increased frequency: Critical conditions detected (criticality %.1f)",
                     result->criticality_level);
            break;

        default:
            snprintf(explanation_buffer, buffer_size,
                     "Offline decision %d: criticality %.1f, confidence %.1f",
                     result->decision, result->criticality_level, result->confidence_level);
            break;
    }
}

// ============================================================================
// FUNCIONES PÚBLICAS - INICIALIZACIÓN
// ============================================================================

esp_err_t offline_irrigation_init(const safety_limits_t* safety_limits, const char* device_mac_address) {
    if (g_offline_state.offline_initialized) {
        ESP_LOGW(TAG, "Offline irrigation already initialized");
        return ESP_OK;
    }

    if (!safety_limits || !device_mac_address) {
        ESP_LOGE(TAG, "Invalid parameters for offline irrigation initialization");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing offline irrigation system");

    // Copiar parámetros
    memcpy(&g_offline_state.safety_limits, safety_limits, sizeof(safety_limits_t));
    strncpy(g_offline_state.device_mac_address, device_mac_address,
            sizeof(g_offline_state.device_mac_address) - 1);

    // Usar configuración por defecto
    memcpy(&g_offline_state.config, &DEFAULT_OFFLINE_CONFIG, sizeof(offline_irrigation_config_t));

    // Inicializar contexto offline
    memset(&g_offline_state.current_context, 0, sizeof(offline_irrigation_context_t));
    g_offline_state.current_context.current_offline_mode = OFFLINE_MODE_NORMAL;
    g_offline_state.current_context.battery_level_percent = 100;  // Asumir batería llena
    g_offline_state.current_context.solar_charging_active = false;

    // Inicializar estadísticas
    memset(&g_offline_state.statistics, 0, sizeof(offline_irrigation_statistics_t));

    g_offline_state.offline_initialized = true;
    ESP_LOGI(TAG, "Offline irrigation system initialized successfully");

    return ESP_OK;
}

esp_err_t offline_irrigation_activate(offline_activation_reason_t reason,
                                     const offline_irrigation_context_t* context) {
    if (!g_offline_state.offline_initialized) {
        ESP_LOGE(TAG, "Offline irrigation not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_offline_state.offline_mode_active) {
        ESP_LOGW(TAG, "Offline mode already active");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Activating offline mode: %s",
             offline_irrigation_activation_reason_to_string(reason));

    // Actualizar contexto
    if (context) {
        memcpy(&g_offline_state.current_context, context, sizeof(offline_irrigation_context_t));
    }

    g_offline_state.current_context.activation_reason = reason;
    g_offline_state.current_context.offline_start_timestamp = esp_timer_get_time() / 1000000;
    g_offline_state.current_context.total_offline_duration_s = 0;
    g_offline_state.current_context.last_evaluation_timestamp = 0;
    g_offline_state.current_context.current_offline_mode = OFFLINE_MODE_WARNING; // Comenzar en warning

    // Calcular primer intervalo de evaluación
    g_offline_state.current_context.evaluation_interval_s = calculate_evaluation_interval_optimized(
        OFFLINE_MODE_WARNING, g_offline_state.current_context.battery_level_percent, 0.5);

    g_offline_state.current_context.next_evaluation_timestamp =
        g_offline_state.current_context.offline_start_timestamp +
        g_offline_state.current_context.evaluation_interval_s;

    // Actualizar estadísticas
    g_offline_state.statistics.total_offline_activations++;
    g_offline_state.offline_mode_active = true;

    ESP_LOGI(TAG, "Offline mode activated: next evaluation in %" PRIu32 "s",
             g_offline_state.current_context.evaluation_interval_s);

    return ESP_OK;
}

esp_err_t offline_irrigation_deactivate(bool reconnection_successful) {
    if (!g_offline_state.offline_initialized || !g_offline_state.offline_mode_active) {
        return ESP_OK; // Ya desactivado
    }

    uint32_t current_time = esp_timer_get_time() / 1000000;
    uint32_t total_offline_time = current_time - g_offline_state.current_context.offline_start_timestamp;

    ESP_LOGI(TAG, "Deactivating offline mode after %" PRIu32 "s (reconnection: %s)",
             total_offline_time, reconnection_successful ? "successful" : "failed");

    // Actualizar estadísticas
    g_offline_state.statistics.total_offline_time_s += total_offline_time;
    if (total_offline_time > g_offline_state.statistics.longest_offline_period_s) {
        g_offline_state.statistics.longest_offline_period_s = total_offline_time;
    }

    g_offline_state.offline_mode_active = false;

    ESP_LOGI(TAG, "Offline mode deactivated - returning to online operation");
    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - EVALUACIÓN OFFLINE
// ============================================================================

offline_irrigation_result_t offline_irrigation_evaluate_conditions(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const irrigation_status_t* current_status,
    const offline_irrigation_context_t* context) {

    offline_irrigation_result_t result = {0};
    uint32_t start_time = esp_timer_get_time() / 1000;

    if (!g_offline_state.offline_initialized || !g_offline_state.offline_mode_active) {
        result.decision = OFFLINE_DECISION_NO_ACTION;
        strcpy(result.decision_explanation, "Offline mode not active");
        return result;
    }

    if (!soil_data) {
        result.decision = OFFLINE_DECISION_NO_ACTION;
        strcpy(result.decision_explanation, "No soil data available");
        g_offline_state.statistics.failed_offline_evaluations++;
        return result;
    }

    ESP_LOGI(TAG, "Evaluating offline conditions: soil %.1f/%.1f/%.1f%%",
             soil_data->soil_humidity_1, soil_data->soil_humidity_2, soil_data->soil_humidity_3);

    // Actualizar tiempo offline
    uint32_t current_time = esp_timer_get_time() / 1000000;
    g_offline_state.current_context.total_offline_duration_s =
        current_time - g_offline_state.current_context.offline_start_timestamp;

    // Calcular criticidad del cultivo
    result.criticality_level = calculate_crop_criticality_level(soil_data, ambient_data,
                                                               &g_offline_state.current_context);

    // Determinar modo offline recomendado
    result.recommended_mode = determine_offline_mode_from_criticality(result.criticality_level,
                                                                     &g_offline_state.current_context);

    // Verificar condiciones de emergencia primero
    if (safety_manager_has_emergency_condition(ambient_data, soil_data)) {
        result.decision = OFFLINE_DECISION_EMERGENCY_STOP;
        result.recommended_valve = 1;
        result.recommended_duration_minutes = 0;
        result.confidence_level = 1.0;
        strcpy(result.safety_status, "EMERGENCY: Over-irrigation protection");
        ESP_LOGW(TAG, "Emergency condition detected in offline mode");
    }
    // Evaluar necesidad de riego autónomo
    else if (evaluate_autonomous_irrigation_need(soil_data, ambient_data, &g_offline_state.current_context)) {
        result.decision = OFFLINE_DECISION_START_IRRIGATION;
        result.recommended_valve = 1;

        // Duración basada en criticidad
        if (result.criticality_level > 0.8) {
            result.recommended_duration_minutes = g_offline_state.config.max_offline_irrigation_minutes;
        } else if (result.criticality_level > 0.6) {
            result.recommended_duration_minutes = g_offline_state.config.max_offline_irrigation_minutes / 2;
        } else {
            result.recommended_duration_minutes = g_offline_state.config.max_offline_irrigation_minutes / 4;
        }

        result.confidence_level = result.criticality_level;
        strcpy(result.safety_status, "OK - Autonomous irrigation safe");
        ESP_LOGI(TAG, "Autonomous irrigation needed: %d minutes", (int)result.recommended_duration_minutes);
    }
    // No se requiere acción de riego
    else {
        result.decision = OFFLINE_DECISION_NO_ACTION;
        result.recommended_valve = 0;
        result.recommended_duration_minutes = 0;
        result.confidence_level = 1.0 - result.criticality_level;
        strcpy(result.safety_status, "OK - No irrigation needed");
    }

    // Determinar próximo intervalo de evaluación
    result.next_evaluation_interval_s = calculate_evaluation_interval_optimized(
        result.recommended_mode,
        g_offline_state.current_context.battery_level_percent,
        result.criticality_level);

    // Estimar consumo de batería
    result.estimated_battery_usage_mah = estimate_offline_battery_consumption(
        result.decision, result.recommended_duration_minutes, &g_offline_state.current_context);

    // Generar explicación
    generate_decision_explanation(&result, soil_data, result.decision_explanation,
                                 sizeof(result.decision_explanation));

    // Calcular tiempo de procesamiento
    uint32_t end_time = esp_timer_get_time() / 1000;
    result.processing_time_ms = end_time - start_time;

    // Actualizar estadísticas
    g_offline_state.statistics.successful_offline_evaluations++;

    ESP_LOGI(TAG, "Offline evaluation completed: %s (criticality %.1f, next check %d s)",
             offline_irrigation_decision_to_string(result.decision),
             result.criticality_level, (int)result.next_evaluation_interval_s);

    return result;
}

offline_mode_t offline_irrigation_determine_mode(const ambient_sensor_data_t* ambient_data,
                                                const soil_sensor_data_t* soil_data,
                                                const offline_irrigation_context_t* context) {
    if (!soil_data || !context) {
        return OFFLINE_MODE_NORMAL;
    }

    float criticality = calculate_crop_criticality_level(soil_data, ambient_data, context);
    return determine_offline_mode_from_criticality(criticality, context);
}

uint16_t offline_irrigation_calculate_evaluation_interval(offline_mode_t offline_mode,
                                                         uint8_t battery_level,
                                                         float criticality_level) {
    return calculate_evaluation_interval_optimized(offline_mode, battery_level, criticality_level);
}

bool offline_irrigation_should_start_irrigation(const soil_sensor_data_t* soil_data,
                                               const ambient_sensor_data_t* ambient_data,
                                               const offline_irrigation_context_t* context) {
    return evaluate_autonomous_irrigation_need(soil_data, ambient_data, context);
}

bool offline_irrigation_should_stop_irrigation(const soil_sensor_data_t* soil_data,
                                              const irrigation_status_t* current_status) {
    if (!soil_data || !current_status) return false;

    // Parar si se alcanza el objetivo (humedad promedio > 70%)
    float avg_humidity = (soil_data->soil_humidity_1 + soil_data->soil_humidity_2 +
                         soil_data->soil_humidity_3) / 3.0;

    return avg_humidity >= 70.0;
}

float offline_irrigation_calculate_criticality_level(const soil_sensor_data_t* soil_data,
                                                    const ambient_sensor_data_t* ambient_data,
                                                    const offline_irrigation_context_t* context) {
    return calculate_crop_criticality_level(soil_data, ambient_data, context);
}

uint32_t offline_irrigation_estimate_battery_usage(offline_irrigation_decision_t decision,
                                                   uint16_t duration_minutes,
                                                   const offline_irrigation_context_t* context) {
    return estimate_offline_battery_consumption(decision, duration_minutes, context);
}

// ============================================================================
// FUNCIONES PÚBLICAS - EJECUCIÓN DE DECISIONES
// ============================================================================

esp_err_t offline_irrigation_execute_decision(const offline_irrigation_result_t* result,
                                             const offline_irrigation_context_t* context) {
    if (!result || !g_offline_state.offline_initialized || !g_offline_state.offline_mode_active) {
        ESP_LOGE(TAG, "Invalid parameters or offline mode not active");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Executing offline decision: %s",
             offline_irrigation_decision_to_string(result->decision));

    esp_err_t execution_result = ESP_OK;

    switch (result->decision) {
        case OFFLINE_DECISION_START_IRRIGATION:
            // Usar control_irrigation para ejecutar la acción
            {
                control_irrigation_context_t irrigation_context;
                control_irrigation_operation_result_t irrigation_result =
                    control_irrigation_start_valve(&irrigation_context, result->recommended_valve,
                                                  result->recommended_duration_minutes, "offline_autonomous");

                if (irrigation_result.result_code == CONTROL_IRRIGATION_SUCCESS) {
                    g_offline_state.statistics.offline_irrigation_sessions++;
                    ESP_LOGI(TAG, "Offline irrigation started successfully");
                } else {
                    execution_result = ESP_FAIL;
                    ESP_LOGE(TAG, "Failed to start offline irrigation: %s", irrigation_result.description);
                }
            }
            break;

        case OFFLINE_DECISION_EMERGENCY_STOP:
            // Ejecutar parada de emergencia
            {
                control_irrigation_context_t irrigation_context;
                control_irrigation_execute_emergency_stop(&irrigation_context, "offline_emergency");
                g_offline_state.statistics.emergency_irrigations_offline++;
                ESP_LOGW(TAG, "Offline emergency stop executed");
            }
            break;

        case OFFLINE_DECISION_STOP_IRRIGATION:
            // Parar riego activo
            {
                control_irrigation_context_t irrigation_context;
                control_irrigation_stop_valve(&irrigation_context, 1, "offline_target_reached");
                ESP_LOGI(TAG, "Offline irrigation stopped - target reached");
            }
            break;

        case OFFLINE_DECISION_REDUCE_FREQUENCY:
        case OFFLINE_DECISION_INCREASE_FREQUENCY:
            // Ajustar intervalo de evaluación
            g_offline_state.current_context.evaluation_interval_s = result->next_evaluation_interval_s;
            g_offline_state.current_context.next_evaluation_timestamp =
                esp_timer_get_time() / 1000000 + result->next_evaluation_interval_s;
            ESP_LOGI(TAG, "Offline evaluation interval adjusted to %d s", (int)result->next_evaluation_interval_s);
            break;

        case OFFLINE_DECISION_NO_ACTION:
        default:
            // No hay acción específica que tomar
            ESP_LOGD(TAG, "No offline action required");
            break;
    }

    // Actualizar contexto offline
    g_offline_state.current_context.last_evaluation_timestamp = esp_timer_get_time() / 1000000;
    g_offline_state.current_context.current_offline_mode = result->recommended_mode;

    // Registrar evento para estadísticas
    offline_irrigation_record_event(result);

    return execution_result;
}

bool offline_irrigation_should_return_online(bool wifi_connected, bool mqtt_connected,
                                            const offline_irrigation_context_t* context) {
    // Retornar online si ambas conexiones están estables
    if (wifi_connected && mqtt_connected) {
        ESP_LOGI(TAG, "Network connections restored - can return online");
        return true;
    }

    // Si llevamos mucho tiempo offline y hay solo WiFi, intentar reconexión MQTT
    if (wifi_connected && context &&
        context->total_offline_duration_s > g_offline_state.config.mqtt_timeout_s * 3) {
        ESP_LOGI(TAG, "WiFi restored after extended offline period - attempt MQTT reconnection");
        return true;
    }

    return false;
}

// ============================================================================
// FUNCIONES PÚBLICAS - ESTADO Y CONFIGURACIÓN
// ============================================================================

esp_err_t offline_irrigation_persist_state(const offline_irrigation_context_t* context) {
    // Implementación futura con NVS
    ESP_LOGD(TAG, "State persistence not yet implemented");
    return ESP_OK;
}

esp_err_t offline_irrigation_restore_state(offline_irrigation_context_t* context) {
    // Implementación futura con NVS
    ESP_LOGD(TAG, "State restoration not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

void offline_irrigation_generate_explanation(const offline_irrigation_result_t* result,
                                            char* explanation_buffer, size_t buffer_size) {
    if (result && explanation_buffer && buffer_size > 0) {
        strncpy(explanation_buffer, result->decision_explanation, buffer_size - 1);
        explanation_buffer[buffer_size - 1] = '\0';
    }
}

void offline_irrigation_update_context(offline_irrigation_context_t* context,
                                      uint8_t battery_level,
                                      bool solar_charging_active) {
    if (context) {
        context->battery_level_percent = battery_level;
        context->solar_charging_active = solar_charging_active;
    }

    if (g_offline_state.offline_mode_active) {
        g_offline_state.current_context.battery_level_percent = battery_level;
        g_offline_state.current_context.solar_charging_active = solar_charging_active;
    }
}

esp_err_t offline_irrigation_set_config(const offline_irrigation_config_t* config) {
    if (!g_offline_state.offline_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&g_offline_state.config, config, sizeof(offline_irrigation_config_t));
    ESP_LOGI(TAG, "Offline configuration updated");
    return ESP_OK;
}

void offline_irrigation_get_config(offline_irrigation_config_t* config) {
    if (config && g_offline_state.offline_initialized) {
        memcpy(config, &g_offline_state.config, sizeof(offline_irrigation_config_t));
    }
}

void offline_irrigation_record_event(const offline_irrigation_result_t* result) {
    if (!result) return;

    switch (result->decision) {
        case OFFLINE_DECISION_START_IRRIGATION:
            g_offline_state.statistics.offline_irrigation_sessions++;
            break;
        case OFFLINE_DECISION_EMERGENCY_STOP:
            g_offline_state.statistics.emergency_irrigations_offline++;
            break;
        default:
            break;
    }

    g_offline_state.statistics.mode_transitions++;
}

void offline_irrigation_get_statistics(offline_irrigation_statistics_t* stats) {
    if (stats && g_offline_state.offline_initialized) {
        memcpy(stats, &g_offline_state.statistics, sizeof(offline_irrigation_statistics_t));
    }
}

void offline_irrigation_reset_statistics(void) {
    if (g_offline_state.offline_initialized) {
        memset(&g_offline_state.statistics, 0, sizeof(offline_irrigation_statistics_t));
        ESP_LOGI(TAG, "Offline statistics reset");
    }
}

bool offline_irrigation_is_active(void) {
    return g_offline_state.offline_mode_active;
}

esp_err_t offline_irrigation_get_current_context(offline_irrigation_context_t* context) {
    if (!context || !g_offline_state.offline_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_offline_state.offline_mode_active) {
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(context, &g_offline_state.current_context, sizeof(offline_irrigation_context_t));
    return ESP_OK;
}

// ============================================================================
// FUNCIONES PÚBLICAS - UTILIDADES
// ============================================================================

const char* offline_irrigation_activation_reason_to_string(offline_activation_reason_t reason) {
    switch (reason) {
        case OFFLINE_ACTIVATION_NONE: return "NONE";
        case OFFLINE_ACTIVATION_WIFI_TIMEOUT: return "WIFI_TIMEOUT";
        case OFFLINE_ACTIVATION_MQTT_TIMEOUT: return "MQTT_TIMEOUT";
        case OFFLINE_ACTIVATION_NETWORK_UNRELIABLE: return "NETWORK_UNRELIABLE";
        case OFFLINE_ACTIVATION_POWER_SAVING: return "POWER_SAVING";
        case OFFLINE_ACTIVATION_MANUAL: return "MANUAL";
        case OFFLINE_ACTIVATION_SYSTEM_ERROR: return "SYSTEM_ERROR";
        default: return "UNKNOWN";
    }
}

const char* offline_irrigation_decision_to_string(offline_irrigation_decision_t decision) {
    switch (decision) {
        case OFFLINE_DECISION_NO_ACTION: return "NO_ACTION";
        case OFFLINE_DECISION_START_IRRIGATION: return "START_IRRIGATION";
        case OFFLINE_DECISION_STOP_IRRIGATION: return "STOP_IRRIGATION";
        case OFFLINE_DECISION_EMERGENCY_STOP: return "EMERGENCY_STOP";
        case OFFLINE_DECISION_WAIT_CONDITIONS: return "WAIT_CONDITIONS";
        case OFFLINE_DECISION_EXTEND_EVALUATION: return "EXTEND_EVALUATION";
        case OFFLINE_DECISION_REDUCE_FREQUENCY: return "REDUCE_FREQUENCY";
        case OFFLINE_DECISION_INCREASE_FREQUENCY: return "INCREASE_FREQUENCY";
        default: return "UNKNOWN";
    }
}

uint32_t offline_irrigation_get_current_timestamp(void) {
    return esp_timer_get_time() / 1000000; // segundos desde boot
}