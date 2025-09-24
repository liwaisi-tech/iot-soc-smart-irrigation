#ifndef DOMAIN_SERVICES_OFFLINE_MODE_LOGIC_H
#define DOMAIN_SERVICES_OFFLINE_MODE_LOGIC_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "system_mode.h"
#include "ambient_sensor_data.h"
#include "soil_sensor_data.h"
#include "irrigation_status.h"
#include "safety_limits.h"

/**
 * @file offline_mode_logic.h
 * @brief Servicio de dominio para lógica de modo offline
 *
 * Implementa algoritmos de riego autónomo para operación sin conectividad,
 * optimizado para preservar batería mientras garantiza supervivencia del cultivo.
 * Diseñado específicamente para despliegue en zonas rurales de Colombia.
 *
 * Especificaciones Técnicas - Offline Mode Logic Service:
 * - Modo adaptativo: frecuencia basada en criticidad del cultivo
 * - Gestión energética: optimización batería + solar
 * - Algoritmos autónomos: sin dependencia de conectividad
 * - Prioridad supervivencia: cultivo sobre eficiencia energética
 * - Transiciones suaves: online ↔ offline sin pérdida de estado
 */

/**
 * @brief Criterios para determinar modo offline
 */
typedef enum {
    OFFLINE_TRIGGER_WIFI_TIMEOUT = 0,        // Timeout WiFi (5 min)
    OFFLINE_TRIGGER_MQTT_TIMEOUT,            // Timeout MQTT (3 min)
    OFFLINE_TRIGGER_NETWORK_UNRELIABLE,      // Red inestable
    OFFLINE_TRIGGER_POWER_SAVING,            // Modo ahorro energético
    OFFLINE_TRIGGER_MANUAL_OVERRIDE,         // Override manual
    OFFLINE_TRIGGER_SYSTEM_ERROR,            // Error de sistema
    OFFLINE_TRIGGER_MAX
} offline_activation_trigger_t;

/**
 * @brief Criterios para cambio de modo offline
 */
typedef enum {
    MODE_CHANGE_SOIL_CRITICAL = 0,           // Humedad suelo crítica (<30%)
    MODE_CHANGE_TEMPERATURE_HIGH,            // Temperatura alta (>32°C)
    MODE_CHANGE_TREND_DETERIORATING,         // Tendencia empeorando
    MODE_CHANGE_BATTERY_LOW,                 // Batería baja (<20%)
    MODE_CHANGE_TIME_THRESHOLD,              // Threshold temporal alcanzado
    MODE_CHANGE_SENSOR_FAILURE,              // Fallo de sensores
    MODE_CHANGE_MAX
} offline_mode_change_reason_t;

/**
 * @brief Resultado de evaluación de modo offline
 */
typedef struct {
    offline_mode_t recommended_mode;         // Modo recomendado
    offline_mode_change_reason_t reason;     // Razón del cambio
    uint16_t recommended_interval_s;         // Intervalo recomendado (segundos)
    float urgency_level;                     // Nivel de urgencia (0.0-1.0)
    char explanation[128];                   // Explicación detallada
    uint32_t next_evaluation_timestamp;      // Cuándo reevaluar
} offline_evaluation_result_t;

/**
 * @brief Contexto para evaluación de modo offline
 */
typedef struct {
    uint32_t time_offline_seconds;           // Tiempo en modo offline
    uint32_t last_irrigation_timestamp;      // Última sesión de riego
    uint32_t connectivity_loss_timestamp;    // Cuándo se perdió conectividad
    offline_activation_trigger_t trigger;   // Por qué se activó offline
    power_mode_t current_power_mode;         // Modo de energía actual
    uint8_t battery_level_percent;           // Nivel de batería (0-100)
    bool solar_charging_active;              // Si está cargando con solar
} offline_evaluation_context_t;

/**
 * @brief Configuración de transiciones de modo offline
 */
typedef struct {
    // Thresholds para cambio NORMAL → WARNING
    float soil_humidity_normal_to_warning;   // 50.0% - Umbral humedad
    float temperature_normal_to_warning;     // 30.0°C - Umbral temperatura
    uint32_t time_normal_to_warning_s;       // 14400s (4h) - Umbral temporal

    // Thresholds para cambio WARNING → CRITICAL
    float soil_humidity_warning_to_critical; // 40.0% - Umbral humedad
    float temperature_warning_to_critical;   // 32.0°C - Umbral temperatura
    uint32_t time_warning_to_critical_s;     // 7200s (2h) - Umbral temporal

    // Thresholds para cambio CRITICAL → EMERGENCY
    float soil_humidity_critical_to_emergency; // 30.0% - Umbral humedad
    float temperature_critical_to_emergency; // 35.0°C - Umbral temperatura
    uint32_t time_critical_to_emergency_s;   // 3600s (1h) - Umbral temporal

    // Thresholds para mejora (downgrade)
    float soil_humidity_improvement;         // 60.0% - Umbral mejora
    float temperature_improvement;           // 28.0°C - Umbral mejora
    uint32_t stable_time_for_downgrade_s;    // 3600s (1h) - Tiempo estable
} offline_transition_config_t;

/**
 * @brief Estadísticas de modo offline
 */
typedef struct {
    uint32_t total_offline_time_s;           // Tiempo total offline
    uint32_t offline_irrigation_sessions;    // Sesiones de riego offline
    uint32_t mode_changes_count;             // Cambios de modo
    uint32_t emergency_activations;          // Activaciones de emergencia
    float average_battery_consumption_offline; // Consumo promedio batería
    uint32_t longest_offline_period_s;       // Período offline más largo
    uint32_t last_online_timestamp;          // Última vez online
} offline_statistics_t;

/**
 * @brief Inicializar servicio de lógica de modo offline
 *
 * @param safety_limits Límites de seguridad del sistema
 * @return ESP_OK en éxito
 */
esp_err_t offline_mode_logic_init(const safety_limits_t* safety_limits);

/**
 * @brief Determinar si debe activarse modo offline
 *
 * Evalúa condiciones de conectividad y sistema para determinar
 * si es necesario cambiar a modo offline.
 *
 * @param wifi_connected Estado de conexión WiFi
 * @param mqtt_connected Estado de conexión MQTT
 * @param last_communication_timestamp Última comunicación exitosa
 * @param power_mode Modo de energía actual
 * @return true si debe activarse modo offline
 */
bool offline_mode_logic_should_activate_offline(
    bool wifi_connected,
    bool mqtt_connected,
    uint32_t last_communication_timestamp,
    power_mode_t power_mode
);

/**
 * @brief Evaluar y recomendar modo offline específico
 *
 * Función principal que analiza condiciones actuales y recomienda
 * el modo offline más apropiado.
 *
 * @param ambient_data Datos de sensores ambientales
 * @param soil_data Datos de sensores de suelo
 * @param current_mode Modo offline actual
 * @param context Contexto de la evaluación
 * @return Resultado con recomendación de modo
 */
offline_evaluation_result_t offline_mode_logic_evaluate_mode(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    offline_mode_t current_mode,
    const offline_evaluation_context_t* context
);

/**
 * @brief Evaluar necesidad de riego en modo offline
 *
 * Algoritmo autónomo que decide si iniciar riego sin conectividad
 * basándose únicamente en datos de sensores locales.
 *
 * @param ambient_data Datos ambientales
 * @param soil_data Datos de suelo
 * @param current_status Estado actual del sistema
 * @param offline_mode Modo offline actual
 * @return true si se debe iniciar riego offline
 */
bool offline_mode_logic_should_start_offline_irrigation(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const irrigation_status_t* current_status,
    offline_mode_t offline_mode
);

/**
 * @brief Calcular intervalo óptimo para modo offline
 *
 * Determina la frecuencia de evaluación basada en criticidad
 * de condiciones y nivel de batería.
 *
 * @param offline_mode Modo offline actual
 * @param battery_level Nivel de batería (0-100)
 * @param soil_criticality Criticidad del suelo (0.0-1.0)
 * @return Intervalo en segundos
 */
uint16_t offline_mode_logic_calculate_evaluation_interval(
    offline_mode_t offline_mode,
    uint8_t battery_level,
    float soil_criticality
);

/**
 * @brief Evaluar tendencia de condiciones del cultivo
 *
 * Analiza si las condiciones están mejorando, empeorando o estables
 * para ajustar el modo offline apropriadamente.
 *
 * @param ambient_data Datos ambientales actuales
 * @param soil_data Datos de suelo actuales
 * @param previous_ambient Datos ambientales anteriores
 * @param previous_soil Datos de suelo anteriores
 * @return Valor de tendencia (-1.0 = empeorando, 0.0 = estable, +1.0 = mejorando)
 */
float offline_mode_logic_analyze_trend(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const ambient_sensor_data_t* previous_ambient,
    const soil_sensor_data_t* previous_soil
);

/**
 * @brief Calcular criticidad del estado del suelo
 *
 * @param soil_data Datos de sensores de suelo
 * @return Nivel de criticidad (0.0 = óptimo, 1.0 = crítico)
 */
float offline_mode_logic_calculate_soil_criticality(const soil_sensor_data_t* soil_data);

/**
 * @brief Estimar consumo energético por modo offline
 *
 * @param offline_mode Modo offline a evaluar
 * @param interval_s Intervalo de evaluación
 * @return Consumo estimado en mAh por hora
 */
uint16_t offline_mode_logic_estimate_power_consumption(
    offline_mode_t offline_mode,
    uint16_t interval_s
);

/**
 * @brief Determinar trigger de activación offline
 *
 * @param wifi_connected Estado WiFi
 * @param mqtt_connected Estado MQTT
 * @param communication_timeout_s Timeout de comunicación
 * @param power_mode Modo de energía
 * @return Trigger de activación identificado
 */
offline_activation_trigger_t offline_mode_logic_determine_activation_trigger(
    bool wifi_connected,
    bool mqtt_connected,
    uint32_t communication_timeout_s,
    power_mode_t power_mode
);

/**
 * @brief Verificar si debe retornar a modo online
 *
 * @param wifi_connected Estado WiFi
 * @param mqtt_connected Estado MQTT
 * @param context Contexto de evaluación offline
 * @return true si debe intentar retorno a online
 */
bool offline_mode_logic_should_return_online(
    bool wifi_connected,
    bool mqtt_connected,
    const offline_evaluation_context_t* context
);

/**
 * @brief Generar explicación para cambio de modo
 *
 * @param result Resultado de evaluación
 * @param ambient_data Datos ambientales
 * @param soil_data Datos de suelo
 * @param explanation_buffer Buffer para explicación
 * @param buffer_size Tamaño del buffer
 */
void offline_mode_logic_generate_explanation(
    const offline_evaluation_result_t* result,
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    char* explanation_buffer,
    size_t buffer_size
);

/**
 * @brief Obtener configuración de transiciones
 *
 * @param config Puntero donde escribir configuración
 */
void offline_mode_logic_get_transition_config(offline_transition_config_t* config);

/**
 * @brief Actualizar configuración de transiciones
 *
 * @param config Nueva configuración
 * @return ESP_OK en éxito
 */
esp_err_t offline_mode_logic_set_transition_config(const offline_transition_config_t* config);

/**
 * @brief Registrar evento de modo offline
 *
 * @param event_type Tipo de evento (activación, cambio, desactivación)
 * @param old_mode Modo anterior
 * @param new_mode Modo nuevo
 * @param reason Razón del evento
 */
void offline_mode_logic_log_event(
    const char* event_type,
    offline_mode_t old_mode,
    offline_mode_t new_mode,
    const char* reason
);

/**
 * @brief Obtener estadísticas de modo offline
 *
 * @param stats Puntero donde escribir estadísticas
 */
void offline_mode_logic_get_statistics(offline_statistics_t* stats);

/**
 * @brief Resetear estadísticas de modo offline
 */
void offline_mode_logic_reset_statistics(void);

/**
 * @brief Convertir trigger de activación a string
 *
 * @param trigger Trigger de activación
 * @return String correspondiente
 */
const char* offline_mode_logic_activation_trigger_to_string(offline_activation_trigger_t trigger);

/**
 * @brief Convertir razón de cambio a string
 *
 * @param reason Razón de cambio de modo
 * @return String correspondiente
 */
const char* offline_mode_logic_change_reason_to_string(offline_mode_change_reason_t reason);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en segundos desde epoch Unix
 */
uint32_t offline_mode_logic_get_current_timestamp(void);

#endif // DOMAIN_SERVICES_OFFLINE_MODE_LOGIC_H