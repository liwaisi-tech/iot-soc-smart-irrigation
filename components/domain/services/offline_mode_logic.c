#include "offline_mode_logic.h"
#include <string.h>
#include <math.h>
#include <time.h>

/**
 * @file offline_mode_logic.c
 * @brief Implementación del servicio de lógica de modo offline
 *
 * Implementa algoritmos de riego autónomo para operación sin conectividad
 * optimizado para zonas rurales de Colombia con sistema de una válvula.
 */

// Variables estáticas para configuración y estado
static safety_limits_t g_safety_limits;
static offline_transition_config_t g_transition_config;
static offline_statistics_t g_statistics;
static bool g_service_initialized = false;
// static uint32_t g_offline_activation_timestamp = 0; // Reservado para uso futuro

// Configuración por defecto de transiciones offline
static const offline_transition_config_t DEFAULT_TRANSITION_CONFIG = {
    // NORMAL → WARNING (condiciones deteriorándose)
    .soil_humidity_normal_to_warning = 50.0f,     // 50% umbral humedad
    .temperature_normal_to_warning = 30.0f,       // 30°C umbral temperatura
    .time_normal_to_warning_s = 14400,            // 4h umbral temporal

    // WARNING → CRITICAL (situación preocupante)
    .soil_humidity_warning_to_critical = 40.0f,   // 40% umbral humedad
    .temperature_warning_to_critical = 32.0f,     // 32°C umbral temperatura
    .time_warning_to_critical_s = 7200,           // 2h umbral temporal

    // CRITICAL → EMERGENCY (supervivencia cultivo)
    .soil_humidity_critical_to_emergency = 30.0f, // 30% umbral humedad
    .temperature_critical_to_emergency = 35.0f,   // 35°C umbral temperatura
    .time_critical_to_emergency_s = 3600,         // 1h umbral temporal

    // Thresholds para mejora (downgrade)
    .soil_humidity_improvement = 60.0f,           // 60% umbral mejora
    .temperature_improvement = 28.0f,             // 28°C umbral mejora
    .stable_time_for_downgrade_s = 3600           // 1h tiempo estable
};

esp_err_t offline_mode_logic_init(const safety_limits_t* safety_limits)
{
    if (!safety_limits) {
        return ESP_ERR_INVALID_ARG;
    }

    // Copiar límites de seguridad
    memcpy(&g_safety_limits, safety_limits, sizeof(safety_limits_t));

    // Configurar valores por defecto
    memcpy(&g_transition_config, &DEFAULT_TRANSITION_CONFIG, sizeof(offline_transition_config_t));

    // Inicializar estadísticas
    memset(&g_statistics, 0, sizeof(offline_statistics_t));
    g_statistics.last_online_timestamp = offline_mode_logic_get_current_timestamp();

    g_service_initialized = true;

    return ESP_OK;
}

bool offline_mode_logic_should_activate_offline(
    bool wifi_connected,
    bool mqtt_connected,
    uint32_t last_communication_timestamp,
    power_mode_t power_mode)
{
    if (!g_service_initialized) {
        return true; // Si el servicio no está inicializado, modo seguro offline
    }

    uint32_t current_time = offline_mode_logic_get_current_timestamp();
    uint32_t time_since_communication = current_time - last_communication_timestamp;

    // 1. Modo de energía crítica/emergencia - forzar offline para conservar batería
    if (power_mode == POWER_MODE_CRITICAL || power_mode == POWER_MODE_EMERGENCY) {
        return true;
    }

    // 2. Sin WiFi por más de 5 minutos (300 segundos)
    if (!wifi_connected && time_since_communication > CONNECTIVITY_WIFI_TIMEOUT_S) {
        return true;
    }

    // 3. Sin MQTT por más de 3 minutos (180 segundos), pero con WiFi
    if (wifi_connected && !mqtt_connected && time_since_communication > CONNECTIVITY_MQTT_TIMEOUT_S) {
        return true;
    }

    // 4. Sin comunicación exitosa por más de 10 minutos (independiente del estado reported)
    if (time_since_communication > 600) { // 10 minutos
        return true;
    }

    return false; // Condiciones normales - mantener online
}

offline_evaluation_result_t offline_mode_logic_evaluate_mode(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    offline_mode_t current_mode,
    const offline_evaluation_context_t* context)
{
    offline_evaluation_result_t result = {0};
    result.recommended_mode = current_mode; // Por defecto mantener modo actual
    result.next_evaluation_timestamp = offline_mode_logic_get_current_timestamp() + 3600; // 1h por defecto

    // Validar parámetros de entrada
    if (!ambient_data || !soil_data || !context || !g_service_initialized) {
        result.recommended_mode = OFFLINE_MODE_EMERGENCY;
        result.reason = MODE_CHANGE_SENSOR_FAILURE;
        result.recommended_interval_s = OFFLINE_MODE_EMERGENCY_INTERVAL_S;
        result.urgency_level = 1.0f;
        snprintf(result.explanation, sizeof(result.explanation),
                "Fallo de datos - modo emergencia por seguridad");
        result.next_evaluation_timestamp = offline_mode_logic_get_current_timestamp() + 900; // 15min
        return result;
    }

    // Calcular métricas clave
    float avg_soil_humidity = (soil_data->soil_humidity_1 +
                              soil_data->soil_humidity_2 +
                              soil_data->soil_humidity_3) / 3.0f;

    float min_soil_humidity = fminf(fminf(soil_data->soil_humidity_1, soil_data->soil_humidity_2),
                                   soil_data->soil_humidity_3);

    // float soil_criticality = offline_mode_logic_calculate_soil_criticality(soil_data); // Reservado para lógica futura

    // Evaluar transiciones basadas en condiciones actuales
    offline_mode_t new_mode = current_mode;
    offline_mode_change_reason_t change_reason = MODE_CHANGE_TIME_THRESHOLD;

    // ESCALAMIENTO: Condiciones empeorando

    // 1. EMERGENCY: Condiciones críticas de supervivencia
    if (min_soil_humidity <= g_transition_config.soil_humidity_critical_to_emergency ||
        ambient_data->ambient_temperature >= g_transition_config.temperature_critical_to_emergency ||
        context->battery_level_percent <= 10) { // Batería crítica

        new_mode = OFFLINE_MODE_EMERGENCY;
        change_reason = MODE_CHANGE_SOIL_CRITICAL;
        result.recommended_interval_s = OFFLINE_MODE_EMERGENCY_INTERVAL_S;
        result.urgency_level = 1.0f;
        snprintf(result.explanation, sizeof(result.explanation),
                "EMERGENCIA: Suelo %.1f%%, Temp %.1f°C - supervivencia cultivo",
                min_soil_humidity, ambient_data->ambient_temperature);
    }
    // 2. CRITICAL: Condiciones críticas
    else if (min_soil_humidity <= g_transition_config.soil_humidity_warning_to_critical ||
             ambient_data->ambient_temperature >= g_transition_config.temperature_warning_to_critical ||
             context->battery_level_percent <= 20) {

        new_mode = OFFLINE_MODE_CRITICAL;
        change_reason = MODE_CHANGE_SOIL_CRITICAL;
        result.recommended_interval_s = OFFLINE_MODE_CRITICAL_INTERVAL_S;
        result.urgency_level = 0.8f;
        snprintf(result.explanation, sizeof(result.explanation),
                "CRÍTICO: Suelo %.1f%%, Temp %.1f°C - acción requerida",
                min_soil_humidity, ambient_data->ambient_temperature);
    }
    // 3. WARNING: Condiciones deteriorándose
    else if (avg_soil_humidity <= g_transition_config.soil_humidity_normal_to_warning ||
             ambient_data->ambient_temperature >= g_transition_config.temperature_normal_to_warning ||
             context->time_offline_seconds >= g_transition_config.time_normal_to_warning_s) {

        new_mode = OFFLINE_MODE_WARNING;
        change_reason = (ambient_data->ambient_temperature >= g_transition_config.temperature_normal_to_warning) ?
                       MODE_CHANGE_TEMPERATURE_HIGH : MODE_CHANGE_SOIL_CRITICAL;
        result.recommended_interval_s = OFFLINE_MODE_WARNING_INTERVAL_S;
        result.urgency_level = 0.6f;
        snprintf(result.explanation, sizeof(result.explanation),
                "ADVERTENCIA: Suelo %.1f%%, Temp %.1f°C - monitorear",
                avg_soil_humidity, ambient_data->ambient_temperature);
    }

    // MEJORAMIENTO: Condiciones mejorando (downgrade)
    else if (current_mode > OFFLINE_MODE_NORMAL &&
             avg_soil_humidity >= g_transition_config.soil_humidity_improvement &&
             ambient_data->ambient_temperature <= g_transition_config.temperature_improvement) {

        // Solo mejorar si las condiciones han estado estables por el tiempo requerido
        // Simplificado: si estamos mejor que los umbrales, podemos mejorar
        if (current_mode == OFFLINE_MODE_EMERGENCY) {
            new_mode = OFFLINE_MODE_CRITICAL;
        } else if (current_mode == OFFLINE_MODE_CRITICAL) {
            new_mode = OFFLINE_MODE_WARNING;
        } else if (current_mode == OFFLINE_MODE_WARNING) {
            new_mode = OFFLINE_MODE_NORMAL;
        }

        change_reason = MODE_CHANGE_TIME_THRESHOLD;
        result.urgency_level = 0.3f;
        snprintf(result.explanation, sizeof(result.explanation),
                "MEJORA: Condiciones estables - reducir frecuencia");
    }
    // 4. NORMAL: Condiciones estables
    else {
        new_mode = OFFLINE_MODE_NORMAL;
        change_reason = MODE_CHANGE_TIME_THRESHOLD;
        result.recommended_interval_s = OFFLINE_MODE_NORMAL_INTERVAL_S;
        result.urgency_level = 0.2f;
        snprintf(result.explanation, sizeof(result.explanation),
                "NORMAL: Condiciones estables - modo conservación energía");
    }

    // Ajustar intervalo basado en modo de energía
    switch (context->current_power_mode) {
        case POWER_MODE_CRITICAL:
        case POWER_MODE_EMERGENCY:
            result.recommended_interval_s *= 2; // Duplicar intervalo para conservar batería
            break;
        case POWER_MODE_SAVING:
            result.recommended_interval_s = (result.recommended_interval_s * 3) / 2; // +50%
            break;
        case POWER_MODE_CHARGING:
            result.recommended_interval_s = (result.recommended_interval_s * 3) / 4; // -25%
            break;
        case POWER_MODE_NORMAL:
        default:
            // Sin cambios
            break;
    }

    // Configurar resultado final
    result.recommended_mode = new_mode;
    result.reason = change_reason;
    result.next_evaluation_timestamp = offline_mode_logic_get_current_timestamp() + result.recommended_interval_s;

    return result;
}

bool offline_mode_logic_should_start_offline_irrigation(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const irrigation_status_t* current_status,
    offline_mode_t offline_mode)
{
    if (!ambient_data || !soil_data || !current_status || !g_service_initialized) {
        return false; // Sin datos válidos no activar riego
    }

    // Si ya hay riego activo, no iniciar otro (sistema de una válvula)
    if (irrigation_status_is_valve_active(current_status, 1)) {
        return false;
    }

    // Si sistema en emergencia, no activar riego
    if (current_status->emergency_stop_active) {
        return false;
    }

    // Calcular humedad mínima de suelo
    float min_soil_humidity = fminf(fminf(soil_data->soil_humidity_1, soil_data->soil_humidity_2),
                                   soil_data->soil_humidity_3);

    float avg_soil_humidity = (soil_data->soil_humidity_1 +
                              soil_data->soil_humidity_2 +
                              soil_data->soil_humidity_3) / 3.0f;

    // Decisión basada en modo offline y condiciones
    switch (offline_mode) {
        case OFFLINE_MODE_EMERGENCY:
            // Riego de supervivencia - activar si cualquier sensor <30%
            return (min_soil_humidity < g_safety_limits.soil_humidity.critical_low_percent);

        case OFFLINE_MODE_CRITICAL:
            // Riego crítico - activar si promedio <35%
            return (avg_soil_humidity < ((g_safety_limits.soil_humidity.critical_low_percent +
                                         g_safety_limits.soil_humidity.warning_low_percent) / 2.0f));

        case OFFLINE_MODE_WARNING:
            // Riego preventivo - activar si promedio <45%
            return (avg_soil_humidity < g_safety_limits.soil_humidity.warning_low_percent);

        case OFFLINE_MODE_NORMAL:
            // Riego conservador - activar si promedio <40% y no hay restricciones térmicas
            if (avg_soil_humidity >= 40.0f) {
                return false;
            }

            // Verificar restricciones térmicas para riego normal
            if (ambient_data->ambient_temperature > 35.0f) { // No regar en calor extremo
                return false;
            }

            return true;

        case OFFLINE_MODE_DISABLED:
        default:
            return false;
    }
}

uint16_t offline_mode_logic_calculate_evaluation_interval(
    offline_mode_t offline_mode,
    uint8_t battery_level,
    float soil_criticality)
{
    uint16_t base_interval;

    // Intervalo base según modo offline
    switch (offline_mode) {
        case OFFLINE_MODE_EMERGENCY:
            base_interval = OFFLINE_MODE_EMERGENCY_INTERVAL_S;
            break;
        case OFFLINE_MODE_CRITICAL:
            base_interval = OFFLINE_MODE_CRITICAL_INTERVAL_S;
            break;
        case OFFLINE_MODE_WARNING:
            base_interval = OFFLINE_MODE_WARNING_INTERVAL_S;
            break;
        case OFFLINE_MODE_NORMAL:
            base_interval = OFFLINE_MODE_NORMAL_INTERVAL_S;
            break;
        default:
            base_interval = OFFLINE_MODE_NORMAL_INTERVAL_S;
            break;
    }

    // Ajuste por nivel de batería
    if (battery_level <= 10) {
        base_interval *= 3; // Triplicar intervalo con batería crítica
    } else if (battery_level <= 20) {
        base_interval *= 2; // Duplicar con batería baja
    } else if (battery_level <= 50) {
        base_interval = (base_interval * 3) / 2; // +50% con batería media
    }
    // Con batería >50% usar intervalo normal

    // Ajuste por criticidad del suelo
    if (soil_criticality >= 0.8f) {
        base_interval = base_interval / 2; // Reducir a la mitad si muy crítico
    } else if (soil_criticality >= 0.6f) {
        base_interval = (base_interval * 3) / 4; // -25% si moderadamente crítico
    }
    // Con criticidad baja usar intervalo calculado

    // Límites de seguridad
    if (base_interval < 900) base_interval = 900;     // Mín 15 minutos
    if (base_interval > 14400) base_interval = 14400; // Máx 4 horas

    return base_interval;
}

float offline_mode_logic_analyze_trend(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const ambient_sensor_data_t* previous_ambient,
    const soil_sensor_data_t* previous_soil)
{
    if (!ambient_data || !soil_data || !previous_ambient || !previous_soil) {
        return 0.0f; // Sin datos previos, tendencia neutral
    }

    float trend_score = 0.0f;
    float factors = 0.0f;

    // Factor 1: Tendencia de humedad del suelo (peso: 50%)
    float current_avg_soil = (soil_data->soil_humidity_1 + soil_data->soil_humidity_2 +
                             soil_data->soil_humidity_3) / 3.0f;
    float previous_avg_soil = (previous_soil->soil_humidity_1 + previous_soil->soil_humidity_2 +
                              previous_soil->soil_humidity_3) / 3.0f;

    float soil_change = current_avg_soil - previous_avg_soil;
    trend_score += (soil_change / 100.0f) * 0.5f; // Normalizar y ponderar 50%
    factors += 0.5f;

    // Factor 2: Tendencia de temperatura (peso: 30%)
    float temp_change = previous_ambient->ambient_temperature - ambient_data->ambient_temperature; // Invertir: temp baja = mejor
    trend_score += (temp_change / 40.0f) * 0.3f; // Normalizar para rango ±40°C y ponderar 30%
    factors += 0.3f;

    // Factor 3: Tendencia de humedad ambiente (peso: 20%)
    float humidity_change = ambient_data->ambient_humidity - previous_ambient->ambient_humidity;
    trend_score += (humidity_change / 100.0f) * 0.2f; // Normalizar y ponderar 20%
    factors += 0.2f;

    // Normalizar por factores totales
    if (factors > 0.0f) {
        trend_score /= factors;
    }

    // Limitar a rango válido
    if (trend_score > 1.0f) trend_score = 1.0f;
    if (trend_score < -1.0f) trend_score = -1.0f;

    return trend_score;
}

float offline_mode_logic_calculate_soil_criticality(const soil_sensor_data_t* soil_data)
{
    if (!soil_data || !g_service_initialized) {
        return 1.0f; // Sin datos = máxima criticidad
    }

    // Encontrar la humedad mínima (sensor más crítico)
    float min_humidity = fminf(fminf(soil_data->soil_humidity_1, soil_data->soil_humidity_2),
                              soil_data->soil_humidity_3);

    // Calcular criticidad basada en umbrales del sistema
    float critical_threshold = g_safety_limits.soil_humidity.critical_low_percent;  // 30%
    float optimal_threshold = g_safety_limits.soil_humidity.target_max_percent;     // 75%

    if (min_humidity <= critical_threshold) {
        return 1.0f; // Máxima criticidad
    } else if (min_humidity >= optimal_threshold) {
        return 0.0f; // Sin criticidad
    } else {
        // Interpolación lineal entre crítico y óptimo
        float range = optimal_threshold - critical_threshold;
        float position = min_humidity - critical_threshold;
        return 1.0f - (position / range);
    }
}

uint16_t offline_mode_logic_estimate_power_consumption(
    offline_mode_t offline_mode,
    uint16_t interval_s)
{
    // Estimación basada en perfil de consumo ESP32
    uint16_t base_consumption_mah = 0;

    // Consumo base por evaluación (despertar, sensores, procesamiento, dormir)
    const uint16_t WAKEUP_CONSUMPTION_MAH = 2;    // 2mAh por despertar+sensores+cálculos
    const uint16_t VALVE_OPERATION_MAH = 50;      // 50mAh por operación de válvula

    // Consumo base por evaluación
    base_consumption_mah += WAKEUP_CONSUMPTION_MAH;

    // Consumo adicional según modo offline (más evaluaciones = más consumo)
    switch (offline_mode) {
        case OFFLINE_MODE_EMERGENCY:
            base_consumption_mah += 1; // Evaluaciones más frecuentes
            break;
        case OFFLINE_MODE_CRITICAL:
            base_consumption_mah += 1;
            break;
        case OFFLINE_MODE_WARNING:
            // Consumo base solamente
            break;
        case OFFLINE_MODE_NORMAL:
            base_consumption_mah -= 1; // Ligeramente menos por optimizaciones
            break;
        default:
            break;
    }

    // Estimar probabilidad de activación de riego según modo
    float irrigation_probability = 0.0f;
    switch (offline_mode) {
        case OFFLINE_MODE_EMERGENCY:
            irrigation_probability = 0.8f; // 80% probabilidad
            break;
        case OFFLINE_MODE_CRITICAL:
            irrigation_probability = 0.6f; // 60% probabilidad
            break;
        case OFFLINE_MODE_WARNING:
            irrigation_probability = 0.3f; // 30% probabilidad
            break;
        case OFFLINE_MODE_NORMAL:
            irrigation_probability = 0.1f; // 10% probabilidad
            break;
        default:
            irrigation_probability = 0.0f;
            break;
    }

    // Agregar consumo estimado por operaciones de riego
    base_consumption_mah += (uint16_t)(VALVE_OPERATION_MAH * irrigation_probability);

    // Convertir a consumo por hora
    float evaluations_per_hour = 3600.0f / interval_s;
    uint16_t hourly_consumption = (uint16_t)(base_consumption_mah * evaluations_per_hour);

    return hourly_consumption;
}

offline_activation_trigger_t offline_mode_logic_determine_activation_trigger(
    bool wifi_connected,
    bool mqtt_connected,
    uint32_t communication_timeout_s,
    power_mode_t power_mode)
{
    // Determinar trigger principal basado en condiciones

    // 1. Modo de energía crítico
    if (power_mode == POWER_MODE_CRITICAL || power_mode == POWER_MODE_EMERGENCY) {
        return OFFLINE_TRIGGER_POWER_SAVING;
    }

    // 2. Timeout de WiFi (más crítico)
    if (!wifi_connected && communication_timeout_s >= CONNECTIVITY_WIFI_TIMEOUT_S) {
        return OFFLINE_TRIGGER_WIFI_TIMEOUT;
    }

    // 3. Timeout de MQTT (con WiFi disponible)
    if (wifi_connected && !mqtt_connected && communication_timeout_s >= CONNECTIVITY_MQTT_TIMEOUT_S) {
        return OFFLINE_TRIGGER_MQTT_TIMEOUT;
    }

    // 4. Red inestable (conexiones frecuentes perdidas)
    if ((!wifi_connected || !mqtt_connected) && communication_timeout_s > 60) {
        return OFFLINE_TRIGGER_NETWORK_UNRELIABLE;
    }

    // 5. Timeout general de comunicación
    if (communication_timeout_s > 600) { // 10 minutos sin comunicación
        return OFFLINE_TRIGGER_SYSTEM_ERROR;
    }

    // No debería llegar aquí si la lógica es correcta
    return OFFLINE_TRIGGER_SYSTEM_ERROR;
}

bool offline_mode_logic_should_return_online(
    bool wifi_connected,
    bool mqtt_connected,
    const offline_evaluation_context_t* context)
{
    if (!context || !g_service_initialized) {
        return false;
    }

    // Solo considerar retorno si ambos WiFi y MQTT están conectados
    if (!wifi_connected || !mqtt_connected) {
        return false;
    }

    // Si llevamos poco tiempo offline (<5 min), esperar estabilidad
    if (context->time_offline_seconds < 300) {
        return false;
    }

    // Verificar que la conexión sea estable (por implementar con más datos históricos)
    // Por ahora, si tenemos conectividad completa, intentar retornar

    // En modo de ahorro energético, ser más conservador
    if (context->current_power_mode == POWER_MODE_CRITICAL ||
        context->current_power_mode == POWER_MODE_EMERGENCY) {

        // Solo retornar si llevamos mucho tiempo offline (>1 hora) y batería mejora
        return (context->time_offline_seconds > 3600 && context->battery_level_percent > 30);
    }

    return true; // Conectividad disponible - intentar retornar
}

void offline_mode_logic_generate_explanation(
    const offline_evaluation_result_t* result,
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    char* explanation_buffer,
    size_t buffer_size)
{
    if (!result || !explanation_buffer || buffer_size == 0) {
        return;
    }

    // Usar la explicación ya generada en el resultado
    if (strlen(result->explanation) > 0) {
        strncpy(explanation_buffer, result->explanation, buffer_size - 1);
        explanation_buffer[buffer_size - 1] = '\0';
        return;
    }

    // Generar explicación por defecto si no existe
    float avg_soil = 0.0f;
    if (soil_data) {
        avg_soil = (soil_data->soil_humidity_1 + soil_data->soil_humidity_2 +
                   soil_data->soil_humidity_3) / 3.0f;
    }

    snprintf(explanation_buffer, buffer_size,
            "Modo: %s, Suelo: %.1f%%, Temp: %.1f°C, Urgencia: %.1f%%",
            system_mode_offline_to_string(result->recommended_mode),
            avg_soil,
            ambient_data ? ambient_data->ambient_temperature : 0.0f,
            result->urgency_level * 100.0f);
}

void offline_mode_logic_get_transition_config(offline_transition_config_t* config)
{
    if (config && g_service_initialized) {
        memcpy(config, &g_transition_config, sizeof(offline_transition_config_t));
    }
}

esp_err_t offline_mode_logic_set_transition_config(const offline_transition_config_t* config)
{
    if (!config || !g_service_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    // Validación básica de configuración
    if (config->soil_humidity_normal_to_warning <= 0.0f ||
        config->soil_humidity_normal_to_warning > 100.0f ||
        config->temperature_normal_to_warning <= 0.0f ||
        config->time_normal_to_warning_s == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&g_transition_config, config, sizeof(offline_transition_config_t));
    return ESP_OK;
}

void offline_mode_logic_log_event(
    const char* event_type,
    offline_mode_t old_mode,
    offline_mode_t new_mode,
    const char* reason)
{
    if (!g_service_initialized) {
        return;
    }

    // Actualizar estadísticas
    if (old_mode != new_mode) {
        g_statistics.mode_changes_count++;

        if (new_mode == OFFLINE_MODE_EMERGENCY) {
            g_statistics.emergency_activations++;
        }
    }

    // En una implementación completa, aquí se enviarían logs al sistema de logging
    // Por ahora solo actualizamos estadísticas internas
}

void offline_mode_logic_get_statistics(offline_statistics_t* stats)
{
    if (stats && g_service_initialized) {
        memcpy(stats, &g_statistics, sizeof(offline_statistics_t));
    }
}

void offline_mode_logic_reset_statistics(void)
{
    if (!g_service_initialized) {
        return;
    }

    // Preservar algunos valores importantes
    uint32_t last_online = g_statistics.last_online_timestamp;
    uint32_t longest_period = g_statistics.longest_offline_period_s;

    // Resetear estadísticas
    memset(&g_statistics, 0, sizeof(offline_statistics_t));

    // Restaurar valores importantes
    g_statistics.last_online_timestamp = last_online;
    g_statistics.longest_offline_period_s = longest_period;
}

const char* offline_mode_logic_activation_trigger_to_string(offline_activation_trigger_t trigger)
{
    switch (trigger) {
        case OFFLINE_TRIGGER_WIFI_TIMEOUT:
            return "wifi_timeout";
        case OFFLINE_TRIGGER_MQTT_TIMEOUT:
            return "mqtt_timeout";
        case OFFLINE_TRIGGER_NETWORK_UNRELIABLE:
            return "network_unreliable";
        case OFFLINE_TRIGGER_POWER_SAVING:
            return "power_saving";
        case OFFLINE_TRIGGER_MANUAL_OVERRIDE:
            return "manual_override";
        case OFFLINE_TRIGGER_SYSTEM_ERROR:
            return "system_error";
        default:
            return "unknown";
    }
}

const char* offline_mode_logic_change_reason_to_string(offline_mode_change_reason_t reason)
{
    switch (reason) {
        case MODE_CHANGE_SOIL_CRITICAL:
            return "soil_critical";
        case MODE_CHANGE_TEMPERATURE_HIGH:
            return "temperature_high";
        case MODE_CHANGE_TREND_DETERIORATING:
            return "trend_deteriorating";
        case MODE_CHANGE_BATTERY_LOW:
            return "battery_low";
        case MODE_CHANGE_TIME_THRESHOLD:
            return "time_threshold";
        case MODE_CHANGE_SENSOR_FAILURE:
            return "sensor_failure";
        default:
            return "unknown";
    }
}

uint32_t offline_mode_logic_get_current_timestamp(void)
{
    return (uint32_t)time(NULL);
}