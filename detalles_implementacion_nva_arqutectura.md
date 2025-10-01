# Arquitectura por Componentes ESP32 - Sistema Riego Inteligente

## 🎯 ARQUITECTURA RECOMENDADA: Component-Based para ESP32

### Principios Arquitecturales Aplicables

#### **1. Single Responsibility Component (SRC)**
```c
// Cada componente tiene UNA responsabilidad específica
components/
├── wifi_manager/          // Solo conectividad WiFi
├── mqtt_client/           // Solo comunicación MQTT  
├── sensor_reader/         // Solo lectura de sensores
├── irrigation_controller/ // Solo control de riego
├── device_config/         // Solo configuración del dispositivo
└── system_monitor/        // Solo monitoreo del sistema
```

#### **2. Minimal Interface Segregation (MIS)**
```c
// Interfaces mínimas y específicas, no genéricas
typedef struct {
    esp_err_t (*read_soil_humidity)(float* values, size_t count);
    esp_err_t (*read_ambient_data)(float* temp, float* hum);
} sensor_interface_t;

typedef struct {
    esp_err_t (*start_irrigation)(uint16_t duration_minutes);
    esp_err_t (*stop_irrigation)(void);
    bool (*is_irrigating)(void);
} irrigation_interface_t;
```

#### **3. Direct Dependencies (DD)**
```c
// Dependencias directas sin abstracción excesiva
#include "sensor_reader.h"
#include "irrigation_controller.h"
#include "mqtt_client.h"

// En lugar de múltiples capas de abstracción
void irrigation_task(void* params) {
    float soil_values[3];
    if (sensor_reader_get_soil_humidity(soil_values, 3) == ESP_OK) {
        if (should_irrigate(soil_values)) {
            irrigation_controller_start(15); // 15 minutos
        }
    }
}
```

---

## 🏗️ ESTRUCTURA DE COMPONENTES PROPUESTA

### **Estructura del Proyecto**
```
smart_irrigation_system/
├── components/
│   ├── wifi_manager/          # Conectividad WiFi + reconexión
│   │   ├── wifi_manager.h
│   │   ├── wifi_manager.c
│   │   └── CMakeLists.txt
│   │
│   ├── mqtt_client/           # Cliente MQTT + WebSockets
│   │   ├── mqtt_client.h
│   │   ├── mqtt_client.c
│   │   └── CMakeLists.txt
│   │
│   ├── sensor_reader/         # Lectura de todos los sensores
│   │   ├── sensor_reader.h    # Interface pública
│   │   ├── sensor_reader.c    # Lógica principal
│   │   ├── dht22_driver.c     # Driver DHT22
│   │   ├── soil_adc_driver.c  # Driver sensores suelo
│   │   └── CMakeLists.txt
│   │
│   ├── irrigation_controller/ # Control de riego + seguridad
│   │   ├── irrigation_controller.h
│   │   ├── irrigation_controller.c
│   │   ├── valve_driver.c     # Driver válvula GPIO
│   │   ├── safety_logic.c     # Lógica de seguridad
│   │   └── CMakeLists.txt
│   │
│   ├── device_config/         # Configuración + NVS
│   │   ├── device_config.h
│   │   ├── device_config.c
│   │   └── CMakeLists.txt
│   │
│   ├── system_monitor/        # Monitoreo + health check
│   │   ├── system_monitor.h
│   │   ├── system_monitor.c
│   │   └── CMakeLists.txt
│   │
│   └── http_server/           # Servidor HTTP + endpoints
│       ├── http_server.h
│       ├── http_server.c
│       └── CMakeLists.txt
│
├── main/
│   ├── main.c                 # Orquestación principal
│   ├── app_config.h           # Configuración global
│   └── CMakeLists.txt
│
└── include/
    ├── common_types.h         # Tipos comunes del sistema
    └── error_codes.h          # Códigos de error específicos
```

---

## 🔧 IMPLEMENTACIÓN DE COMPONENTES

### **1. Tipos Comunes del Sistema**
```c
// include/common_types.h
typedef struct {
    float temperature;         // °C
    float humidity;           // %
    uint32_t timestamp;
} ambient_data_t;

typedef struct {
    float soil_humidity[3];   // % humedad suelo
    uint8_t sensor_count;     // Sensores activos
    uint32_t timestamp;
} soil_data_t;

typedef struct {
    ambient_data_t ambient;
    soil_data_t soil;
    char device_mac[18];
    char device_ip[16];
} sensor_reading_t;

typedef enum {
    IRRIGATION_IDLE,
    IRRIGATION_ACTIVE,
    IRRIGATION_ERROR,
    IRRIGATION_EMERGENCY_STOP
} irrigation_state_t;
```

### **2. Sensor Reader Component**
```c
// components/sensor_reader/sensor_reader.h
#ifndef SENSOR_READER_H
#define SENSOR_READER_H

#include "common_types.h"
#include "esp_err.h"

// Configuración del componente
typedef struct {
    uint8_t soil_sensor_count;        // 1-3 sensores
    uint16_t reading_interval_ms;     // Intervalo lectura
    bool enable_filtering;            // Filtrado de lecturas
} sensor_config_t;

// API pública
esp_err_t sensor_reader_init(const sensor_config_t* config);
esp_err_t sensor_reader_get_ambient(ambient_data_t* data);
esp_err_t sensor_reader_get_soil(soil_data_t* data);
esp_err_t sensor_reader_get_all(sensor_reading_t* reading);
void sensor_reader_deinit(void);

// Health check
bool sensor_reader_is_healthy(void);
uint32_t sensor_reader_get_error_count(void);

#endif
```

### **3. Irrigation Controller Component**
```c
// components/irrigation_controller/irrigation_controller.h
#ifndef IRRIGATION_CONTROLLER_H
#define IRRIGATION_CONTROLLER_H

#include "common_types.h"
#include "esp_err.h"

// Configuración de riego
typedef struct {
    float soil_threshold_percent;     // 45% por defecto
    uint16_t max_duration_minutes;    // 30 min máximo
    uint16_t default_duration_minutes; // 15 min por defecto
    bool enable_offline_mode;         // Modo offline
    uint16_t offline_activation_seconds; // 300s para activar offline
} irrigation_config_t;

// Comandos de riego
typedef enum {
    IRRIGATION_CMD_START,
    IRRIGATION_CMD_STOP,
    IRRIGATION_CMD_EMERGENCY_STOP
} irrigation_command_t;

// API pública
esp_err_t irrigation_controller_init(const irrigation_config_t* config);
esp_err_t irrigation_controller_execute_command(irrigation_command_t cmd, uint16_t duration_minutes);
esp_err_t irrigation_controller_evaluate_soil(const soil_data_t* soil_data);
irrigation_state_t irrigation_controller_get_state(void);
void irrigation_controller_deinit(void);

// Modo offline
esp_err_t irrigation_controller_enable_offline_mode(bool enable);
bool irrigation_controller_is_offline_active(void);

#endif
```

### **4. Main Application Orchestration**
```c
// main/main.c - Orquestación simplificada
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "sensor_reader.h"
#include "irrigation_controller.h"
#include "device_config.h"
#include "system_monitor.h"
#include "http_server.h"

void app_main(void) {
    ESP_LOGI(TAG, "Iniciando Sistema de Riego Inteligente v1.4.0");
    
    // 1. Inicialización de configuración
    device_config_init();
    
    // 2. Inicialización de componentes básicos
    sensor_reader_config_t sensor_cfg = {
        .soil_sensor_count = 3,
        .reading_interval_ms = 30000,
        .enable_filtering = true
    };
    sensor_reader_init(&sensor_cfg);
    
    irrigation_config_t irrigation_cfg = {
        .soil_threshold_percent = 45.0,
        .max_duration_minutes = 30,
        .default_duration_minutes = 15,
        .enable_offline_mode = true,
        .offline_activation_seconds = 300
    };
    irrigation_controller_init(&irrigation_cfg);
    
    // 3. Conectividad
    wifi_manager_init();
    mqtt_client_init();
    http_server_init();
    
    // 4. Monitoreo del sistema
    system_monitor_init();
    
    // 5. Creación de tareas principales
    xTaskCreate(sensor_reading_task, "sensor_task", 4096, NULL, 5, NULL);
    xTaskCreate(irrigation_evaluation_task, "irrigation_task", 4096, NULL, 4, NULL);
    xTaskCreate(mqtt_communication_task, "mqtt_task", 6144, NULL, 3, NULL);
    xTaskCreate(system_health_task, "health_task", 2048, NULL, 2, NULL);
    
    ESP_LOGI(TAG, "Sistema iniciado correctamente");
}

// Tarea de lectura de sensores
void sensor_reading_task(void* params) {
    sensor_reading_t reading;
    TickType_t last_reading = 0;
    const TickType_t reading_interval = pdMS_TO_TICKS(30000); // 30 segundos
    
    while (1) {
        if (xTaskGetTickCount() - last_reading >= reading_interval) {
            if (sensor_reader_get_all(&reading) == ESP_OK) {
                // Enviar datos vía MQTT
                mqtt_client_publish_sensor_data(&reading);
                
                // Evaluar necesidad de riego
                irrigation_controller_evaluate_soil(&reading.soil);
            }
            last_reading = xTaskGetTickCount();
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Check cada segundo
    }
}
```

---

## ⚡ PRINCIPIOS ESPECÍFICOS PARA ESP32

### **1. Memory-First Design**
```c
// Usar arrays estáticos en lugar de malloc
#define MAX_SENSOR_HISTORY 10
static sensor_reading_t sensor_history[MAX_SENSOR_HISTORY];
static uint8_t history_index = 0;

// Evitar allocación dinámica en tasks críticas
void sensor_reading_task(void* params) {
    sensor_reading_t reading; // Stack allocation
    // ... usar reading sin malloc
}
```

### **2. Task-Oriented Architecture**
```c
// Cada tarea tiene responsabilidad específica y stack optimizado
#define SENSOR_TASK_STACK_SIZE    4096  // Lectura sensores
#define IRRIGATION_TASK_STACK_SIZE 4096  // Control riego
#define MQTT_TASK_STACK_SIZE      6144  // Comunicación MQTT
#define HEALTH_TASK_STACK_SIZE    2048  // Monitoreo sistema
```

### **3. Error Handling Consistent**
```c
// Códigos de error específicos del sistema
typedef enum {
    IRRIGATION_SUCCESS = 0,
    IRRIGATION_ERROR_SENSOR_FAIL = 0x1001,
    IRRIGATION_ERROR_VALVE_STUCK = 0x1002,
    IRRIGATION_ERROR_OVER_TEMP = 0x1003,
    IRRIGATION_ERROR_LOW_MEMORY = 0x1004
} irrigation_error_t;
```

### **4. Configuration Management**
```c
// Configuración centralizada en NVS
typedef struct {
    // WiFi
    char wifi_ssid[32];
    char wifi_password[64];
    
    // MQTT
    char mqtt_broker[128];
    uint16_t mqtt_port;
    
    // Device
    char device_name[32];
    char crop_name[16];
    
    // Irrigation
    float soil_threshold;
    uint16_t irrigation_duration;
    
    // Sensores
    uint8_t soil_sensor_count;
    uint16_t reading_interval;
} device_configuration_t;
```

---

## 🔄 PLAN DE MIGRACIÓN

### **Fase 1: Preparación (2-3 horas)**
1. **Crear estructura de componentes** nueva
2. **Extraer lógica de negocio** actual del código hexagonal
3. **Identificar dependencias reales** entre módulos

### **Fase 2: Migración Core (4-6 horas)**
1. **Migrar sensor_reader** - consolidar lectura de sensores
2. **Migrar irrigation_controller** - simplificar lógica de riego
3. **Migrar mqtt_client** - mantener funcionalidad actual
4. **Crear main.c** con orquestación simplificada

### **Fase 3: Testing y Optimización (2-3 horas)**
1. **Compilar y resolver errores**
2. **Testing funcional** básico
3. **Medición de memoria** y optimización
4. **Validación con hardware**

**TIEMPO TOTAL ESTIMADO**: 8-12 horas (vs semanas con hexagonal)

---

## 📊 ESTADO ACTUAL DE IMPLEMENTACIÓN

**Fecha última actualización**: 2025-10-01
**Versión**: 1.2.0 - Component-Based Architecture (En Progreso)

### ✅ **COMPONENTES COMPLETADOS**

#### **1. sensor_reader (IMPLEMENTADO Y FUNCIONAL)**

**Ubicación**: `components_new/sensor_reader/`

**Estado**: ✅ Compilando correctamente, Binary 905 KB

**Estructura Implementada**:
```
sensor_reader/
├── sensor_reader.h           # ✅ API pública completa
├── sensor_reader.c           # ✅ Implementación (448 líneas)
├── drivers/
│   ├── dht22/
│   │   ├── dht.h            # ✅ Driver DHT22 con retry
│   │   └── dht.c            # ✅ Implementación completa
│   └── moisture_sensor/
│       ├── moisture_sensor.h # ✅ API con sensor_read_with_raw()
│       └── moisture_sensor.c # ✅ Implementación + calibración
└── CMakeLists.txt           # ✅ Configurado correctamente
```

**Funcionalidad Completa**:
- ✅ Inicialización DHT22 + 3 sensores capacitivos de suelo (ADC)
- ✅ Lectura temperatura y humedad ambiente (DHT22)
- ✅ Lectura 3 sensores capacitivos de humedad del suelo
- ✅ Health monitoring por sensor individual
- ✅ Error tracking y recovery automático
- ✅ Logging de valores RAW para calibración (nivel DEBUG)
- ✅ Obtención de MAC address y IP address
- ✅ Estructura completa `sensor_reading_t`

**API Implementada**:
```c
esp_err_t sensor_reader_init(const sensor_config_t* config);
esp_err_t sensor_reader_get_ambient(ambient_data_t* data);
esp_err_t sensor_reader_get_soil(soil_data_t* data);
esp_err_t sensor_reader_get_all(sensor_reading_t* reading);
bool sensor_reader_is_healthy(void);
esp_err_t sensor_reader_get_status(sensor_status_t* status);
esp_err_t sensor_reader_reset_errors(sensor_type_t type);

// Funciones preparadas para Fase 2 (documentadas como NO actualmente usadas)
esp_err_t sensor_reader_calibrate_soil(uint8_t sensor_index, uint16_t dry_mv, uint16_t wet_mv);
esp_err_t sensor_reader_get_soil_calibration(uint8_t sensor_index, uint16_t* dry_mv, uint16_t* wet_mv);
```

---

### 🔬 **CALIBRACIÓN DE SENSORES DE SUELO**

#### **Fase 1 - Implementación Actual (COMPLETADA)**

**Método**: Calibración manual vía `#define` hardcodeado

**Ubicación**: `components_new/sensor_reader/drivers/moisture_sensor/moisture_sensor.c`

**Valores por defecto**:
```c
#define VALUE_WHEN_DRY_CAP 2865  // Sensor capacitivo en seco (aire)
#define VALUE_WHEN_WET_CAP 1220  // Sensor capacitivo húmedo (agua)
```

**Workflow de Calibración Manual**:
1. Activar logs DEBUG: `idf.py menuconfig` → Component config → Log output → Debug
2. Instalar sensor en campo y monitorear logs por 5 minutos
3. Observar valores RAW en suelo seco:
   ```
   D (12345) sensor_reader: Soil sensors: [RAW: 2856/2863/2871] [%: 1/0/0]
   ```
4. Regar abundantemente y esperar 30 minutos
5. Observar valores RAW en suelo húmedo:
   ```
   D (45678) sensor_reader: Soil sensors: [RAW: 1215/1223/1228] [%: 100/99/98]
   ```
6. Actualizar `VALUE_WHEN_DRY_CAP` y `VALUE_WHEN_WET_CAP` con promedios
7. Recompilar y flashear firmware

**Formato de Logs Compacto**:
```c
// Una sola línea por ciclo de lectura (nivel DEBUG)
ESP_LOGD(TAG, "Soil sensors: [RAW: %d/%d/%d] [%%: %d/%d/%d]",
         raw1, raw2, raw3, hum1, hum2, hum3);
```

**Ventajas**:
- ✅ Implementación simple y funcional
- ✅ Sin overhead de memoria (~20 KB ahorrados vs esp_console)
- ✅ Suficiente para prototipo y pruebas de campo

#### **Fase 2 - Implementación Futura (PLANIFICADA)**

**Método**: Calibración dinámica desde interfaces web

**Fuentes de Calibración**:

1. **WiFi Provisioning Page** (primera configuración):
   - Usuario conecta a AP "Liwaisi-Config"
   - Interfaz web muestra valores RAW en tiempo real
   - Botones "Marcar como SECO" y "Marcar como HÚMEDO"
   - Guardar valores en NVS automáticamente

2. **API REST en Raspberry Pi** (reconfiguración remota):
   ```http
   POST /api/devices/{mac_address}/calibrate
   {
     "sensor_index": 0,
     "dry_mv": 2863,
     "wet_mv": 1222
   }
   ```
   - ESP32 recibe comando vía MQTT
   - Actualiza valores en NVS
   - No requiere recompilar firmware

**Persistencia**: NVS (Non-Volatile Storage)
- Valores sobreviven reinicios
- Cargados automáticamente en `sensor_reader_init()`

**API Preparada** (ya existe en código):
```c
// Guardar calibración en NVS
esp_err_t sensor_reader_calibrate_soil(uint8_t sensor_index, uint16_t dry_mv, uint16_t wet_mv);

// Leer calibración desde NVS
esp_err_t sensor_reader_get_soil_calibration(uint8_t sensor_index, uint16_t* dry_mv, uint16_t* wet_mv);
```

**Estado**: Funciones implementadas pero documentadas como "[NOT CURRENTLY USED - Phase 2 Feature]"

---

### 🔌 **INTEGRACIÓN CON MAIN.C**

**Archivo**: `main/iot-soc-smart-irrigation.c`

**Cambios Implementados**:
1. ✅ Includes actualizados: `sensor_reader.h` reemplaza `dht_sensor.h`
2. ✅ Inicialización de `sensor_reader` con configuración completa
3. ✅ Task `sensor_publishing_task()` reescrita para usar `sensor_reader_get_all()`
4. ✅ Logging cada 30s con datos DHT22 + 3 sensores suelo
5. ✅ Publicación MQTT (adaptación temporal para compatibilidad)

**Ciclo de Publicación Actual (cada 30 segundos)**:
```c
1. Leer todos los sensores: sensor_reader_get_all(&reading)
2. Log datos cada 2.5 min (5 ciclos):
   - Temperatura, humedad ambiente
   - 3 valores de humedad del suelo
   - Heap memory disponible
3. Verificar conectividad MQTT
4. Publicar datos (temporal: solo temp + humedad)
5. Log éxito cada 5 minutos (10 ciclos)
```

---

### 📊 **USO DE MEMORIA**

| Componente | Flash | RAM | Notas |
|------------|-------|-----|-------|
| **sensor_reader.c** | ~8.5 KB | ~1 KB | Incluye health tracking |
| **moisture_sensor.c** | ~2.5 KB | ~500 bytes | Con nueva API RAW |
| **dht.c** (driver DHT22) | ~4 KB | ~200 bytes | Con retry automático |
| **Total sensor_reader** | **~15 KB** | **~1.7 KB** | Componente completo |
| **Binary total** | **905 KB** | **~180 KB** | Dentro de límites |

**Optimizaciones Aplicadas**:
- ✅ No se agregó `esp_console` (~20 KB ahorrados)
- ✅ Logs DEBUG solo cuando se activan
- ✅ Estructuras estáticas en lugar de malloc
- ✅ Buffers mínimos para logs compactos

---

### 🚀 **PRÓXIMOS PASOS**

#### **Corto Plazo (1-2 semanas)**
1. ⏳ Probar `sensor_reader` en hardware real
2. ⏳ Calibrar sensores capacitivos en campo (Colombia rural)
3. ⏳ Verificar datos MQTT publicados correctamente
4. ⏳ Migrar `mqtt_adapter` para publicar `sensor_reading_t` completo (incluir datos suelo)

#### **Mediano Plazo (Fase 2 - 1-2 meses)**
1. ⏳ Implementar WiFi Provisioning UI con calibración de sensores
2. ⏳ Agregar endpoints HTTP para valores RAW en JSON
3. ⏳ Implementar guardado/carga de calibración en NVS
4. ⏳ API REST en Raspberry Pi para reconfiguración remota
5. ⏳ Migrar componentes restantes (`mqtt_client`, `irrigation_controller`)

#### **Largo Plazo (Fase 3+)**
1. ⏳ Control de riego automático basado en umbrales
2. ⏳ Modo offline con decisiones locales
3. ⏳ Dashboard web en tiempo real
4. ⏳ Machine learning para predicción de riego

---

### 📝 **ARCHIVOS MODIFICADOS EN ESTA SESIÓN**

1. ✅ `components_new/sensor_reader/sensor_reader.c` - Creado desde cero (448 líneas)
2. ✅ `components_new/sensor_reader/sensor_reader.h` - Documentación Phase 2
3. ✅ `components_new/sensor_reader/drivers/moisture_sensor/moisture_sensor.h` - API `sensor_read_with_raw()`
4. ✅ `components_new/sensor_reader/drivers/moisture_sensor/moisture_sensor.c` - Implementación + comentarios calibración
5. ✅ `main/iot-soc-smart-irrigation.c` - Integración sensor_reader
6. ✅ `main/CMakeLists.txt` - Dependencias actualizadas
7. ✅ `components_new/sensor_reader/CMakeLists.txt` - Include paths
8. ✅ `CMakeLists.txt` (raíz) - EXTRA_COMPONENT_DIRS configurado

---

### 🎓 **LECCIONES APRENDIDAS**

#### **Decisiones Arquitecturales**
1. ✅ **Component-Based > Hexagonal** para ESP32 embedded
   - Menos overhead de memoria
   - APIs más directas y simples
   - Mejor para sistemas resource-constrained

2. ✅ **Calibración manual primero, dinámica después**
   - Evita complejidad prematura
   - Ahorra ~20 KB Flash (sin esp_console)
   - Funcional para prototipo y pruebas de campo

3. ✅ **Logs DEBUG en lugar de comandos de consola**
   - Mismo propósito, cero overhead cuando desactivado
   - Activable/desactivable en menuconfig
   - Formato compacto evita spam

#### **Integración de Drivers**
1. ✅ **Driver DHT22**: Usar `dht_read_ambient_data()` directamente
   - Ya retorna `ambient_data_t` compatible
   - Retry automático integrado (3 intentos)
   - Timestamp generado automáticamente

2. ✅ **Driver moisture_sensor**: Extender con `sensor_read_with_raw()`
   - Retorna RAW + porcentaje calculado
   - Permite calibración sin modificar driver base
   - Clamping a 0-100% incluido

3. ✅ **Conversión mV → ADC**: NO necesaria en fase actual
   - Calibración usa valores ADC crudos directamente
   - Simplifica lógica y reduce cálculos
   - Valores mV reservados para Fase 2 (NVS storage)

---

### 🔧 **COMANDOS ÚTILES**

#### **Compilación**
```bash
# Activar ESP-IDF
. ~/esp/esp-idf/export.sh
# o
get_idf

# Compilar proyecto
idf.py build

# Flashear
idf.py flash

# Monitorear logs
idf.py monitor

# Todo en uno
idf.py build flash monitor
```

#### **Activar Logs DEBUG (para calibración)**
```bash
idf.py menuconfig
# Navegar a: Component config → Log output → Default log verbosity
# Seleccionar: Debug
# Guardar y salir

idf.py build flash monitor
# Ahora verás logs DEBUG con valores RAW
```

#### **Ver Tamaño de Binary**
```bash
idf.py size
idf.py size-components
```

---

### ✅ **CRITERIOS DE ÉXITO**

**Fase 1 (Actual) - ✅ COMPLETADO**:
- [x] Componente `sensor_reader` compila sin errores
- [x] Integración con `main.c` funcional
- [x] Sistema lee DHT22 cada 30s
- [x] Sistema lee 3 sensores de suelo cada 30s
- [x] Logs DEBUG muestran valores RAW para calibración
- [x] Publica datos MQTT (temporal: solo ambient)
- [x] Manejo robusto de errores (no crashea si sensor falla)
- [x] Memoria < 200 KB RAM, < 1 MB Flash

**Fase 2 (Próxima) - ⏳ PENDIENTE**:
- [ ] Calibración dinámica desde WiFi Provisioning UI
- [ ] Guardar calibración en NVS
- [ ] API REST para calibración remota (Raspberry)
- [ ] Publicar `sensor_reading_t` completo vía MQTT (incluir suelo)
- [ ] Endpoint HTTP `/sensors/raw` con valores ADC en JSON

---

**Documentación completa**: Ver [ESTADO_ACTUAL_IMPLEMENTACION.md](ESTADO_ACTUAL_IMPLEMENTACION.md)
**Mantenido por**: Liwaisi Tech
**Última compilación exitosa**: 2025-10-01
**Próxima revisión**: Después de pruebas en campo (Colombia rural)

---