#ifndef DOMAIN_VALUE_OBJECTS_SYSTEM_MODE_H
#define DOMAIN_VALUE_OBJECTS_SYSTEM_MODE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @file system_mode.h
 * @brief Value Object para modos de operación del sistema
 *
 * Define los diferentes modos de operación del sistema de riego,
 * especialmente el modo offline adaptativo basado en criticidad de sensores.
 *
 * Especificaciones Técnicas - Modos del Sistema:
 * - Modo online: Conectividad WiFi/MQTT disponible
 * - Modo offline adaptativo: Sin conectividad, frecuencia basada en criticidad
 * - Gestión energética: Optimización para batería + solar
 * - Transiciones automáticas: Basadas en conectividad y estado sensores
 */

/**
 * @brief Tipos de modo de conectividad
 */
typedef enum {
    CONNECTIVITY_MODE_UNKNOWN = 0,
    CONNECTIVITY_MODE_ONLINE,             // WiFi y MQTT activos
    CONNECTIVITY_MODE_WIFI_ONLY,          // Solo WiFi, sin MQTT
    CONNECTIVITY_MODE_OFFLINE,            // Sin conectividad
    CONNECTIVITY_MODE_MAINTENANCE,        // Modo mantenimiento
    CONNECTIVITY_MODE_MAX
} connectivity_mode_t;

/**
 * @brief Tipos de modo offline adaptativo
 *
 * Basado en criticidad de las condiciones del cultivo para optimizar
 * el uso de batería mientras se garantiza la supervivencia del cultivo.
 */
typedef enum {
    OFFLINE_MODE_DISABLED = 0,
    OFFLINE_MODE_NORMAL,                  // Cada 2 horas - condiciones normales
    OFFLINE_MODE_WARNING,                 // Cada 1 hora - humedad bajando
    OFFLINE_MODE_CRITICAL,                // Cada 30 min - condición crítica
    OFFLINE_MODE_EMERGENCY,               // Cada 15 min - supervivencia cultivo
    OFFLINE_MODE_MAX
} offline_mode_t;

/**
 * @brief Estados de energía del sistema
 */
typedef enum {
    POWER_MODE_NORMAL = 0,                // Batería > 50%, operación normal
    POWER_MODE_SAVING,                    // Batería 20-50%, conservar energía
    POWER_MODE_CRITICAL,                  // Batería < 20%, operación mínima
    POWER_MODE_EMERGENCY,                 // Batería < 10%, solo supervivencia
    POWER_MODE_CHARGING,                  // Cargando con solar
    POWER_MODE_MAX
} power_mode_t;

/**
 * @brief Value Object completo para modo del sistema
 *
 * Encapsula el estado completo de operación del sistema incluyendo
 * conectividad, modo offline y gestión energética.
 */
typedef struct {
    connectivity_mode_t connectivity;     // Modo de conectividad actual
    offline_mode_t offline_mode;          // Modo offline adaptativo
    power_mode_t power_mode;              // Modo de gestión energética
    uint32_t mode_change_timestamp;       // Última vez que cambió el modo
    uint32_t offline_activation_timestamp; // Cuándo se activó modo offline
    uint16_t sensor_reading_interval_s;   // Intervalo actual de lectura sensores
    uint16_t data_publish_interval_s;     // Intervalo actual publicación datos
    bool manual_mode_override;            // Override manual del modo automático
    char mode_change_reason[64];          // Razón del último cambio de modo
} system_mode_t;

/**
 * @brief Configuración de intervalos por modo offline
 */
typedef struct {
    uint16_t normal_interval_s;           // 7200s (2 horas) - modo normal
    uint16_t warning_interval_s;          // 3600s (1 hora) - advertencia
    uint16_t critical_interval_s;         // 1800s (30 min) - crítico
    uint16_t emergency_interval_s;        // 900s (15 min) - emergencia
} offline_intervals_t;

/**
 * @brief Intervalos por defecto para modo offline adaptativo
 */
#define OFFLINE_MODE_NORMAL_INTERVAL_S      7200    // 2 horas
#define OFFLINE_MODE_WARNING_INTERVAL_S     3600    // 1 hora
#define OFFLINE_MODE_CRITICAL_INTERVAL_S    1800    // 30 minutos
#define OFFLINE_MODE_EMERGENCY_INTERVAL_S   900     // 15 minutos

/**
 * @brief Intervalos online por defecto
 */
#define ONLINE_MODE_SENSOR_INTERVAL_S       60      // 1 minuto
#define ONLINE_MODE_PUBLISH_INTERVAL_S      60      // 1 minuto

/**
 * @brief Timeouts para detección de pérdida de conectividad
 */
#define CONNECTIVITY_WIFI_TIMEOUT_S         300     // 5 minutos
#define CONNECTIVITY_MQTT_TIMEOUT_S         180     // 3 minutos

/**
 * @brief Strings para modos de conectividad
 */
#define CONNECTIVITY_MODE_ONLINE_STR        "online"
#define CONNECTIVITY_MODE_WIFI_ONLY_STR     "wifi_only"
#define CONNECTIVITY_MODE_OFFLINE_STR       "offline"
#define CONNECTIVITY_MODE_MAINTENANCE_STR   "maintenance"

/**
 * @brief Strings para modos offline
 */
#define OFFLINE_MODE_NORMAL_STR             "normal"
#define OFFLINE_MODE_WARNING_STR            "warning"
#define OFFLINE_MODE_CRITICAL_STR           "critical"
#define OFFLINE_MODE_EMERGENCY_STR          "emergency"

/**
 * @brief Strings para modos de energía
 */
#define POWER_MODE_NORMAL_STR               "normal"
#define POWER_MODE_SAVING_STR               "saving"
#define POWER_MODE_CRITICAL_STR             "critical"
#define POWER_MODE_EMERGENCY_STR            "emergency"
#define POWER_MODE_CHARGING_STR             "charging"

/**
 * @brief Inicializar modo del sistema con valores por defecto
 *
 * @param mode Puntero al modo del sistema
 */
void system_mode_init_default(system_mode_t* mode);

/**
 * @brief Actualizar modo de conectividad
 *
 * @param mode Puntero al modo del sistema
 * @param connectivity Nuevo modo de conectividad
 * @param reason Razón del cambio
 */
void system_mode_set_connectivity(system_mode_t* mode,
                                connectivity_mode_t connectivity,
                                const char* reason);

/**
 * @brief Actualizar modo offline basado en criticidad
 *
 * @param mode Puntero al modo del sistema
 * @param offline_mode Nuevo modo offline
 * @param reason Razón del cambio
 */
void system_mode_set_offline_mode(system_mode_t* mode,
                                offline_mode_t offline_mode,
                                const char* reason);

/**
 * @brief Actualizar modo de energía
 *
 * @param mode Puntero al modo del sistema
 * @param power_mode Nuevo modo de energía
 */
void system_mode_set_power_mode(system_mode_t* mode, power_mode_t power_mode);

/**
 * @brief Verificar si el sistema está en modo online
 *
 * @param mode Puntero al modo del sistema
 * @return true si está online (WiFi y MQTT activos)
 */
bool system_mode_is_online(const system_mode_t* mode);

/**
 * @brief Verificar si el sistema está en modo offline
 *
 * @param mode Puntero al modo del sistema
 * @return true si está offline (sin conectividad)
 */
bool system_mode_is_offline(const system_mode_t* mode);

/**
 * @brief Verificar si el sistema permite operaciones de alto consumo
 *
 * @param mode Puntero al modo del sistema
 * @return true si permite operaciones de alto consumo
 */
bool system_mode_allows_high_power_operations(const system_mode_t* mode);

/**
 * @brief Obtener intervalo actual de lectura de sensores
 *
 * @param mode Puntero al modo del sistema
 * @return Intervalo en segundos
 */
uint16_t system_mode_get_sensor_interval(const system_mode_t* mode);

/**
 * @brief Obtener intervalo actual de publicación de datos
 *
 * @param mode Puntero al modo del sistema
 * @return Intervalo en segundos (0 si está offline)
 */
uint16_t system_mode_get_publish_interval(const system_mode_t* mode);

/**
 * @brief Determinar modo offline basado en condiciones de sensores
 *
 * Analiza las lecturas de sensores para determinar el nivel de criticidad
 * y ajustar automáticamente el modo offline.
 *
 * @param current_soil_humidity Humedad promedio del suelo (%)
 * @param ambient_temperature Temperatura ambiente (°C)
 * @param time_since_last_irrigation Tiempo desde último riego (minutos)
 * @return Modo offline recomendado
 */
offline_mode_t system_mode_determine_offline_mode(float current_soil_humidity,
                                                 float ambient_temperature,
                                                 uint32_t time_since_last_irrigation);

/**
 * @brief Actualizar intervalos basados en el modo offline actual
 *
 * @param mode Puntero al modo del sistema
 */
void system_mode_update_intervals(system_mode_t* mode);

/**
 * @brief Verificar si es tiempo de cambiar de modo offline
 *
 * @param mode Puntero al modo del sistema
 * @param current_soil_humidity Humedad promedio del suelo (%)
 * @param ambient_temperature Temperatura ambiente (°C)
 * @return true si se recomienda cambiar el modo
 */
bool system_mode_should_change_offline_mode(const system_mode_t* mode,
                                           float current_soil_humidity,
                                           float ambient_temperature);

/**
 * @brief Activar override manual del modo
 *
 * Permite forzar un modo específico ignorando la lógica automática.
 *
 * @param mode Puntero al modo del sistema
 * @param override_active true para activar override
 * @param reason Razón del override
 */
void system_mode_set_manual_override(system_mode_t* mode,
                                   bool override_active,
                                   const char* reason);

/**
 * @brief Obtener tiempo en el modo actual
 *
 * @param mode Puntero al modo del sistema
 * @return Segundos en el modo actual
 */
uint32_t system_mode_get_time_in_current_mode(const system_mode_t* mode);

/**
 * @brief Convertir modo de conectividad a string
 *
 * @param connectivity Modo de conectividad
 * @return String correspondiente
 */
const char* system_mode_connectivity_to_string(connectivity_mode_t connectivity);

/**
 * @brief Convertir string a modo de conectividad
 *
 * @param mode_str String del modo
 * @return Modo de conectividad correspondiente
 */
connectivity_mode_t system_mode_string_to_connectivity(const char* mode_str);

/**
 * @brief Convertir modo offline a string
 *
 * @param offline_mode Modo offline
 * @return String correspondiente
 */
const char* system_mode_offline_to_string(offline_mode_t offline_mode);

/**
 * @brief Convertir string a modo offline
 *
 * @param mode_str String del modo
 * @return Modo offline correspondiente
 */
offline_mode_t system_mode_string_to_offline(const char* mode_str);

/**
 * @brief Convertir modo de energía a string
 *
 * @param power_mode Modo de energía
 * @return String correspondiente
 */
const char* system_mode_power_to_string(power_mode_t power_mode);

/**
 * @brief Convertir string a modo de energía
 *
 * @param mode_str String del modo
 * @return Modo de energía correspondiente
 */
power_mode_t system_mode_string_to_power(const char* mode_str);

/**
 * @brief Obtener configuración de intervalos offline
 *
 * @param intervals Puntero donde escribir la configuración
 */
void system_mode_get_offline_intervals(offline_intervals_t* intervals);

/**
 * @brief Obtener timestamp actual del sistema
 *
 * @return Timestamp actual en segundos desde epoch Unix
 */
uint32_t system_mode_get_current_timestamp(void);

#endif // DOMAIN_VALUE_OBJECTS_SYSTEM_MODE_H