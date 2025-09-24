#ifndef APPLICATION_USE_CASES_OFFLINE_IRRIGATION_H
#define APPLICATION_USE_CASES_OFFLINE_IRRIGATION_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ambient_sensor_data.h"
#include "soil_sensor_data.h"
#include "irrigation_status.h"
#include "safety_limits.h"
#include "system_mode.h"

/**
 * @file offline_irrigation.h
 * @brief Use Case para riego autónomo en modo offline
 *
 * Implementa algoritmos de riego autónomo cuando no hay conectividad,
 * optimizado para supervivencia del cultivo y conservación de batería.
 * Diseñado específicamente para zonas rurales de Colombia.
 *
 * Especificaciones Técnicas - Offline Irrigation Use Case:
 * - Autonomía total: No depende de conectividad externa
 * - Algoritmos adaptativos: Frecuencia según criticidad (2h→1h→30min→15min)
 * - Battery-optimized: Minimiza consumo preservando cultivo
 * - Safety-first: Protecciones críticas siempre activas
 * - Estado persistente: Mantiene decisiones entre reinicios
 */

/**
 * @brief Estados de activación de modo offline
 */
typedef enum {
    OFFLINE_ACTIVATION_NONE = 0,              // No está en modo offline
    OFFLINE_ACTIVATION_WIFI_TIMEOUT,          // Activado por timeout WiFi
    OFFLINE_ACTIVATION_MQTT_TIMEOUT,          // Activado por timeout MQTT
    OFFLINE_ACTIVATION_NETWORK_UNRELIABLE,    // Activado por red inestable
    OFFLINE_ACTIVATION_POWER_SAVING,          // Activado por modo ahorro
    OFFLINE_ACTIVATION_MANUAL,                // Activado manualmente
    OFFLINE_ACTIVATION_SYSTEM_ERROR,          // Activado por error de sistema
    OFFLINE_ACTIVATION_MAX
} offline_activation_reason_t;

/**
 * @brief Tipos de decisiones de riego offline
 */
typedef enum {
    OFFLINE_DECISION_NO_ACTION = 0,           // No requiere acción
    OFFLINE_DECISION_START_IRRIGATION,        // Iniciar riego autónomo
    OFFLINE_DECISION_STOP_IRRIGATION,         // Detener riego autónomo
    OFFLINE_DECISION_EMERGENCY_STOP,          // Parada de emergencia offline
    OFFLINE_DECISION_WAIT_CONDITIONS,         // Esperar mejores condiciones
    OFFLINE_DECISION_EXTEND_EVALUATION,       // Extender evaluación (batería)
    OFFLINE_DECISION_REDUCE_FREQUENCY,        // Reducir frecuencia evaluación
    OFFLINE_DECISION_INCREASE_FREQUENCY,      // Aumentar frecuencia evaluación
    OFFLINE_DECISION_MAX
} offline_irrigation_decision_t;

/**
 * @brief Contexto de operación offline
 */
typedef struct {
    offline_activation_reason_t activation_reason; // Por qué se activó offline
    uint32_t offline_start_timestamp;          // Cuándo empezó modo offline
    uint32_t total_offline_duration_s;         // Tiempo total offline
    uint32_t last_communication_timestamp;     // Última comunicación exitosa
    uint32_t last_evaluation_timestamp;        // Última evaluación offline
    uint32_t next_evaluation_timestamp;        // Próxima evaluación programada
    offline_mode_t current_offline_mode;       // Modo offline actual
    uint16_t evaluation_interval_s;            // Intervalo actual de evaluación
    uint8_t battery_level_percent;             // Nivel de batería (0-100)
    bool solar_charging_active;                // Si está cargando con solar
} offline_irrigation_context_t;

/**
 * @brief Resultado de evaluación offline
 */
typedef struct {
    offline_irrigation_decision_t decision;    // Decisión tomada
    offline_mode_t recommended_mode;           // Modo recomendado
    uint8_t recommended_valve;                 // Válvula recomendada (1-3)
    uint16_t recommended_duration_minutes;     // Duración recomendada
    uint16_t next_evaluation_interval_s;       // Próximo intervalo evaluación
    float criticality_level;                   // Nivel de criticidad (0.0-1.0)
    float confidence_level;                    // Confianza de la decisión (0.0-1.0)
    uint32_t estimated_battery_usage_mah;      // Uso estimado de batería
    char decision_explanation[128];            // Explicación de la decisión
    char safety_status[64];                    // Estado de seguridad
    uint32_t processing_time_ms;               // Tiempo de procesamiento
} offline_irrigation_result_t;

/**
 * @brief Configuración de riego offline
 */
typedef struct {
    // Intervalos por modo (segundos)
    uint16_t normal_mode_interval_s;           // 7200 - Modo normal (2 horas)
    uint16_t warning_mode_interval_s;          // 3600 - Modo warning (1 hora)
    uint16_t critical_mode_interval_s;         // 1800 - Modo crítico (30 min)
    uint16_t emergency_mode_interval_s;        // 900 - Modo emergencia (15 min)

    // Umbrales de activación offline
    uint16_t wifi_timeout_s;                   // 300 - Timeout WiFi (5 min)
    uint16_t mqtt_timeout_s;                   // 180 - Timeout MQTT (3 min)
    uint8_t network_failure_threshold;         // 3 - Fallos consecutivos

    // Optimización de batería
    uint8_t minimum_battery_level_percent;     // 20 - Mínimo nivel batería
    bool enable_solar_optimization;            // Optimizar para carga solar
    bool enable_deep_sleep_offline;            // Usar deep sleep en offline

    // Configuración de seguridad offline
    bool enable_emergency_irrigation;          // Permitir riego de emergencia
    float soil_emergency_threshold;            // 25.0 - Umbral emergencia suelo
    uint16_t max_offline_irrigation_minutes;   // 20 - Máximo tiempo riego offline
} offline_irrigation_config_t;

/**
 * @brief Estadísticas de modo offline
 */
typedef struct {
    uint32_t total_offline_activations;        // Total activaciones offline
    uint32_t total_offline_time_s;             // Tiempo total offline
    uint32_t offline_irrigation_sessions;      // Sesiones de riego offline
    uint32_t emergency_irrigations_offline;    // Riegos de emergencia offline
    uint32_t mode_transitions;                 // Transiciones de modo
    uint32_t safety_blocks_offline;            // Bloqueos de seguridad offline
    float average_battery_consumption_offline; // Consumo promedio batería offline
    uint32_t longest_offline_period_s;         // Período offline más largo
    uint32_t successful_offline_evaluations;   // Evaluaciones exitosas offline
    uint32_t failed_offline_evaluations;       // Evaluaciones fallidas offline
} offline_irrigation_statistics_t;

/**
 * @brief Inicializar use case de riego offline
 *
 * @param safety_limits Límites de seguridad del sistema
 * @param device_mac_address MAC address del dispositivo
 * @return ESP_OK en éxito
 */
esp_err_t offline_irrigation_init(
    const safety_limits_t* safety_limits,
    const char* device_mac_address
);

/**
 * @brief Activar modo offline
 *
 * @param reason Razón de la activación offline
 * @param context Contexto inicial offline
 * @return ESP_OK en éxito
 */
esp_err_t offline_irrigation_activate(
    offline_activation_reason_t reason,
    const offline_irrigation_context_t* context
);

/**
 * @brief Desactivar modo offline
 *
 * @param reconnection_successful true si la reconexión fue exitosa
 * @return ESP_OK en éxito
 */
esp_err_t offline_irrigation_deactivate(bool reconnection_successful);

/**
 * @brief Realizar evaluación de riego offline
 *
 * Función principal que evalúa condiciones y toma decisiones
 * de riego autónomo sin conectividad.
 *
 * @param ambient_data Datos de sensores ambientales
 * @param soil_data Datos de sensores de suelo
 * @param current_status Estado actual del sistema
 * @param context Contexto offline actual
 * @return Resultado de evaluación offline
 */
offline_irrigation_result_t offline_irrigation_evaluate_conditions(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const irrigation_status_t* current_status,
    const offline_irrigation_context_t* context
);

/**
 * @brief Determinar modo offline apropiado
 *
 * @param ambient_data Datos ambientales
 * @param soil_data Datos de suelo
 * @param context Contexto offline
 * @return Modo offline recomendado
 */
offline_mode_t offline_irrigation_determine_mode(
    const ambient_sensor_data_t* ambient_data,
    const soil_sensor_data_t* soil_data,
    const offline_irrigation_context_t* context
);

/**
 * @brief Calcular intervalo de evaluación óptimo
 *
 * Basado en criticidad, nivel de batería y modo offline.
 *
 * @param offline_mode Modo offline actual
 * @param battery_level Nivel de batería (0-100)
 * @param criticality_level Nivel de criticidad (0.0-1.0)
 * @return Intervalo en segundos
 */
uint16_t offline_irrigation_calculate_evaluation_interval(
    offline_mode_t offline_mode,
    uint8_t battery_level,
    float criticality_level
);

/**
 * @brief Evaluar si debe iniciar riego offline
 *
 * Algoritmo autónomo para decidir inicio de riego sin conectividad.
 *
 * @param soil_data Datos de sensores de suelo
 * @param ambient_data Datos ambientales
 * @param context Contexto offline
 * @return true si debe iniciar riego
 */
bool offline_irrigation_should_start_irrigation(
    const soil_sensor_data_t* soil_data,
    const ambient_sensor_data_t* ambient_data,
    const offline_irrigation_context_t* context
);

/**
 * @brief Evaluar si debe detener riego offline
 *
 * @param soil_data Datos de sensores de suelo
 * @param current_status Estado actual del riego
 * @return true si debe detener riego
 */
bool offline_irrigation_should_stop_irrigation(
    const soil_sensor_data_t* soil_data,
    const irrigation_status_t* current_status
);

/**
 * @brief Calcular criticidad del cultivo
 *
 * Basado en humedad del suelo, condiciones ambientales y tiempo offline.
 *
 * @param soil_data Datos de sensores de suelo
 * @param ambient_data Datos ambientales
 * @param context Contexto offline
 * @return Nivel de criticidad (0.0-1.0)
 */
float offline_irrigation_calculate_criticality_level(
    const soil_sensor_data_t* soil_data,
    const ambient_sensor_data_t* ambient_data,
    const offline_irrigation_context_t* context
);

/**
 * @brief Estimar consumo de batería para operación
 *
 * @param decision Decisión a ejecutar
 * @param duration_minutes Duración de la operación
 * @param context Contexto offline
 * @return Consumo estimado en mAh
 */
uint32_t offline_irrigation_estimate_battery_usage(
    offline_irrigation_decision_t decision,
    uint16_t duration_minutes,
    const offline_irrigation_context_t* context
);

/**
 * @brief Ejecutar decisión de riego offline
 *
 * @param result Resultado de evaluación con decisión
 * @param context Contexto offline actual
 * @return ESP_OK en éxito
 */
esp_err_t offline_irrigation_execute_decision(
    const offline_irrigation_result_t* result,
    const offline_irrigation_context_t* context
);

/**
 * @brief Verificar si debe retornar a modo online
 *
 * @param wifi_connected Estado de conexión WiFi
 * @param mqtt_connected Estado de conexión MQTT
 * @param context Contexto offline
 * @return true si debe intentar retorno online
 */
bool offline_irrigation_should_return_online(
    bool wifi_connected,
    bool mqtt_connected,
    const offline_irrigation_context_t* context
);

/**
 * @brief Persistir estado offline en NVS
 *
 * Para mantener estado entre reinicios.
 *
 * @param context Contexto offline a persistir
 * @return ESP_OK en éxito
 */
esp_err_t offline_irrigation_persist_state(const offline_irrigation_context_t* context);

/**
 * @brief Restaurar estado offline desde NVS
 *
 * @param context Puntero donde escribir contexto restaurado
 * @return ESP_OK en éxito
 */
esp_err_t offline_irrigation_restore_state(offline_irrigation_context_t* context);

/**
 * @brief Generar explicación de decisión offline
 *
 * @param result Resultado de evaluación offline
 * @param explanation_buffer Buffer para explicación
 * @param buffer_size Tamaño del buffer
 */
void offline_irrigation_generate_explanation(
    const offline_irrigation_result_t* result,
    char* explanation_buffer,
    size_t buffer_size
);

/**
 * @brief Actualizar contexto offline
 *
 * @param context Contexto a actualizar
 * @param battery_level Nivel actual de batería
 * @param solar_charging_active Estado de carga solar
 */
void offline_irrigation_update_context(
    offline_irrigation_context_t* context,
    uint8_t battery_level,
    bool solar_charging_active
);

/**
 * @brief Configurar parámetros de riego offline
 *
 * @param config Nueva configuración offline
 * @return ESP_OK en éxito
 */
esp_err_t offline_irrigation_set_config(const offline_irrigation_config_t* config);

/**
 * @brief Obtener configuración actual offline
 *
 * @param config Puntero donde escribir configuración
 */
void offline_irrigation_get_config(offline_irrigation_config_t* config);

/**
 * @brief Registrar evento offline para estadísticas
 *
 * @param result Resultado de evaluación offline
 */
void offline_irrigation_record_event(const offline_irrigation_result_t* result);

/**
 * @brief Obtener estadísticas de modo offline
 *
 * @param stats Puntero donde escribir estadísticas
 */
void offline_irrigation_get_statistics(offline_irrigation_statistics_t* stats);

/**
 * @brief Resetear estadísticas offline
 */
void offline_irrigation_reset_statistics(void);

/**
 * @brief Verificar estado de modo offline
 *
 * @return true si está actualmente en modo offline
 */
bool offline_irrigation_is_active(void);

/**
 * @brief Obtener contexto offline actual
 *
 * @param context Puntero donde escribir contexto actual
 * @return ESP_OK si está en modo offline
 */
esp_err_t offline_irrigation_get_current_context(offline_irrigation_context_t* context);

/**
 * @brief Convertir razón de activación a string
 *
 * @param reason Razón de activación
 * @return String correspondiente
 */
const char* offline_irrigation_activation_reason_to_string(offline_activation_reason_t reason);

/**
 * @brief Convertir decisión offline a string
 *
 * @param decision Decisión offline
 * @return String correspondiente
 */
const char* offline_irrigation_decision_to_string(offline_irrigation_decision_t decision);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en segundos desde epoch Unix
 */
uint32_t offline_irrigation_get_current_timestamp(void);

#endif // APPLICATION_USE_CASES_OFFLINE_IRRIGATION_H