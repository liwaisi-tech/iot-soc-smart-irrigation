# PLAN DE EJECUCIÓN - IRRIGATION CONTROLLER COMPONENT

**Fase**: Phase 5 - Irrigation Control Implementation
**Fecha Inicio**: 2025-10-18
**Duración Estimada**: 3-4 días (incluyendo testing)
**Estado**: 🟢 LISTO PARA EMPEZAR

---

## 📋 RESUMEN EJECUTIVO

### Objetivo
Implementar el componente `irrigation_controller` que controle el riego automático basado en umbrales de humedad del suelo, con integración MQTT para control remoto y notificaciones a N8N para eventos críticos.

### Características Principales
- ✅ State machine completo para riego automático
- ✅ Evaluación de sensores cada 60s (online) o 2h (offline)
- ✅ Control de válvula via GPIO LED (simulación Phase 5)
- ✅ Protecciones de seguridad (timeouts, temperatura, sobre-humedad)
- ✅ Control remoto vía comandos MQTT (override)
- ✅ Notificaciones N8N para eventos (riego iniciado/detenido, errores)
- ✅ Detección automática online/offline

### Restricciones Phase 5
- ❌ NO implementar: Deep sleep, ventana riego nocturno, histórico, failsafe
- ⏳ Planificar para Phase 6: Dejar tech debt documentado

---

## 🏗️ ESTRUCTURA DE ARCHIVOS

```
components/irrigation_controller/
├── irrigation_controller.h           # API pública + tipos
├── irrigation_controller.c           # State machine + lógica principal
├── valve_driver.c                    # Control GPIO bajo nivel
├── valve_driver.h                    # Header valve driver
├── safety_watchdog.c                 # Protecciones de seguridad
├── safety_watchdog.h                 # Header watchdog
├── CMakeLists.txt                    # Configuración CMake
└── Kconfig                           # Configuración menuconfig
```

---

## 📝 FASES DE IMPLEMENTACIÓN

## FASE 1: Estructura Base (Día 1)

### 1.1 Crear tipos y constantes básicos
**Archivo**: `irrigation_controller.h`

```c
// State machine
typedef enum {
    IRRIGATION_STATE_IDLE,
    IRRIGATION_STATE_EVALUATING,
    IRRIGATION_STATE_RUNNING,
    IRRIGATION_STATE_OVERRIDE_MQTT,
    IRRIGATION_STATE_OFFLINE,
    IRRIGATION_STATE_SAFETY_STOP,
    IRRIGATION_STATE_ERROR
} irrigation_state_t;

typedef enum {
    IRRIGATION_REASON_IDLE,
    IRRIGATION_REASON_SENSOR_THRESHOLD,
    IRRIGATION_REASON_MQTT_COMMAND,
    IRRIGATION_REASON_OFFLINE_MODE,
    IRRIGATION_REASON_SAFETY_TIMEOUT,
    IRRIGATION_REASON_TEMPERATURE_HIGH,
    IRRIGATION_REASON_OVERMOISTURE,
    IRRIGATION_REASON_SENSOR_ERROR
} irrigation_stop_reason_t;

// Estructuras
typedef struct {
    irrigation_state_t current_state;
    time_t session_start_time;
    time_t last_session_end_time;
    uint16_t current_session_duration_min;
    bool is_valve_open;
    time_t valve_open_time;
    bool mqtt_override_active;
    char mqtt_override_command[20];
    uint32_t session_count;
    irrigation_stop_reason_t last_stop_reason;
    bool is_online;
} irrigation_controller_status_t;

typedef struct {
    float soil_humidity_avg;
    float soil_humidity_max;
    float soil_humidity_min;
    float ambient_temperature;
    float ambient_humidity;
    time_t reading_timestamp;
} irrigation_sensor_reading_t;

// Constantes
#define IRRIGATION_SOIL_THRESHOLD_START      51
#define IRRIGATION_SOIL_THRESHOLD_STOP_MIN   70
#define IRRIGATION_SOIL_THRESHOLD_STOP_MAX   75
#define IRRIGATION_SOIL_THRESHOLD_DANGER     80
#define IRRIGATION_MAX_DURATION_MINUTES      120
#define IRRIGATION_MIN_INTERVAL_MINUTES      240
#define IRRIGATION_VALVE_TIMEOUT_MINUTES     40
#define IRRIGATION_TEMP_CRITICAL_CELSIUS     40
#define IRRIGATION_VALVE_LED_GPIO            GPIO_NUM_21
```

**Tareas**:
- [ ] Crear `irrigation_controller.h` con tipos
- [ ] Definir constantes finales
- [ ] Verificar consistencia con `Logica_de_riego.md`

### 1.2 Crear archivos de headers base
**Archivos**: `valve_driver.h`, `safety_watchdog.h`

```c
// valve_driver.h
esp_err_t valve_driver_init(void);
esp_err_t valve_driver_open(void);
esp_err_t valve_driver_close(void);
bool valve_driver_is_open(void);
esp_err_t valve_driver_deinit(void);

// safety_watchdog.h
typedef struct {
    uint32_t session_duration_ms;
    uint32_t valve_open_time_ms;
    uint32_t mqtt_override_idle_time_ms;
    float current_temperature;
} watchdog_inputs_t;

typedef struct {
    bool session_timeout_exceeded;
    bool valve_timeout_exceeded;
    bool mqtt_timeout_exceeded;
    bool temperature_critical;
} watchdog_alerts_t;

esp_err_t safety_watchdog_init(void);
watchdog_alerts_t safety_watchdog_check(const watchdog_inputs_t* inputs);
esp_err_t safety_watchdog_reset_session(void);
```

**Tareas**:
- [ ] Crear `valve_driver.h`
- [ ] Crear `safety_watchdog.h`
- [ ] Crear `irrigation_controller.h` completo

### 1.3 Crear archivos .c base con stubs
**Archivos**: `irrigation_controller.c`, `valve_driver.c`, `safety_watchdog.c`

**Tareas**:
- [ ] Crear `valve_driver.c` con stubs
- [ ] Crear `safety_watchdog.c` con stubs
- [ ] Crear `irrigation_controller.c` con stubs
- [ ] Compilar sin errores (solo stubs)

### 1.4 CMakeLists.txt y Kconfig
**Archivos**: `CMakeLists.txt`, `Kconfig`

```cmake
# CMakeLists.txt
idf_component_register(
    SRCS "irrigation_controller.c" "valve_driver.c" "safety_watchdog.c"
    INCLUDE_DIRS "."
    REQUIRES
        sensor_reader
        wifi_manager
        mqtt_client
        device_config
    PRIV_REQUIRES
        esp_timer
)
```

```kconfig
# Kconfig
menu "Irrigation Controller Configuration"

    config IRRIGATION_SOIL_THRESHOLD_START
        int "Soil moisture threshold start (%)"
        default 51
        range 30 70
        help
            Irrigation starts when soil humidity average drops below this level.

    config IRRIGATION_SOIL_THRESHOLD_STOP
        int "Soil moisture threshold stop (%)"
        default 70
        range 50 85
        help
            Irrigation stops when all sensors reach this level.

    config IRRIGATION_SOIL_THRESHOLD_DANGER
        int "Soil moisture danger level (%)"
        default 80
        range 70 95
        help
            Immediate stop if any sensor exceeds this level.

    config IRRIGATION_MAX_DURATION_MINUTES
        int "Max irrigation duration (minutes)"
        default 120
        range 30 180
        help
            Safety limit - stops after this time.

    config IRRIGATION_VALVE_TIMEOUT_MINUTES
        int "Valve timeout alert (minutes)"
        default 40
        range 20 60
        help
            Alert to N8N if valve open longer than this.

    config IRRIGATION_OFFLINE_INTERVAL_HOURS
        int "Offline mode evaluation interval (hours)"
        default 2
        range 1 6
        help
            Check sensors every N hours in offline mode.

    config IRRIGATION_N8N_WEBHOOK_URL
        string "N8N Webhook URL"
        default "http://192.168.1.177:5678/webhook/irrigation-events"
        help
            URL where irrigation events are sent for notifications.

    config IRRIGATION_ENABLE_MQTT_OVERRIDE
        bool "Enable MQTT command override"
        default y
        help
            Allow remote start/stop via MQTT commands.

endmenu
```

**Tareas**:
- [ ] Crear `CMakeLists.txt`
- [ ] Crear `Kconfig`
- [ ] Verificar que compila

---

## FASE 2: State Machine y Lógica Core (Día 1-2)

### 2.1 Implementar valve_driver
**Archivo**: `valve_driver.c`

**Responsabilidad**: Control bajo nivel del GPIO (LED simulador)

```c
static const char *TAG = "valve_driver";
static bool is_valve_open = false;

esp_err_t valve_driver_init(void) {
    // Configurar GPIO_NUM_21 como OUTPUT
    // Iniciar en estado LOW (válvula cerrada)
}

esp_err_t valve_driver_open(void) {
    // SET GPIO HIGH
    // is_valve_open = true
    // Log: "Valve opened"
}

esp_err_t valve_driver_close(void) {
    // SET GPIO LOW
    // is_valve_open = false
    // Log: "Valve closed"
}

bool valve_driver_is_open(void) {
    return is_valve_open;
}
```

**Tareas**:
- [ ] Implementar `valve_driver_init()` - configurar GPIO
- [ ] Implementar `valve_driver_open()` - SET HIGH
- [ ] Implementar `valve_driver_close()` - SET LOW
- [ ] Implementar `valve_driver_is_open()`
- [ ] Testing: Verificar que el LED enciende/apaga correctamente

### 2.2 Implementar safety_watchdog
**Archivo**: `safety_watchdog.c`

**Responsabilidad**: Monitorear tiempos y protecciones

```c
static const char *TAG = "safety_watchdog";
static time_t session_start_time = 0;

watchdog_alerts_t safety_watchdog_check(const watchdog_inputs_t* inputs) {
    watchdog_alerts_t alerts = {false, false, false, false};

    // Verificar timeouts y protecciones
    if (inputs->session_duration_ms > (IRRIGATION_MAX_DURATION_MINUTES * 60 * 1000)) {
        alerts.session_timeout_exceeded = true;
    }

    if (inputs->valve_open_time_ms > (IRRIGATION_VALVE_TIMEOUT_MINUTES * 60 * 1000)) {
        alerts.valve_timeout_exceeded = true;
    }

    if (inputs->current_temperature > IRRIGATION_TEMP_CRITICAL_CELSIUS) {
        alerts.temperature_critical = true;
    }

    return alerts;
}
```

**Tareas**:
- [ ] Implementar `safety_watchdog_init()`
- [ ] Implementar `safety_watchdog_check()` - toda la lógica de alerts
- [ ] Implementar `safety_watchdog_reset_session()`
- [ ] Testing: Verificar cada condición de alert

### 2.3 Implementar state machine core
**Archivo**: `irrigation_controller.c`

**Estructura principal**:

```c
static const char *TAG = "irrigation_controller";
static irrigation_controller_status_t controller_status = {
    .current_state = IRRIGATION_STATE_IDLE,
    .is_online = false,
    .session_count = 0
};

static TaskHandle_t irrigation_task_handle = NULL;

// Task principal
void irrigation_evaluation_task(void *param) {
    while (1) {
        // 1. Detectar conectividad
        bool is_online = wifi_manager_is_connected();

        // 2. Si cambio de estado online/offline
        if (controller_status.is_online != is_online) {
            controller_status.is_online = is_online;
            if (!is_online) {
                controller_status.current_state = IRRIGATION_STATE_OFFLINE;
            }
        }

        // 3. State machine evaluation
        switch (controller_status.current_state) {
            case IRRIGATION_STATE_IDLE:
                irrigation_state_idle_handler();
                break;
            case IRRIGATION_STATE_RUNNING:
                irrigation_state_running_handler();
                break;
            case IRRIGATION_STATE_OFFLINE:
                irrigation_state_offline_handler();
                break;
            // ... otros estados
        }

        // 4. Esperar según modo
        uint32_t wait_ms = controller_status.is_online ? 60000 : (2 * 60 * 60 * 1000);
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

// Manejadores de estado
static void irrigation_state_idle_handler(void) {
    // Leer sensores
    sensor_reading_t reading;
    sensor_reader_get_all(&reading);

    // Calcular promedio
    float avg = (reading.soil_humidity_1 +
                 reading.soil_humidity_2 +
                 reading.soil_humidity_3) / 3.0;

    // Si < 51%, iniciar riego
    if (avg < 51.0) {
        valve_driver_open();
        controller_status.current_state = IRRIGATION_STATE_RUNNING;
        controller_status.session_start_time = time(NULL);
        send_webhook_irrigation_started(&reading);
    }
}

static void irrigation_state_running_handler(void) {
    // Leer sensores
    sensor_reading_t reading;
    sensor_reader_get_all(&reading);

    // Verificar condiciones de parada
    bool all_above_threshold = (reading.soil_humidity_1 >= 70.0 &&
                                reading.soil_humidity_2 >= 70.0 &&
                                reading.soil_humidity_3 >= 70.0);

    bool any_dangerous = (reading.soil_humidity_1 >= 80.0 ||
                         reading.soil_humidity_2 >= 80.0 ||
                         reading.soil_humidity_3 >= 80.0);

    // Verificar protecciones
    time_t elapsed = time(NULL) - controller_status.session_start_time;
    watchdog_inputs_t watchdog_inputs = {
        .session_duration_ms = elapsed * 1000,
        .valve_open_time_ms = elapsed * 1000,
        .current_temperature = reading.ambient_temperature
    };
    watchdog_alerts_t alerts = safety_watchdog_check(&watchdog_inputs);

    // Decidir parada
    if (all_above_threshold || any_dangerous ||
        alerts.session_timeout_exceeded || alerts.temperature_critical) {

        valve_driver_close();
        controller_status.current_state = IRRIGATION_STATE_IDLE;
        send_webhook_irrigation_stopped(&reading, elapsed);
    }

    // Alert valve timeout
    if (alerts.valve_timeout_exceeded) {
        send_webhook_valve_timeout(elapsed);
    }
}
```

**Tareas**:
- [ ] Implementar `irrigation_controller_init()`
- [ ] Crear task `irrigation_evaluation_task()`
- [ ] Implementar `irrigation_state_idle_handler()`
- [ ] Implementar `irrigation_state_running_handler()`
- [ ] Implementar `irrigation_state_offline_handler()`
- [ ] Implementar `irrigation_controller_get_status()`
- [ ] Testing: State machine transitions correctas

---

## FASE 3: Integraciones Externas (Día 2-3)

### 3.1 Integración con sensor_reader
**Archivo**: `irrigation_controller.c`

```c
#include "sensor_reader.h"

// Ya usado en handlers de estado
sensor_reading_t reading;
sensor_reader_get_all(&reading);

// Manejar errores de lectura
if (sensor_reader_get_all(&reading) != ESP_OK) {
    controller_status.current_state = IRRIGATION_STATE_ERROR;
    send_webhook_sensor_error("soil_moisture_1", -1);
}
```

**Tareas**:
- [ ] Integrar lecturas de `sensor_reader`
- [ ] Manejar errores de lectura
- [ ] Testing: Verificar datos correctos

### 3.2 Integración con wifi_manager
**Archivo**: `irrigation_controller.c`

```c
#include "wifi_manager.h"

// En task principal
bool is_online = wifi_manager_is_connected() && mqtt_client_is_connected();

if (controller_status.is_online != is_online) {
    controller_status.is_online = is_online;
    // Cambiar estado según conectividad
    if (!is_online && controller_status.current_state != IRRIGATION_STATE_RUNNING) {
        controller_status.current_state = IRRIGATION_STATE_OFFLINE;
    }
}
```

**Tareas**:
- [ ] Detectar cambios online/offline
- [ ] Transiciones correctas entre IDLE/OFFLINE
- [ ] Testing: Simular desconexión WiFi

### 3.3 Integración con MQTT (Comandos Remotos)
**Archivo**: `irrigation_controller.c`

```c
#include "mqtt_client.h"

// Callback registrado en mqtt_client
static void mqtt_command_callback(const char* topic, const char* payload) {
    if (strcmp(payload, "start") == 0) {
        // Forzar inicio
        valve_driver_open();
        controller_status.current_state = IRRIGATION_STATE_OVERRIDE_MQTT;
        controller_status.mqtt_override_active = true;
        strcpy(controller_status.mqtt_override_command, "start");
        controller_status.session_start_time = time(NULL);
        send_webhook_irrigation_started_mqtt();

    } else if (strcmp(payload, "stop") == 0) {
        // Detener
        valve_driver_close();
        controller_status.current_state = IRRIGATION_STATE_IDLE;
        controller_status.mqtt_override_active = false;
        send_webhook_irrigation_stopped_mqtt();
    }
}

// En init
esp_err_t irrigation_controller_init(void) {
    // ...
    mqtt_client_register_command_callback(mqtt_command_callback);
}

// En handler OVERRIDE_MQTT
static void irrigation_state_override_mqtt_handler(void) {
    // Timeout 30 min sin nuevo comando
    time_t elapsed = time(NULL) - controller_status.session_start_time;
    if (elapsed > (30 * 60)) {
        valve_driver_close();
        controller_status.current_state = IRRIGATION_STATE_IDLE;
        controller_status.mqtt_override_active = false;
    }
}
```

**Tareas**:
- [ ] Registrar callback MQTT para comandos
- [ ] Implementar handler estado OVERRIDE_MQTT
- [ ] Implementar timeout MQTT (30 min)
- [ ] Testing: Enviar comandos MQTT start/stop

### 3.4 Webhooks N8N
**Archivo**: `irrigation_controller.c` (agregar funciones)

```c
// Helper para enviar webhooks
static void send_webhook(const char* event_type, cJSON* data) {
    // Obtener MAC address
    char mac_str[18];
    device_config_get_mac_address(mac_str, sizeof(mac_str));

    // Construir payload
    cJSON* payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "event_type", event_type);
    cJSON_AddStringToObject(payload, "mac_address", mac_str);
    cJSON_AddNumberToObject(payload, "timestamp", time(NULL));
    cJSON_AddItemToObject(payload, "data", data);

    // Convertir a string
    char* json_str = cJSON_Print(payload);

    // Enviar vía MQTT
    mqtt_client_publish(
        "irrigation/events/{mac}/webhook",
        json_str,
        strlen(json_str)
    );

    free(json_str);
    cJSON_Delete(payload);
}

// Eventos específicos
static void send_webhook_irrigation_started(const sensor_reading_t* reading) {
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "reason", "SENSOR_THRESHOLD");
    cJSON_AddNumberToObject(data, "soil_humidity_avg",
        (reading->soil_humidity_1 + reading->soil_humidity_2 + reading->soil_humidity_3) / 3.0);
    cJSON_AddNumberToObject(data, "ambient_temperature", reading->ambient_temperature);

    send_webhook("IRRIGATION_STARTED", data);
}

static void send_webhook_irrigation_stopped(const sensor_reading_t* reading, time_t duration_sec) {
    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "duration_minutes", duration_sec / 60);
    cJSON_AddStringToObject(data, "stop_reason", "HUMEDAD_ALCANZADA");
    cJSON_AddNumberToObject(data, "final_soil_humidity_avg",
        (reading->soil_humidity_1 + reading->soil_humidity_2 + reading->soil_humidity_3) / 3.0);

    send_webhook("IRRIGATION_STOPPED", data);
}

static void send_webhook_sensor_error(const char* sensor_id, int error_code) {
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "sensor_id", sensor_id);
    cJSON_AddNumberToObject(data, "error_code", error_code);

    send_webhook("SENSOR_ERROR", data);
}

static void send_webhook_valve_timeout(time_t duration_sec) {
    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "open_time_minutes", duration_sec / 60);
    cJSON_AddStringToObject(data, "alert", "Valve open > 40 minutes");

    send_webhook("VALVE_TIMEOUT", data);
}

static void send_webhook_temperature_critical(float temp) {
    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "current_temperature", temp);
    cJSON_AddNumberToObject(data, "threshold", 40.0);
    cJSON_AddStringToObject(data, "action", "IRRIGATION_STOPPED");

    send_webhook("TEMPERATURE_CRITICAL", data);
}
```

**Tareas**:
- [ ] Implementar función helper `send_webhook()`
- [ ] Implementar `send_webhook_irrigation_started()`
- [ ] Implementar `send_webhook_irrigation_stopped()`
- [ ] Implementar `send_webhook_sensor_error()`
- [ ] Implementar `send_webhook_valve_timeout()`
- [ ] Implementar `send_webhook_temperature_critical()`
- [ ] Testing: Verificar payloads JSON correctos
- [ ] Testing: Verificar que N8N recibe eventos

---

## FASE 4: Integración en main.c (Día 3)

### 4.1 Agregar includes
**Archivo**: `main/iot-soc-smart-irrigation.c`

```c
#include "irrigation_controller.h"
```

### 4.2 Inicialización en main
**Archivo**: `main/iot-soc-smart-irrigation.c`

```c
void app_main(void) {
    // ... init wifi, mqtt, sensor_reader ...

    // Inicializar irrigation controller
    if (irrigation_controller_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize irrigation controller");
        return;
    }

    if (irrigation_controller_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start irrigation controller");
        return;
    }

    // ... resto del código ...
}
```

**Tareas**:
- [ ] Agregar include
- [ ] Llamar `irrigation_controller_init()`
- [ ] Llamar `irrigation_controller_start()`
- [ ] Testing: Compilación sin errores

---

## FASE 5: Testing y Validación (Día 3-4)

### 5.1 Testing Online Mode
**Escenarios**:
- [ ] **Test 1: Inicio automático** - Humedad < 51% → válvula abre
- [ ] **Test 2: Parada normal** - TODOS >= 70% → válvula cierra
- [ ] **Test 3: Parada sobre-humedad** - ALGUNO >= 80% → cierra inmediato
- [ ] **Test 4: Timeout sesión** - Riego > 120 min → auto-stop
- [ ] **Test 5: Temperatura crítica** - T° > 40°C → detiene

### 5.2 Testing Offline Mode
**Escenarios**:
- [ ] **Test 6: Cambio online→offline** - WiFi desconecta → interval 2h
- [ ] **Test 7: Cambio offline→online** - WiFi conecta → vuelve a 60s
- [ ] **Test 8: Riego en offline** - Humedad < 51% → funciona igual

### 5.3 Testing MQTT Override
**Escenarios**:
- [ ] **Test 9: MQTT start** - Comando "start" → válvula abre
- [ ] **Test 10: MQTT stop** - Comando "stop" → válvula cierra
- [ ] **Test 11: MQTT timeout** - Sin comando > 30 min → vuelve IDLE

### 5.4 Testing Protecciones
**Escenarios**:
- [ ] **Test 12: Timeout válvula** - Abierta > 40 min → webhook alert
- [ ] **Test 13: Error sensor** - Lectura falla → estado ERROR, webhook
- [ ] **Test 14: Intervalo mínimo** - Esperar 4h entre sesiones

### 5.5 Testing Webhooks
**Escenarios**:
- [ ] **Test 15: Webhook riego iniciado** - Evento JSON correcto, N8N lo recibe
- [ ] **Test 16: Webhook riego detenido** - Evento JSON con duración
- [ ] **Test 17: Webhook sensor error** - Evento cuando falla lectura
- [ ] **Test 18: Webhook valve timeout** - Evento cuando > 40 min

### 5.6 Testing Recursos
**Escenarios**:
- [ ] **Test 19: Consumo RAM** - < 50 KB adicional
- [ ] **Test 20: Consumo Flash** - < 20 KB total componente

---

## 📋 CHECKLIST DE COMPLETITUD

### Fase 1 ✅
- [ ] Headers con tipos completos
- [ ] Constantes definidas
- [ ] CMakeLists.txt correcto
- [ ] Kconfig con todos los parámetros
- [ ] Compila sin errores

### Fase 2 ✅
- [ ] valve_driver implementado
- [ ] safety_watchdog implementado
- [ ] State machine core funcional
- [ ] Task principal ejecutándose
- [ ] Transiciones correctas entre estados

### Fase 3 ✅
- [ ] sensor_reader integrado
- [ ] wifi_manager integrado
- [ ] mqtt_client integrado (comandos)
- [ ] Webhooks N8N enviándose
- [ ] Payloads JSON correctos

### Fase 4 ✅
- [ ] irrigation_controller en main.c
- [ ] Inicialización correcta
- [ ] Sin conflictos con otros componentes

### Fase 5 ✅
- [ ] 20 tests pasando
- [ ] Consumo de recursos OK
- [ ] Documentación actualizada

---

## 🎯 CRITERIOS DE ÉXITO

| Criterio | Target | Estado |
|----------|--------|--------|
| **Compilación** | 0 errores | ⏳ |
| **Riego automático** | Funciona online | ⏳ |
| **Riego offline** | Funciona sin WiFi | ⏳ |
| **Control MQTT** | Start/stop remoto | ⏳ |
| **Webhooks** | 5 eventos a N8N | ⏳ |
| **Protecciones** | Todos timeouts funcionan | ⏳ |
| **Recursos** | < 50 KB RAM, < 20 KB Flash | ⏳ |
| **Testing** | 20/20 tests pasando | ⏳ |

---

## 📚 REFERENCIAS

- **Especificaciones**: Ver `Logica_de_riego.md`
- **Arquitectura**: Component-Based (ver `CLAUDE.md`)
- **Componentes relacionados**:
  - `sensor_reader` - Lecturas de sensores
  - `wifi_manager` - Detectar online/offline
  - `mqtt_client` - Comandos y webhooks
  - `device_config` - Configuración persistente

---

## ⚠️ NOTAS IMPORTANTES

### Phase 5 (Esta implementación)
✅ Incluir:
- State machine básico
- Control GPIO (LED simulador)
- Protecciones de seguridad (timeouts, temperatura)
- MQTT override (start/stop)
- Webhooks N8N (eventos principales)
- Online/offline detection

❌ NO Incluir (Phase 6):
- Deep sleep
- Ventana riego nocturno
- Histórico de sensores
- Failsafe con redundancia
- Retry automático webhooks
- Machine learning

### Configuración
- GPIO LED: `GPIO_NUM_21` (definitivo)
- N8N URL: Configurable en Kconfig (default: `http://192.168.1.177:5678/webhook/irrigation-events`)
- Umbrales: Todos configurables en Kconfig

### Seguridad
- Máximo 2h riego continuo
- Mínimo 4h entre sesiones
- Válvula abierta máx 40 min
- Auto-stop si T° > 40°C
- Auto-stop si ALGÚN sensor >= 80%

---

**Estatus Actual**: 🟢 LISTO PARA EMPEZAR
**Próximo Paso**: Iniciar Fase 1 - Crear estructura base

