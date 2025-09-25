#include "safety_manager.h"
#include <string.h>
#include <math.h>
#include <time.h>
#include <inttypes.h>

/**
 * @file safety_manager.c
 * @brief Implementación del servicio de gestión de seguridad crítica
 *
 * Implementa todas las validaciones y protecciones para prevenir
 * sobre-irrigación y daño a cultivos en sistema de una válvula.
 */

// Variables estáticas para configuración y estado
static safety_limits_t g_safety_limits;
static safety_critical_limits_t g_critical_limits;
static safety_statistics_t g_statistics;
static bool g_service_initialized = false;
static bool g_override_active = false;
static char g_override_reason[64] = {0};

// Configuración por defecto de límites críticos
static const safety_critical_limits_t DEFAULT_CRITICAL_LIMITS = {
    .soil_emergency_threshold_percent = 80.0f,    // Parada inmediata >80%
    .soil_warning_threshold_percent = 75.0f,      // Advertencia >75%
    .temperature_emergency_celsius = 40.0f,       // Parada inmediata >40°C
    .temperature_warning_celsius = 35.0f,         // Advertencia >35°C
    .max_session_duration_minutes = 30,           // Máx 30 min por sesión
    .min_cooldown_duration_minutes = 240,         // Mín 4h cooldown
    .min_working_soil_sensors = 2,                // Mín 2 de 3 sensores
    .sensor_response_timeout_ms = 5000,           // 5s timeout
    .max_emergency_stops_per_day = 10,            // Máx 10 emergencias/día
    .emergency_cooldown_minutes = 60              // 1h post-emergencia
};

esp_err_t safety_manager_init(const safety_limits_t* safety_limits)
{
    if (!safety_limits) {
        return ESP_ERR_INVALID_ARG;
    }

    // Copiar límites de seguridad
    memcpy(&g_safety_limits, safety_limits, sizeof(safety_limits_t));

    // Configurar límites críticos por defecto
    memcpy(&g_critical_limits, &DEFAULT_CRITICAL_LIMITS, sizeof(safety_critical_limits_t));

    // Inicializar estadísticas
    memset(&g_statistics, 0, sizeof(safety_statistics_t));
    g_statistics.system_safety_score = 1.0f; // Iniciar con puntuación máxima

    g_service_initialized = true;
    g_override_active = false;

    return ESP_OK;
}

safety_evaluation_result_t safety_manager_evaluate_start_irrigation(
    const irrigation_command_t* command,
    const irrigation_status_t* current_status,
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const safety_evaluation_context_t* context)
{
    safety_evaluation_result_t result = {0};
    result.evaluation_timestamp = safety_manager_get_current_timestamp();
    result.overall_result = SAFETY_CHECK_OK;
    result.violation_type = SAFETY_VIOLATION_NONE;
    result.severity_level = 0.0f;

    // Validar parámetros de entrada
    if (!command || !current_status || !ambient_data || !soil_data || !context || !g_service_initialized) {
        result.overall_result = SAFETY_CHECK_EMERGENCY;
        result.violation_type = SAFETY_VIOLATION_SENSOR_FAILURE;
        result.severity_level = 1.0f;
        result.immediate_action_required = true;
        snprintf(result.violation_description, sizeof(result.violation_description),
                "Datos de entrada inválidos o servicio no inicializado");
        snprintf(result.recommended_action, sizeof(result.recommended_action), "Verificar sensores");
        return result;
    }

    // 1. PROTECCIÓN CRÍTICA: Verificar sobre-irrigación (>80%)
    if (safety_manager_check_over_irrigation_protection(soil_data)) {
        result.overall_result = SAFETY_CHECK_EMERGENCY;
        result.violation_type = SAFETY_VIOLATION_SOIL_OVER_IRRIGATION;
        result.severity_level = 1.0f;
        result.immediate_action_required = true;

        safety_manager_generate_violation_description(result.violation_type, soil_data,
                                                    result.violation_description,
                                                    sizeof(result.violation_description));
        snprintf(result.recommended_action, sizeof(result.recommended_action), "PARAR RIEGO");

        safety_manager_log_safety_event(&result);
        return result;
    }

    // 2. PROTECCIÓN TÉRMICA: Temperatura crítica (>40°C)
    if (safety_manager_check_thermal_protection(ambient_data)) {
        result.overall_result = SAFETY_CHECK_EMERGENCY;
        result.violation_type = SAFETY_VIOLATION_TEMPERATURE_HIGH;
        result.severity_level = 1.0f;
        result.immediate_action_required = true;
        snprintf(result.violation_description, sizeof(result.violation_description),
                "Temperatura crítica: %.1f°C >%.1f°C",
                ambient_data->ambient_temperature, g_critical_limits.temperature_emergency_celsius);
        snprintf(result.recommended_action, sizeof(result.recommended_action), "PARAR RIEGO");

        safety_manager_log_safety_event(&result);
        return result;
    }

    // 3. VERIFICAR SISTEMA EN PARADA DE EMERGENCIA
    if (current_status->emergency_stop_active) {
        result.overall_result = SAFETY_CHECK_BLOCKED;
        result.violation_type = SAFETY_VIOLATION_SYSTEM_OVERLOAD;
        result.severity_level = 0.8f;
        result.immediate_action_required = false;
        snprintf(result.violation_description, sizeof(result.violation_description),
                "Sistema en parada de emergencia activa");
        snprintf(result.recommended_action, sizeof(result.recommended_action), "Resolver emergencia");
        return result;
    }

    // 4. VERIFICAR VÁLVULA YA ACTIVA (sistema de una válvula)
    if (irrigation_status_is_valve_active(current_status, 1)) {
        result.overall_result = SAFETY_CHECK_BLOCKED;
        result.violation_type = SAFETY_VIOLATION_SYSTEM_OVERLOAD;
        result.severity_level = 0.5f;
        snprintf(result.violation_description, sizeof(result.violation_description),
                "Válvula única ya está activa");
        snprintf(result.recommended_action, sizeof(result.recommended_action), "Esperar fin sesión");
        return result;
    }

    // 5. VERIFICAR PERÍODO COOLDOWN
    if (safety_manager_check_cooldown_period(context, 1)) {
        result.overall_result = SAFETY_CHECK_BLOCKED;
        result.violation_type = SAFETY_VIOLATION_COOLDOWN_VIOLATED;
        result.severity_level = 0.6f;
        snprintf(result.violation_description, sizeof(result.violation_description),
                "Válvula en período de cooldown (<%dh desde última sesión)",
                g_critical_limits.min_cooldown_duration_minutes / 60);
        snprintf(result.recommended_action, sizeof(result.recommended_action), "Esperar cooldown");
        return result;
    }

    // 6. VERIFICAR DURACIÓN SOLICITADA
    if (command->duration_minutes > g_critical_limits.max_session_duration_minutes) {
        result.overall_result = SAFETY_CHECK_BLOCKED;
        result.violation_type = SAFETY_VIOLATION_DURATION_EXCEEDED;
        result.severity_level = 0.7f;
        snprintf(result.violation_description, sizeof(result.violation_description),
                "Duración solicitada %d min >%d min máximo",
                command->duration_minutes, g_critical_limits.max_session_duration_minutes);
        snprintf(result.recommended_action, sizeof(result.recommended_action), "Reducir duración");
        return result;
    }

    // 7. VERIFICAR SALUD DE SENSORES
    if (!safety_manager_check_sensor_health(soil_data, ambient_data)) {
        result.overall_result = SAFETY_CHECK_CRITICAL;
        result.violation_type = SAFETY_VIOLATION_SENSOR_FAILURE;
        result.severity_level = 0.8f;
        snprintf(result.violation_description, sizeof(result.violation_description),
                "Fallo crítico de sensores - datos inconsistentes");
        snprintf(result.recommended_action, sizeof(result.recommended_action), "Revisar sensores");

        // Permitir solo si override está activo
        if (!g_override_active) {
            result.immediate_action_required = true;
            safety_manager_log_safety_event(&result);
            return result;
        }
    }

    // 8. VERIFICAR LÍMITES DIARIOS DE EMERGENCIAS
    if (safety_manager_check_daily_emergency_limits(context)) {
        result.overall_result = SAFETY_CHECK_WARNING;
        result.violation_type = SAFETY_VIOLATION_MULTIPLE_VIOLATIONS;
        result.severity_level = 0.4f;
        snprintf(result.violation_description, sizeof(result.violation_description),
                "Múltiples emergencias hoy (%" PRIu32 ") - sistema inestable",
                context->emergency_stop_count_today);
        snprintf(result.recommended_action, sizeof(result.recommended_action), "Revisar sistema");
    }

    // 9. ADVERTENCIAS NO BLOQUEANTES

    // Advertencia temperatura alta
    if (ambient_data->ambient_temperature > g_critical_limits.temperature_warning_celsius) {
        result.overall_result = SAFETY_CHECK_WARNING;
        result.severity_level = fmaxf(result.severity_level, 0.3f);
        snprintf(result.violation_description, sizeof(result.violation_description),
                "Temperatura alta: %.1f°C >%.1f°C - monitorear",
                ambient_data->ambient_temperature, g_critical_limits.temperature_warning_celsius);
    }

    // Advertencia humedad suelo alta
    float max_soil = fmaxf(fmaxf(soil_data->soil_humidity_1, soil_data->soil_humidity_2),
                          soil_data->soil_humidity_3);
    if (max_soil > g_critical_limits.soil_warning_threshold_percent) {
        result.overall_result = SAFETY_CHECK_WARNING;
        result.severity_level = fmaxf(result.severity_level, 0.3f);
        snprintf(result.violation_description, sizeof(result.violation_description),
                "Humedad suelo alta: %.1f%% >%.1f%% - monitorear",
                max_soil, g_critical_limits.soil_warning_threshold_percent);
    }

    // TODO CORRECTO - PERMITIR INICIO
    if (result.overall_result == SAFETY_CHECK_OK) {
        snprintf(result.violation_description, sizeof(result.violation_description),
                "Condiciones seguras para iniciar riego");
        snprintf(result.recommended_action, sizeof(result.recommended_action), "Proceder");
    }

    // Actualizar estadísticas
    g_statistics.total_safety_checks_performed++;
    if (result.overall_result == SAFETY_CHECK_WARNING) {
        g_statistics.warnings_issued_today++;
    }

    return result;
}

safety_evaluation_result_t safety_manager_evaluate_during_irrigation(
    const irrigation_status_t* current_status,
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const safety_evaluation_context_t* context)
{
    safety_evaluation_result_t result = {0};
    result.evaluation_timestamp = safety_manager_get_current_timestamp();
    result.overall_result = SAFETY_CHECK_OK;
    result.violation_type = SAFETY_VIOLATION_NONE;

    if (!current_status || !ambient_data || !soil_data || !context || !g_service_initialized) {
        result.overall_result = SAFETY_CHECK_EMERGENCY;
        result.violation_type = SAFETY_VIOLATION_SENSOR_FAILURE;
        result.severity_level = 1.0f;
        result.immediate_action_required = true;
        snprintf(result.violation_description, sizeof(result.violation_description),
                "Fallo crítico de datos durante riego");
        snprintf(result.recommended_action, sizeof(result.recommended_action), "PARAR INMEDIATAMENTE");
        return result;
    }

    // 1. VERIFICAR SOBRE-IRRIGACIÓN (CRÍTICO)
    if (safety_manager_check_over_irrigation_protection(soil_data)) {
        result.overall_result = SAFETY_CHECK_EMERGENCY;
        result.violation_type = SAFETY_VIOLATION_SOIL_OVER_IRRIGATION;
        result.severity_level = 1.0f;
        result.immediate_action_required = true;
        safety_manager_generate_violation_description(result.violation_type, soil_data,
                                                    result.violation_description,
                                                    sizeof(result.violation_description));
        snprintf(result.recommended_action, sizeof(result.recommended_action), "PARAR INMEDIATAMENTE");

        safety_manager_log_safety_event(&result);
        return result;
    }

    // 2. VERIFICAR TEMPERATURA CRÍTICA
    if (safety_manager_check_thermal_protection(ambient_data)) {
        result.overall_result = SAFETY_CHECK_EMERGENCY;
        result.violation_type = SAFETY_VIOLATION_TEMPERATURE_HIGH;
        result.severity_level = 1.0f;
        result.immediate_action_required = true;
        snprintf(result.violation_description, sizeof(result.violation_description),
                "Temperatura crítica durante riego: %.1f°C",
                ambient_data->ambient_temperature);
        snprintf(result.recommended_action, sizeof(result.recommended_action), "PARAR INMEDIATAMENTE");

        safety_manager_log_safety_event(&result);
        return result;
    }

    // 3. VERIFICAR LÍMITES DE DURACIÓN
    if (safety_manager_check_duration_limits(current_status)) {
        result.overall_result = SAFETY_CHECK_CRITICAL;
        result.violation_type = SAFETY_VIOLATION_DURATION_EXCEEDED;
        result.severity_level = 0.9f;
        result.immediate_action_required = true;
        snprintf(result.violation_description, sizeof(result.violation_description),
                "Duración máxima excedida (%d min)", g_critical_limits.max_session_duration_minutes);
        snprintf(result.recommended_action, sizeof(result.recommended_action), "PARAR por timeout");

        safety_manager_log_safety_event(&result);
        return result;
    }

    // 4. TODO NORMAL - CONTINUAR
    snprintf(result.violation_description, sizeof(result.violation_description),
            "Riego en progreso - condiciones normales");
    snprintf(result.recommended_action, sizeof(result.recommended_action), "Continuar");

    g_statistics.total_safety_checks_performed++;
    return result;
}

bool safety_manager_has_emergency_condition(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data)
{
    if (!ambient_data || !soil_data || !g_service_initialized) {
        return true; // Fallo de datos es emergencia
    }

    // Sobre-irrigación: cualquier sensor >80%
    if (safety_manager_check_over_irrigation_protection(soil_data)) {
        return true;
    }

    // Temperatura crítica: >40°C
    if (safety_manager_check_thermal_protection(ambient_data)) {
        return true;
    }

    // Valores de sensores imposibles
    if (soil_data->soil_humidity_1 < 0.0f || soil_data->soil_humidity_1 > 100.0f ||
        soil_data->soil_humidity_2 < 0.0f || soil_data->soil_humidity_2 > 100.0f ||
        soil_data->soil_humidity_3 < 0.0f || soil_data->soil_humidity_3 > 100.0f ||
        ambient_data->ambient_temperature < -50.0f || ambient_data->ambient_temperature > 80.0f ||
        ambient_data->ambient_humidity < 0.0f || ambient_data->ambient_humidity > 100.0f) {
        return true;
    }

    return false;
}

bool safety_manager_check_over_irrigation_protection(const soil_sensor_data_t* soil_data)
{
    if (!soil_data || !g_service_initialized) {
        return true; // Error = emergencia
    }

    // CRÍTICO: Si CUALQUIER sensor supera 80%
    return (soil_data->soil_humidity_1 > g_critical_limits.soil_emergency_threshold_percent ||
            soil_data->soil_humidity_2 > g_critical_limits.soil_emergency_threshold_percent ||
            soil_data->soil_humidity_3 > g_critical_limits.soil_emergency_threshold_percent);
}

bool safety_manager_check_thermal_protection(const ambient_sensor_data_t* ambient_data)
{
    if (!ambient_data || !g_service_initialized) {
        return true; // Error = emergencia
    }

    return (ambient_data->ambient_temperature > g_critical_limits.temperature_emergency_celsius);
}

bool safety_manager_check_duration_limits(const irrigation_status_t* current_status)
{
    if (!current_status || !g_service_initialized) {
        return true; // Error = emergencia
    }

    // Sistema de una válvula - verificar solo válvula 1
    if (irrigation_status_is_valve_active(current_status, 1)) {
        return irrigation_status_is_valve_timeout(current_status, 1);
    }

    return false; // No hay válvulas activas
}

bool safety_manager_check_cooldown_period(
    const safety_evaluation_context_t* context,
    uint8_t valve_number)
{
    if (!context || !g_service_initialized || valve_number != 1) {
        return false; // Sistema de una válvula
    }

    // Verificar si ha pasado suficiente tiempo desde la última sesión
    uint32_t current_time = safety_manager_get_current_timestamp();
    uint32_t time_since_last = current_time - context->last_irrigation_end_time;
    uint32_t cooldown_seconds = g_critical_limits.min_cooldown_duration_minutes * 60;

    return (time_since_last < cooldown_seconds);
}

bool safety_manager_check_sensor_health(
    const soil_sensor_data_t* soil_data,
    const ambient_sensor_data_t* ambient_data)
{
    if (!soil_data || !ambient_data || !g_service_initialized) {
        return false; // Datos nulos = sensores no saludables
    }

    // Contar sensores de suelo funcionales
    int working_soil_sensors = 0;

    if (soil_data->soil_humidity_1 >= 0.0f && soil_data->soil_humidity_1 <= 100.0f) {
        working_soil_sensors++;
    }
    if (soil_data->soil_humidity_2 >= 0.0f && soil_data->soil_humidity_2 <= 100.0f) {
        working_soil_sensors++;
    }
    if (soil_data->soil_humidity_3 >= 0.0f && soil_data->soil_humidity_3 <= 100.0f) {
        working_soil_sensors++;
    }

    // Necesitamos al menos 2 de 3 sensores funcionales
    if (working_soil_sensors < g_critical_limits.min_working_soil_sensors) {
        return false;
    }

    // Verificar sensor ambiental
    if (ambient_data->ambient_temperature < -50.0f || ambient_data->ambient_temperature > 80.0f ||
        ambient_data->ambient_humidity < 0.0f || ambient_data->ambient_humidity > 100.0f) {
        return false;
    }

    // Verificar consistencia entre sensores de suelo
    float avg_soil = (soil_data->soil_humidity_1 + soil_data->soil_humidity_2 +
                     soil_data->soil_humidity_3) / 3.0f;
    float max_deviation = 0.0f;

    max_deviation = fmaxf(max_deviation, fabsf(soil_data->soil_humidity_1 - avg_soil));
    max_deviation = fmaxf(max_deviation, fabsf(soil_data->soil_humidity_2 - avg_soil));
    max_deviation = fmaxf(max_deviation, fabsf(soil_data->soil_humidity_3 - avg_soil));

    // Si la desviación es >30%, los sensores son inconsistentes
    if (max_deviation > 30.0f) {
        return false;
    }

    return true;
}

bool safety_manager_check_daily_emergency_limits(const safety_evaluation_context_t* context)
{
    if (!context || !g_service_initialized) {
        return false;
    }

    return (context->emergency_stop_count_today >= g_critical_limits.max_emergency_stops_per_day);
}

float safety_manager_calculate_violation_severity(
    safety_violation_type_t violation_type,
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data)
{
    switch (violation_type) {
        case SAFETY_VIOLATION_SOIL_OVER_IRRIGATION:
        case SAFETY_VIOLATION_TEMPERATURE_HIGH:
            return 1.0f; // Máxima severidad

        case SAFETY_VIOLATION_DURATION_EXCEEDED:
        case SAFETY_VIOLATION_SENSOR_FAILURE:
            return 0.9f; // Muy severo

        case SAFETY_VIOLATION_COOLDOWN_VIOLATED:
        case SAFETY_VIOLATION_VALVE_MALFUNCTION:
            return 0.7f; // Severo

        case SAFETY_VIOLATION_SYSTEM_OVERLOAD:
            return 0.6f; // Moderado

        case SAFETY_VIOLATION_MULTIPLE_VIOLATIONS:
            return 0.4f; // Advertencia

        case SAFETY_VIOLATION_NONE:
        default:
            return 0.0f;
    }
}

void safety_manager_generate_violation_description(
    safety_violation_type_t violation_type,
    const soil_sensor_data_t* soil_data,
    char* description_buffer,
    size_t buffer_size)
{
    if (!description_buffer || buffer_size == 0) {
        return;
    }

    switch (violation_type) {
        case SAFETY_VIOLATION_SOIL_OVER_IRRIGATION:
            if (soil_data) {
                snprintf(description_buffer, buffer_size,
                        "SOBRE-IRRIGACIÓN: Sensores %.1f%%, %.1f%%, %.1f%% (>%.1f%%)",
                        soil_data->soil_humidity_1, soil_data->soil_humidity_2,
                        soil_data->soil_humidity_3, g_critical_limits.soil_emergency_threshold_percent);
            } else {
                snprintf(description_buffer, buffer_size, "SOBRE-IRRIGACIÓN detectada");
            }
            break;

        case SAFETY_VIOLATION_TEMPERATURE_HIGH:
            snprintf(description_buffer, buffer_size,
                    "TEMPERATURA CRÍTICA >%.1f°C", g_critical_limits.temperature_emergency_celsius);
            break;

        case SAFETY_VIOLATION_DURATION_EXCEEDED:
            snprintf(description_buffer, buffer_size,
                    "DURACIÓN EXCEDIDA >%d minutos", g_critical_limits.max_session_duration_minutes);
            break;

        case SAFETY_VIOLATION_SENSOR_FAILURE:
            snprintf(description_buffer, buffer_size, "FALLO CRÍTICO DE SENSORES");
            break;

        case SAFETY_VIOLATION_COOLDOWN_VIOLATED:
            snprintf(description_buffer, buffer_size,
                    "COOLDOWN violado <%dh", g_critical_limits.min_cooldown_duration_minutes / 60);
            break;

        default:
            snprintf(description_buffer, buffer_size, "Violación de seguridad detectada");
            break;
    }
}

void safety_manager_generate_recommended_action(
    safety_violation_type_t violation_type,
    float severity_level,
    char* action_buffer,
    size_t buffer_size)
{
    if (!action_buffer || buffer_size == 0) {
        return;
    }

    if (severity_level >= 0.9f) {
        snprintf(action_buffer, buffer_size, "PARAR INMEDIATAMENTE");
    } else if (severity_level >= 0.7f) {
        snprintf(action_buffer, buffer_size, "PARAR riego");
    } else if (severity_level >= 0.5f) {
        snprintf(action_buffer, buffer_size, "BLOQUEAR operación");
    } else if (severity_level >= 0.3f) {
        snprintf(action_buffer, buffer_size, "MONITOREAR de cerca");
    } else {
        snprintf(action_buffer, buffer_size, "Continuar con precaución");
    }
}

void safety_manager_log_safety_event(const safety_evaluation_result_t* result)
{
    if (!result || !g_service_initialized) {
        return;
    }

    // Actualizar estadísticas
    g_statistics.total_safety_checks_performed++;

    switch (result->overall_result) {
        case SAFETY_CHECK_WARNING:
            g_statistics.warnings_issued_today++;
            break;
        case SAFETY_CHECK_CRITICAL:
            g_statistics.critical_alerts_today++;
            break;
        case SAFETY_CHECK_EMERGENCY:
            g_statistics.emergency_stops_today++;
            g_statistics.last_emergency_timestamp = result->evaluation_timestamp;
            strncpy(g_statistics.last_emergency_reason, result->violation_description,
                   sizeof(g_statistics.last_emergency_reason) - 1);
            break;
        case SAFETY_CHECK_BLOCKED:
            g_statistics.operations_blocked_today++;
            break;
        default:
            break;
    }

    // Recalcular puntuación de seguridad
    g_statistics.system_safety_score = safety_manager_calculate_system_safety_score();
}

void safety_manager_record_emergency_stop(const char* reason)
{
    if (!g_service_initialized) {
        return;
    }

    g_statistics.emergency_stops_today++;
    g_statistics.last_emergency_timestamp = safety_manager_get_current_timestamp();

    if (reason) {
        strncpy(g_statistics.last_emergency_reason, reason,
               sizeof(g_statistics.last_emergency_reason) - 1);
        g_statistics.last_emergency_reason[sizeof(g_statistics.last_emergency_reason) - 1] = '\0';
    }

    // Recalcular puntuación de seguridad
    g_statistics.system_safety_score = safety_manager_calculate_system_safety_score();
}

float safety_manager_calculate_system_safety_score(void)
{
    if (!g_service_initialized) {
        return 0.0f;
    }

    float score = 1.0f; // Empezar con puntuación perfecta

    // Penalizar emergencias (cada una reduce 10%)
    score -= (g_statistics.emergency_stops_today * 0.1f);

    // Penalizar alertas críticas (cada una reduce 5%)
    score -= (g_statistics.critical_alerts_today * 0.05f);

    // Penalizar advertencias (cada una reduce 2%)
    score -= (g_statistics.warnings_issued_today * 0.02f);

    // Penalizar operaciones bloqueadas (cada una reduce 1%)
    score -= (g_statistics.operations_blocked_today * 0.01f);

    // Bonus por checks exitosos (hasta +10%)
    if (g_statistics.total_safety_checks_performed > 0) {
        float success_rate = 1.0f - ((float)(g_statistics.warnings_issued_today +
                                            g_statistics.critical_alerts_today +
                                            g_statistics.emergency_stops_today) /
                                   g_statistics.total_safety_checks_performed);
        score += (success_rate * 0.1f);
    }

    // Limitar a rango válido
    if (score > 1.0f) score = 1.0f;
    if (score < 0.0f) score = 0.0f;

    return score;
}

void safety_manager_get_statistics(safety_statistics_t* stats)
{
    if (stats && g_service_initialized) {
        memcpy(stats, &g_statistics, sizeof(safety_statistics_t));
    }
}

void safety_manager_reset_daily_statistics(void)
{
    if (!g_service_initialized) {
        return;
    }

    g_statistics.warnings_issued_today = 0;
    g_statistics.critical_alerts_today = 0;
    g_statistics.emergency_stops_today = 0;
    g_statistics.operations_blocked_today = 0;

    // Recalcular puntuación después del reset
    g_statistics.system_safety_score = safety_manager_calculate_system_safety_score();
}

void safety_manager_get_critical_limits(safety_critical_limits_t* limits)
{
    if (limits && g_service_initialized) {
        memcpy(limits, &g_critical_limits, sizeof(safety_critical_limits_t));
    }
}

esp_err_t safety_manager_set_critical_limits(const safety_critical_limits_t* limits)
{
    if (!limits || !g_service_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    // Validar límites básicos
    if (limits->soil_emergency_threshold_percent <= 0.0f ||
        limits->soil_emergency_threshold_percent > 100.0f ||
        limits->temperature_emergency_celsius <= 0.0f ||
        limits->max_session_duration_minutes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&g_critical_limits, limits, sizeof(safety_critical_limits_t));
    return ESP_OK;
}

esp_err_t safety_manager_set_override_mode(bool override_active, const char* reason)
{
    if (!g_service_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    g_override_active = override_active;

    if (reason) {
        strncpy(g_override_reason, reason, sizeof(g_override_reason) - 1);
        g_override_reason[sizeof(g_override_reason) - 1] = '\0';
    } else {
        g_override_reason[0] = '\0';
    }

    return ESP_OK;
}

bool safety_manager_is_override_active(void)
{
    return g_override_active && g_service_initialized;
}

const char* safety_manager_check_result_to_string(safety_check_result_t result)
{
    switch (result) {
        case SAFETY_CHECK_OK:
            return "ok";
        case SAFETY_CHECK_WARNING:
            return "warning";
        case SAFETY_CHECK_CRITICAL:
            return "critical";
        case SAFETY_CHECK_EMERGENCY:
            return "emergency";
        case SAFETY_CHECK_BLOCKED:
            return "blocked";
        default:
            return "unknown";
    }
}

const char* safety_manager_violation_type_to_string(safety_violation_type_t violation_type)
{
    switch (violation_type) {
        case SAFETY_VIOLATION_NONE:
            return "none";
        case SAFETY_VIOLATION_SOIL_OVER_IRRIGATION:
            return "soil_over_irrigation";
        case SAFETY_VIOLATION_TEMPERATURE_HIGH:
            return "temperature_high";
        case SAFETY_VIOLATION_DURATION_EXCEEDED:
            return "duration_exceeded";
        case SAFETY_VIOLATION_COOLDOWN_VIOLATED:
            return "cooldown_violated";
        case SAFETY_VIOLATION_SENSOR_FAILURE:
            return "sensor_failure";
        case SAFETY_VIOLATION_VALVE_MALFUNCTION:
            return "valve_malfunction";
        case SAFETY_VIOLATION_MULTIPLE_VIOLATIONS:
            return "multiple_violations";
        case SAFETY_VIOLATION_SYSTEM_OVERLOAD:
            return "system_overload";
        default:
            return "unknown";
    }
}

uint32_t safety_manager_get_current_timestamp(void)
{
    return (uint32_t)time(NULL);
}