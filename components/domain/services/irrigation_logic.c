#include "irrigation_logic.h"
#include <string.h>
#include <math.h>
#include <time.h>

/**
 * @file irrigation_logic.c
 * @brief Implementación del servicio de lógica de riego inteligente
 *
 * Implementa la lógica de negocio para decisiones de riego optimizadas
 * para Colombia rural con sistema de una válvula única.
 */

// Variables estáticas para configuración
static safety_limits_t g_safety_limits;
static night_irrigation_config_t g_night_config;
static thermal_stress_config_t g_thermal_config;
static bool g_service_initialized = false;

// Configuración por defecto para riego nocturno
static const night_irrigation_config_t DEFAULT_NIGHT_CONFIG = {
    .window_start_hour = 10,              // 10:00
    .window_end_hour = 6,                // 06:00
    .max_temperature_celsius = 32.0f,    // Máx temp para nocturno
    .min_ambient_humidity_percent = 60.0f, // Mín humedad ambiente
    .soil_activation_threshold_percent = 45.0f, // Umbral suelo
    .default_duration_minutes = 15,      // Duración por defecto
    .max_duration_minutes = 30           // Duración máxima
};

// Configuración por defecto para estrés térmico
static const thermal_stress_config_t DEFAULT_THERMAL_CONFIG = {
    .window_start_hour = 12,             // 12:00
    .window_end_hour = 15,               // 15:00
    .min_temperature_celsius = 32.0f,    // Mín temp activación
    .max_ambient_humidity_percent = 55.0f, // Máx humedad ambiente
    .soil_minimum_threshold_percent = 30.0f, // Mín umbral suelo
    .session_duration_minutes = 2,       // Duración sesión emergencia
    .cooldown_interval_seconds = 21600   // 6 horas cooldown
};

esp_err_t irrigation_logic_init(const safety_limits_t* safety_limits)
{
    if (!safety_limits) {
        return ESP_ERR_INVALID_ARG;
    }

    // Copiar límites de seguridad
    memcpy(&g_safety_limits, safety_limits, sizeof(safety_limits_t));

    // Configurar valores por defecto
    memcpy(&g_night_config, &DEFAULT_NIGHT_CONFIG, sizeof(night_irrigation_config_t));
    memcpy(&g_thermal_config, &DEFAULT_THERMAL_CONFIG, sizeof(thermal_stress_config_t));

    g_service_initialized = true;

    return ESP_OK;
}

irrigation_decision_t irrigation_logic_evaluate_conditions(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const irrigation_evaluation_context_t* context)
{
    irrigation_decision_t result = {0};
    result.recommended_valve = 1; // Sistema de una válvula única
    result.evaluation_timestamp = irrigation_logic_get_current_timestamp();

    // Validar parámetros
    if (!ambient_data || !soil_data || !context || !g_service_initialized) {
        result.decision = IRRIGATION_DECISION_NO_ACTION;
        result.reason = IRRIGATION_REASON_SENSOR_FAILURE;
        result.confidence_level = 0.0f;
        snprintf(result.explanation, sizeof(result.explanation),
                "Error: Datos de entrada inválidos o servicio no inicializado");
        return result;
    }

    // 1. PROTECCIÓN CRÍTICA: Verificar sobre-irrigación (>80%)
    if (safety_limits_is_soil_emergency_high(&g_safety_limits, soil_data->soil_humidity_1) ||
        safety_limits_is_soil_emergency_high(&g_safety_limits, soil_data->soil_humidity_2) ||
        safety_limits_is_soil_emergency_high(&g_safety_limits, soil_data->soil_humidity_3)) {

        result.decision = IRRIGATION_DECISION_EMERGENCY_STOP;
        result.reason = IRRIGATION_REASON_OVER_IRRIGATION;
        result.recommended_duration_minutes = 0;
        result.confidence_level = 1.0f;
        snprintf(result.explanation, sizeof(result.explanation),
                "PARADA EMERGENCIA: Sensor >80%% detectado (%.1f%%, %.1f%%, %.1f%%)",
                soil_data->soil_humidity_1, soil_data->soil_humidity_2, soil_data->soil_humidity_3);
        return result;
    }

    // 2. PROTECCIÓN TÉRMICA: Temperatura demasiado alta
    if (safety_limits_requires_temperature_emergency_stop(&g_safety_limits, ambient_data->ambient_temperature)) {
        result.decision = IRRIGATION_DECISION_EMERGENCY_STOP;
        result.reason = IRRIGATION_REASON_TEMPERATURE_HIGH;
        result.recommended_duration_minutes = 0;
        result.confidence_level = 1.0f;
        snprintf(result.explanation, sizeof(result.explanation),
                "PARADA EMERGENCIA: Temperatura crítica %.1f°C (>%.1f°C)",
                ambient_data->ambient_temperature, g_safety_limits.temperature.emergency_stop_max);
        return result;
    }

    // 3. TARGET ALCANZADO: Todos sensores >= 75%
    if (safety_limits_is_soil_target_reached(&g_safety_limits, soil_data->soil_humidity_1) &&
        safety_limits_is_soil_target_reached(&g_safety_limits, soil_data->soil_humidity_2) &&
        safety_limits_is_soil_target_reached(&g_safety_limits, soil_data->soil_humidity_3)) {

        result.decision = IRRIGATION_DECISION_STOP_TARGET;
        result.reason = IRRIGATION_REASON_TARGET_REACHED;
        result.recommended_duration_minutes = 0;
        result.confidence_level = 0.9f;
        snprintf(result.explanation, sizeof(result.explanation),
                "Objetivo alcanzado: Todos sensores ≥75%% (%.1f%%, %.1f%%, %.1f%%)",
                soil_data->soil_humidity_1, soil_data->soil_humidity_2, soil_data->soil_humidity_3);
        return result;
    }

    // 4. EMERGENCIA DE SEQUÍA: Cualquier sensor < 30%
    if (safety_limits_is_soil_critical_low(&g_safety_limits, soil_data->soil_humidity_1) ||
        safety_limits_is_soil_critical_low(&g_safety_limits, soil_data->soil_humidity_2) ||
        safety_limits_is_soil_critical_low(&g_safety_limits, soil_data->soil_humidity_3)) {

        result.decision = IRRIGATION_DECISION_START_EMERGENCY;
        result.reason = IRRIGATION_REASON_SOIL_CRITICAL;
        result.recommended_duration_minutes = g_safety_limits.duration.emergency_duration_minutes;
        result.confidence_level = 1.0f;
        snprintf(result.explanation, sizeof(result.explanation),
                "RIEGO EMERGENCIA: Sensor crítico <30%% detectado (%.1f%%, %.1f%%, %.1f%%)",
                soil_data->soil_humidity_1, soil_data->soil_humidity_2, soil_data->soil_humidity_3);
        return result;
    }

    // 5. Calcular promedio de humedad del suelo
    float average_soil_humidity = (soil_data->soil_humidity_1 +
                                  soil_data->soil_humidity_2 +
                                  soil_data->soil_humidity_3) / 3.0f;

    // 6. EVALUACIÓN POR VENTANAS DE TIEMPO
    if (irrigation_logic_is_night_window(context->current_hour)) {
        // Ventana nocturna (00:00-06:00) - ÓPTIMA para riego
        return irrigation_logic_evaluate_night_irrigation(ambient_data, soil_data, context);
    }
    else if (irrigation_logic_is_thermal_stress_window(context->current_hour)) {
        // Ventana estrés térmico (12:00-15:00) - Solo si crítico
        return irrigation_logic_evaluate_thermal_stress_irrigation(ambient_data, soil_data, context);
    }
    else {
        // Fuera de ventanas óptimas - Solo si muy necesario
        if (average_soil_humidity < g_safety_limits.soil_humidity.warning_low_percent) {
            result.decision = IRRIGATION_DECISION_START_THERMAL; // Riego subóptimo
            result.reason = IRRIGATION_REASON_SOIL_LOW;
            result.recommended_duration_minutes = g_safety_limits.duration.default_duration_minutes;
            result.confidence_level = 0.6f;
            snprintf(result.explanation, sizeof(result.explanation),
                    "Riego necesario fuera ventana óptima: promedio %.1f%% <%.1f%%",
                    average_soil_humidity, g_safety_limits.soil_humidity.warning_low_percent);
            return result;
        }
    }

    // 7. NO SE REQUIERE ACCIÓN
    result.decision = IRRIGATION_DECISION_NO_ACTION;
    result.reason = IRRIGATION_REASON_SOIL_LOW; // No hay razón específica
    result.recommended_duration_minutes = 0;
    result.confidence_level = 0.8f;
    snprintf(result.explanation, sizeof(result.explanation),
            "Sin acción necesaria: promedio suelo %.1f%%, condiciones normales",
            average_soil_humidity);

    return result;
}

irrigation_decision_t irrigation_logic_evaluate_night_irrigation(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const irrigation_evaluation_context_t* context)
{
    irrigation_decision_t result = {0};
    result.recommended_valve = 1;
    result.evaluation_timestamp = irrigation_logic_get_current_timestamp();

    // Validar condiciones para riego nocturno
    if (!irrigation_logic_conditions_allow_night_irrigation(ambient_data)) {
        result.decision = IRRIGATION_DECISION_WAIT_COOLDOWN;
        result.reason = IRRIGATION_REASON_THERMAL_STRESS;
        result.confidence_level = 0.3f;
        snprintf(result.explanation, sizeof(result.explanation),
                "Ventana nocturna: condiciones no óptimas (T:%.1f°C, RH:%.1f%%)",
                ambient_data->ambient_temperature, ambient_data->ambient_humidity);
        return result;
    }

    float average_soil_humidity = (soil_data->soil_humidity_1 +
                                  soil_data->soil_humidity_2 +
                                  soil_data->soil_humidity_3) / 3.0f;

    // Verificar si necesita riego nocturno
    if (average_soil_humidity < g_night_config.soil_activation_threshold_percent) {
        result.decision = IRRIGATION_DECISION_START_NIGHT;
        result.reason = IRRIGATION_REASON_NIGHT_OPTIMAL;
        result.recommended_duration_minutes = g_night_config.default_duration_minutes;
        result.confidence_level = 0.95f;
        snprintf(result.explanation, sizeof(result.explanation),
                "Riego nocturno óptimo: suelo %.1f%% <%.1f%%, condiciones ideales",
                average_soil_humidity, g_night_config.soil_activation_threshold_percent);
        return result;
    }

    // Suelo suficientemente húmedo durante ventana nocturna
    result.decision = IRRIGATION_DECISION_NO_ACTION;
    result.reason = IRRIGATION_REASON_NIGHT_OPTIMAL;
    result.confidence_level = 0.8f;
    snprintf(result.explanation, sizeof(result.explanation),
            "Ventana nocturna: suelo suficiente %.1f%% >%.1f%%",
            average_soil_humidity, g_night_config.soil_activation_threshold_percent);

    return result;
}

irrigation_decision_t irrigation_logic_evaluate_thermal_stress_irrigation(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const irrigation_evaluation_context_t* context)
{
    irrigation_decision_t result = {0};
    result.recommended_valve = 1;
    result.evaluation_timestamp = irrigation_logic_get_current_timestamp();

    // Verificar si las condiciones requieren riego por estrés térmico
    if (!irrigation_logic_conditions_require_thermal_stress(ambient_data, soil_data)) {
        result.decision = IRRIGATION_DECISION_NO_ACTION;
        result.reason = IRRIGATION_REASON_THERMAL_STRESS;
        result.confidence_level = 0.7f;
        snprintf(result.explanation, sizeof(result.explanation),
                "Estrés térmico: condiciones no críticas (T:%.1f°C, RH:%.1f%%)",
                ambient_data->ambient_temperature, ambient_data->ambient_humidity);
        return result;
    }

    // Condiciones críticas - activar riego de emergencia térmico
    result.decision = IRRIGATION_DECISION_START_THERMAL;
    result.reason = IRRIGATION_REASON_THERMAL_STRESS;
    result.recommended_duration_minutes = g_thermal_config.session_duration_minutes;
    result.confidence_level = 0.8f;

    float min_soil = fminf(fminf(soil_data->soil_humidity_1, soil_data->soil_humidity_2),
                          soil_data->soil_humidity_3);

    snprintf(result.explanation, sizeof(result.explanation),
            "Riego estrés térmico: T:%.1f°C >%.1f°C, suelo mín:%.1f%%",
            ambient_data->ambient_temperature, g_thermal_config.min_temperature_celsius, min_soil);

    return result;
}

irrigation_decision_t irrigation_logic_evaluate_stop_conditions(
    const soil_sensor_data_t* soil_data)
{
    irrigation_decision_t result = {0};
    result.recommended_valve = 1;
    result.evaluation_timestamp = irrigation_logic_get_current_timestamp();

    if (!soil_data || !g_service_initialized) {
        result.decision = IRRIGATION_DECISION_EMERGENCY_STOP;
        result.reason = IRRIGATION_REASON_SENSOR_FAILURE;
        result.confidence_level = 1.0f;
        snprintf(result.explanation, sizeof(result.explanation), "Error datos sensores");
        return result;
    }

    // CRÍTICO: Verificar sobre-irrigación
    if (safety_limits_is_soil_emergency_high(&g_safety_limits, soil_data->soil_humidity_1) ||
        safety_limits_is_soil_emergency_high(&g_safety_limits, soil_data->soil_humidity_2) ||
        safety_limits_is_soil_emergency_high(&g_safety_limits, soil_data->soil_humidity_3)) {

        result.decision = IRRIGATION_DECISION_EMERGENCY_STOP;
        result.reason = IRRIGATION_REASON_OVER_IRRIGATION;
        result.confidence_level = 1.0f;
        snprintf(result.explanation, sizeof(result.explanation),
                "PARADA EMERGENCIA: Sobre-irrigación detectada");
        return result;
    }

    // Verificar si objetivo alcanzado (75%)
    if (safety_limits_is_soil_target_reached(&g_safety_limits, soil_data->soil_humidity_1) &&
        safety_limits_is_soil_target_reached(&g_safety_limits, soil_data->soil_humidity_2) &&
        safety_limits_is_soil_target_reached(&g_safety_limits, soil_data->soil_humidity_3)) {

        result.decision = IRRIGATION_DECISION_STOP_TARGET;
        result.reason = IRRIGATION_REASON_TARGET_REACHED;
        result.confidence_level = 0.9f;
        snprintf(result.explanation, sizeof(result.explanation),
                "Objetivo alcanzado: todos sensores ≥75%%");
        return result;
    }

    // Continuar riego - objetivo no alcanzado aún
    result.decision = IRRIGATION_DECISION_CONTINUE;
    result.reason = IRRIGATION_REASON_TARGET_REACHED;
    result.confidence_level = 0.8f;
    snprintf(result.explanation, sizeof(result.explanation), "Continuar hasta objetivo");

    return result;
}

bool irrigation_logic_has_emergency_condition(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data)
{
    if (!ambient_data || !soil_data || !g_service_initialized) {
        return true; // Fallo de datos es emergencia
    }

    // Sobre-irrigación: cualquier sensor > 80%
    if (safety_limits_is_soil_emergency_high(&g_safety_limits, soil_data->soil_humidity_1) ||
        safety_limits_is_soil_emergency_high(&g_safety_limits, soil_data->soil_humidity_2) ||
        safety_limits_is_soil_emergency_high(&g_safety_limits, soil_data->soil_humidity_3)) {
        return true;
    }

    // Temperatura peligrosa: > 40°C
    if (safety_limits_requires_temperature_emergency_stop(&g_safety_limits, ambient_data->ambient_temperature)) {
        return true;
    }

    // Fallo de sensores: valores imposibles
    if (soil_data->soil_humidity_1 < 0.0f || soil_data->soil_humidity_1 > 100.0f ||
        soil_data->soil_humidity_2 < 0.0f || soil_data->soil_humidity_2 > 100.0f ||
        soil_data->soil_humidity_3 < 0.0f || soil_data->soil_humidity_3 > 100.0f) {
        return true;
    }

    return false;
}

uint16_t irrigation_logic_calculate_optimal_duration(
    const soil_sensor_data_t* soil_data,
    const ambient_sensor_data_t* ambient_data,
    bool is_emergency,
    power_mode_t power_mode)
{
    if (!soil_data || !g_service_initialized) {
        return g_safety_limits.duration.min_session_minutes;
    }

    // Duración de emergencia - corta y segura
    if (is_emergency) {
        return g_safety_limits.duration.emergency_duration_minutes; // 5 min
    }

    // Calcular déficit de humedad promedio
    float average_soil = (soil_data->soil_humidity_1 +
                         soil_data->soil_humidity_2 +
                         soil_data->soil_humidity_3) / 3.0f;

    float target_humidity = g_safety_limits.soil_humidity.target_max_percent; // 75%
    float deficit = target_humidity - average_soil;

    // Duración base según déficit
    uint16_t base_duration;
    if (deficit <= 5.0f) {
        base_duration = 5;  // Déficit pequeño
    } else if (deficit <= 15.0f) {
        base_duration = 10; // Déficit medio
    } else if (deficit <= 25.0f) {
        base_duration = 15; // Déficit normal
    } else {
        base_duration = 20; // Déficit grande
    }

    // Ajuste por modo de energía
    switch (power_mode) {
        case POWER_MODE_CRITICAL:
        case POWER_MODE_EMERGENCY:
            base_duration = base_duration / 2; // Reducir a la mitad
            break;
        case POWER_MODE_SAVING:
            base_duration = (base_duration * 3) / 4; // Reducir 25%
            break;
        case POWER_MODE_NORMAL:
        case POWER_MODE_CHARGING:
        default:
            // Sin ajuste
            break;
    }

    // Aplicar límites de seguridad
    if (base_duration < g_safety_limits.duration.min_session_minutes) {
        base_duration = g_safety_limits.duration.min_session_minutes;
    }
    if (base_duration > g_safety_limits.duration.max_session_minutes) {
        base_duration = g_safety_limits.duration.max_session_minutes;
    }

    return base_duration;
}

uint8_t irrigation_logic_select_optimal_valve(
    const soil_sensor_data_t* soil_data,
    uint8_t last_used_valve)
{
    // Sistema de una válvula única - siempre retornar 1
    (void)soil_data;      // Suprimir warning unused parameter
    (void)last_used_valve; // Suprimir warning unused parameter
    return 1;
}

bool irrigation_logic_is_night_window(uint8_t current_hour)
{
    if (!g_service_initialized) {
        return false;
    }

    // Ventana nocturna: 00:00-06:00 (configurada en night_config)
    return (current_hour >= g_night_config.window_start_hour &&
            current_hour < g_night_config.window_end_hour);
}

bool irrigation_logic_is_thermal_stress_window(uint8_t current_hour)
{
    if (!g_service_initialized) {
        return false;
    }

    // Ventana estrés térmico: 12:00-15:00 (configurada en thermal_config)
    return (current_hour >= g_thermal_config.window_start_hour &&
            current_hour < g_thermal_config.window_end_hour);
}

bool irrigation_logic_conditions_allow_night_irrigation(
    const ambient_sensor_data_t* ambient_data)
{
    if (!ambient_data || !g_service_initialized) {
        return false;
    }

    // Temperatura debe ser ≤ 32°C
    if (ambient_data->ambient_temperature > g_night_config.max_temperature_celsius) {
        return false;
    }

    // Humedad ambiente debe ser ≥ 60%
    if (ambient_data->ambient_humidity < g_night_config.min_ambient_humidity_percent) {
        return false;
    }

    return true;
}

bool irrigation_logic_conditions_require_thermal_stress(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data)
{
    if (!ambient_data || !soil_data || !g_service_initialized) {
        return false;
    }

    // Temperatura debe ser ≥ 32°C
    if (ambient_data->ambient_temperature < g_thermal_config.min_temperature_celsius) {
        return false;
    }

    // Humedad ambiente debe ser ≤ 55%
    if (ambient_data->ambient_humidity > g_thermal_config.max_ambient_humidity_percent) {
        return false;
    }

    // Al menos un sensor de suelo debe estar crítico (≤ 30%)
    if (soil_data->soil_humidity_1 <= g_thermal_config.soil_minimum_threshold_percent ||
        soil_data->soil_humidity_2 <= g_thermal_config.soil_minimum_threshold_percent ||
        soil_data->soil_humidity_3 <= g_thermal_config.soil_minimum_threshold_percent) {
        return true;
    }

    return false;
}

float irrigation_logic_calculate_confidence_level(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    irrigation_decision_type_t decision_type)
{
    if (!ambient_data || !soil_data) {
        return 0.0f;
    }

    float confidence = 0.5f; // Base

    // Factor 1: Calidad de datos
    bool valid_temp = (ambient_data->ambient_temperature >= -10.0f && ambient_data->ambient_temperature <= 60.0f);
    bool valid_humidity = (ambient_data->ambient_humidity >= 0.0f && ambient_data->ambient_humidity <= 100.0f);
    bool valid_soil1 = (soil_data->soil_humidity_1 >= 0.0f && soil_data->soil_humidity_1 <= 100.0f);
    bool valid_soil2 = (soil_data->soil_humidity_2 >= 0.0f && soil_data->soil_humidity_2 <= 100.0f);
    bool valid_soil3 = (soil_data->soil_humidity_3 >= 0.0f && soil_data->soil_humidity_3 <= 100.0f);

    int valid_count = valid_temp + valid_humidity + valid_soil1 + valid_soil2 + valid_soil3;
    confidence += (valid_count / 5.0f) * 0.3f; // Hasta +30%

    // Factor 2: Consistencia de sensores de suelo
    float avg_soil = (soil_data->soil_humidity_1 + soil_data->soil_humidity_2 + soil_data->soil_humidity_3) / 3.0f;
    float max_deviation = fmaxf(fmaxf(fabsf(soil_data->soil_humidity_1 - avg_soil),
                                     fabsf(soil_data->soil_humidity_2 - avg_soil)),
                               fabsf(soil_data->soil_humidity_3 - avg_soil));

    if (max_deviation < 5.0f) {
        confidence += 0.15f; // Muy consistentes
    } else if (max_deviation < 10.0f) {
        confidence += 0.05f; // Moderadamente consistentes
    }

    // Factor 3: Tipo de decisión
    switch (decision_type) {
        case IRRIGATION_DECISION_EMERGENCY_STOP:
        case IRRIGATION_DECISION_START_EMERGENCY:
            confidence = 1.0f; // Máxima confianza en emergencias
            break;
        case IRRIGATION_DECISION_START_NIGHT:
            confidence += 0.15f; // Alta confianza en riego nocturno
            break;
        case IRRIGATION_DECISION_STOP_TARGET:
            confidence += 0.10f; // Buena confianza al alcanzar objetivo
            break;
        default:
            // Sin ajuste adicional
            break;
    }

    // Limitar a rango válido
    if (confidence > 1.0f) confidence = 1.0f;
    if (confidence < 0.0f) confidence = 0.0f;

    return confidence;
}

void irrigation_logic_generate_explanation(
    const irrigation_decision_t* decision,
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    char* explanation_buffer,
    size_t buffer_size)
{
    if (!decision || !explanation_buffer || buffer_size == 0) {
        return;
    }

    // Usar la explicación ya generada en la decisión
    if (strlen(decision->explanation) > 0) {
        strncpy(explanation_buffer, decision->explanation, buffer_size - 1);
        explanation_buffer[buffer_size - 1] = '\0';
        return;
    }

    // Generar explicación por defecto si no existe
    float avg_soil = 0.0f;
    if (soil_data) {
        avg_soil = (soil_data->soil_humidity_1 + soil_data->soil_humidity_2 + soil_data->soil_humidity_3) / 3.0f;
    }

    snprintf(explanation_buffer, buffer_size,
            "Decisión: %s, Suelo: %.1f%%, Temp: %.1f°C, Confianza: %.1f%%",
            irrigation_logic_decision_type_to_string(decision->decision),
            avg_soil,
            ambient_data ? ambient_data->ambient_temperature : 0.0f,
            decision->confidence_level * 100.0f);
}

void irrigation_logic_get_night_irrigation_config(night_irrigation_config_t* config)
{
    if (config && g_service_initialized) {
        memcpy(config, &g_night_config, sizeof(night_irrigation_config_t));
    }
}

void irrigation_logic_get_thermal_stress_config(thermal_stress_config_t* config)
{
    if (config && g_service_initialized) {
        memcpy(config, &g_thermal_config, sizeof(thermal_stress_config_t));
    }
}

esp_err_t irrigation_logic_set_night_irrigation_config(const night_irrigation_config_t* config)
{
    if (!config || !g_service_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    // Validar configuración básica
    if (config->window_start_hour >= 24 || config->window_end_hour >= 24 ||
        config->max_temperature_celsius < 0.0f || config->max_temperature_celsius > 60.0f ||
        config->min_ambient_humidity_percent < 0.0f || config->min_ambient_humidity_percent > 100.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&g_night_config, config, sizeof(night_irrigation_config_t));
    return ESP_OK;
}

esp_err_t irrigation_logic_set_thermal_stress_config(const thermal_stress_config_t* config)
{
    if (!config || !g_service_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    // Validar configuración básica
    if (config->window_start_hour >= 24 || config->window_end_hour >= 24 ||
        config->min_temperature_celsius < 0.0f || config->min_temperature_celsius > 60.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&g_thermal_config, config, sizeof(thermal_stress_config_t));
    return ESP_OK;
}

const char* irrigation_logic_decision_type_to_string(irrigation_decision_type_t decision_type)
{
    switch (decision_type) {
        case IRRIGATION_DECISION_NO_ACTION:
            return "sin_accion";
        case IRRIGATION_DECISION_START_NIGHT:
            return "iniciar_nocturno";
        case IRRIGATION_DECISION_START_THERMAL:
            return "iniciar_termico";
        case IRRIGATION_DECISION_START_EMERGENCY:
            return "iniciar_emergencia";
        case IRRIGATION_DECISION_CONTINUE:
            return "continuar";
        case IRRIGATION_DECISION_STOP_TARGET:
            return "parar_objetivo";
        case IRRIGATION_DECISION_STOP_TIMEOUT:
            return "parar_timeout";
        case IRRIGATION_DECISION_EMERGENCY_STOP:
            return "parada_emergencia";
        case IRRIGATION_DECISION_WAIT_COOLDOWN:
            return "esperar_cooldown";
        default:
            return "desconocido";
    }
}

const char* irrigation_logic_decision_reason_to_string(irrigation_decision_reason_t reason)
{
    switch (reason) {
        case IRRIGATION_REASON_SOIL_CRITICAL:
            return "suelo_critico";
        case IRRIGATION_REASON_SOIL_LOW:
            return "suelo_bajo";
        case IRRIGATION_REASON_THERMAL_STRESS:
            return "estres_termico";
        case IRRIGATION_REASON_NIGHT_OPTIMAL:
            return "nocturno_optimo";
        case IRRIGATION_REASON_TARGET_REACHED:
            return "objetivo_alcanzado";
        case IRRIGATION_REASON_OVER_IRRIGATION:
            return "sobre_irrigacion";
        case IRRIGATION_REASON_TEMPERATURE_HIGH:
            return "temperatura_alta";
        case IRRIGATION_REASON_DURATION_EXCEEDED:
            return "duracion_excedida";
        case IRRIGATION_REASON_SENSOR_FAILURE:
            return "fallo_sensor";
        case IRRIGATION_REASON_POWER_SAVING:
            return "ahorro_energia";
        default:
            return "desconocido";
    }
}

uint32_t irrigation_logic_get_current_timestamp(void)
{
    // Usar time() estándar de C - en ESP-IDF devuelve segundos desde epoch Unix
    return (uint32_t)time(NULL);
}