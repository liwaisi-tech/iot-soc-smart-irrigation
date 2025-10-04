# Estado Actual de Implementación - Sistema Riego Inteligente
**Fecha última actualización**: 2025-10-04
**Versión**: 1.2.0 - Component-Based Architecture (En Migración)

---

## 🎯 **DEVELOPMENT PHASES - ESTADO ACTUAL**

### **Phase 1: Basic Infrastructure** ✅ **COMPLETED**
- ✅ WiFi connectivity with reconnection
- ✅ MQTT client over WebSockets
- ✅ Basic HTTP server
- ✅ Semaphore system for concurrency

**Implementation**: `wifi_adapter/`, `mqtt_adapter/`, `http_adapter/`, `shared_resource_manager`

### **Phase 2: Data & Sensors** ✅ **COMPLETED**
- ✅ Sensor data structures
- ✅ Sensor abstraction layer
- ✅ Periodic sensor reading task

**Implementation**: `domain/value_objects/`, `dht_sensor/`, domain services
**Migration Status**: ✅ **sensor_reader** migrado a component-based (448 líneas)

### **Phase 3: Data Communication** ✅ **COMPLETED**
- ✅ Device registration message
- ✅ MQTT data publishing
- ✅ HTTP endpoints (/whoami, /sensors)

**Implementation**: `publish_sensor_data`, `json_device_serializer`, HTTP endpoints
**Migration Status**: ✅ **device_config** migrado a component-based (1090 líneas)

### **Phase 4: Irrigation Control** 🚧 **IN PROGRESS - BLOQUEADO**
- ⏳ MQTT command subscription **PENDIENTE**
- ⏳ Valve control system **PENDIENTE**
- ⏳ Offline automatic irrigation logic **PENDIENTE**

**⚠️ BLOQUEADO POR**: Análisis arquitectural de componentes migrados requerido

### **Phase 5: Optimization** ⏳ **PENDING**
- ⏳ Memory management & sleep modes
- ⏳ Final task scheduling system
- ⏳ Complete integration testing

---

## 🚧 **ESTADO DE MIGRACIÓN COMPONENT-BASED (40% completado)**

### **Estrategia de Migración**
- ✅ **Arquitectura Hexagonal** (components/): Funcional y operativa (respaldo)
- 🚧 **Arquitectura Component-Based** (components_new/): En migración activa
- ✅ **Sistema COMPILA** correctamente (binary 934 KB, 56% partition free)
- 🔄 **Sistema usa MIX** de hexagonal + component-based durante migración

### **⚠️ PRÓXIMA ACCIÓN CRÍTICA - ANÁLISIS ARQUITECTURAL**

**🔴 ANTES de continuar migración: VALIDAR componentes migrados**

**Objetivo**: Validar que componentes migrados cumplen con Principios Arquitecturales
**Referencia**: @detalles_implementacion_nva_arqutectura.md

#### **Principios a Validar**:

1. **Single Responsibility Component (SRC)**
   - ¿sensor_reader tiene UNA sola responsabilidad?
   - ¿device_config tiene UNA sola responsabilidad? ⚠️ (30+ funciones)

2. **Minimal Interface Segregation (MIS)**
   - ⚠️ device_config con 30+ funciones: ¿Viola MIS?
   - ¿Necesita subdivisión en sub-componentes?

3. **Direct Dependencies (DD)**
   - ¿Dependencias directas sin abstracción excesiva?

4. **Memory-First Design**
   - ¿Arrays estáticos en lugar de malloc?
   - ¿Stack allocation para datos temporales?

5. **Task-Oriented Architecture**
   - ¿Tareas con responsabilidad específica?
   - ¿Stack size optimizado?

#### **Acciones Requeridas**:

1. **[ ] Revisar sensor_reader** contra principios arquitecturales
2. **[ ] Revisar device_config** contra principios arquitecturales
3. **[ ] Documentar conclusiones** en este archivo
4. **[ ] Aplicar refactorings** si es necesario
5. **[ ] SOLO DESPUÉS**: Continuar con wifi_manager, mqtt_client, http_server

---

## 📦 **ESTADO DE COMPONENTES**

| Componente | Estado Migración | Prioridad | Origen |
|------------|------------------|-----------|--------|
| **sensor_reader** | ✅ COMPLETADO | - | components/infrastructure/drivers/dht_sensor |
| **device_config** | ✅ COMPLETADO | - | components/domain/services/device_config_service |
| **wifi_manager** | ⏳ PENDIENTE | 🔴 CRÍTICA (paso 1) | components/infrastructure/adapters/wifi_adapter |
| **mqtt_client** | ⏳ PENDIENTE | 🔴 CRÍTICA (paso 2) | components/infrastructure/adapters/mqtt_adapter |
| **http_server** | ⏳ PENDIENTE | 🟡 ALTA (paso 3) | components/infrastructure/adapters/http_adapter |
| **main.c** | ⏳ PENDIENTE | 🔴 CRÍTICA (paso 5) | Actualizar a components_new/ |
| **irrigation_controller** | ❌ NO INICIADO | 🟢 MEDIA (DESPUÉS validación) | Funcionalidad NUEVA |
| **system_monitor** | ❌ NO INICIADO | 🟢 BAJA (DESPUÉS validación) | Funcionalidad NUEVA |

---

## 📦 **COMPONENTES MIGRADOS**

### **1. sensor_reader (✅ COMPLETADO)**

**Ubicación**: `components_new/sensor_reader/`

**Estado**: Implementado y compilando correctamente

**Archivos**:
```
sensor_reader/
├── sensor_reader.h           ✅ Header completo con API pública
├── sensor_reader.c           ✅ Implementación completa (448 líneas)
├── drivers/
│   ├── dht22/
│   │   ├── dht.h            ✅ Driver DHT22 con retry automático
│   │   └── dht.c            ✅ Implementación completa
│   └── moisture_sensor/
│       ├── moisture_sensor.h ✅ API con sensor_read_with_raw()
│       └── moisture_sensor.c ✅ Implementación con logging RAW
└── CMakeLists.txt           ✅ Configurado correctamente
```

**Funcionalidad Implementada**:
- ✅ Inicialización DHT22 + 3 sensores de suelo (ADC)
- ✅ Lectura de temperatura y humedad ambiente (DHT22)
- ✅ Lectura de 3 sensores capacitivos de humedad del suelo
- ✅ Health monitoring por sensor individual
- ✅ Error tracking y recovery automático
- ✅ Logging de valores RAW para calibración (nivel DEBUG)
- ✅ Obtención de MAC address y IP address
- ✅ Estructura completa `sensor_reading_t`

**API Pública**:
```c
esp_err_t sensor_reader_init(const sensor_config_t* config);
esp_err_t sensor_reader_get_ambient(ambient_data_t* data);
esp_err_t sensor_reader_get_soil(soil_data_t* data);
esp_err_t sensor_reader_get_all(sensor_reading_t* reading);
bool sensor_reader_is_healthy(void);
esp_err_t sensor_reader_get_status(sensor_status_t* status);
esp_err_t sensor_reader_reset_errors(sensor_type_t type);

// Funciones preparadas para Fase 2 (NO actualmente usadas)
esp_err_t sensor_reader_calibrate_soil(uint8_t sensor_index, uint16_t dry_mv, uint16_t wet_mv);
esp_err_t sensor_reader_get_soil_calibration(uint8_t sensor_index, uint16_t* dry_mv, uint16_t* wet_mv);
```

---

## 🔬 **CALIBRACIÓN DE SENSORES DE SUELO**

### **Implementación Actual (Fase 1)**

**Método**: Calibración manual vía `#define` hardcodeado

**Ubicación**: `components_new/sensor_reader/drivers/moisture_sensor/moisture_sensor.c`

**Valores por defecto**:
```c
#define VALUE_WHEN_DRY_CAP 2865  // Sensor capacitivo en seco (aire)
#define VALUE_WHEN_WET_CAP 1220  // Sensor capacitivo húmedo (agua)
```

**Workflow de Calibración**:
1. Activar logs DEBUG: `idf.py menuconfig` → Component config → Log output → Debug
2. Instalar sensor en campo y monitorear logs por 5 minutos
3. Observar valores RAW en suelo seco en logs DEBUG:
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

**Formato de Logs DEBUG**:
```c
// Formato compacto - Una sola línea por ciclo de lectura
ESP_LOGD(TAG, "Soil sensors: [RAW: %d/%d/%d] [%%: %d/%d/%d]", ...)
```

### **Implementación Futura (Fase 2)**

**Método**: Calibración dinámica desde interfaces web

**Fuentes de calibración**:
1. **WiFi Provisioning Page** (primera configuración)
   - Usuario conecta a AP "Liwaisi-Config"
   - Interfaz web muestra valores RAW en tiempo real
   - Botones "Marcar como SECO" y "Marcar como HÚMEDO"
   - Guardar valores en NVS automáticamente

2. **API REST en Raspberry Pi** (reconfiguración remota)
   ```
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

**API preparada** (ya existe en código):
```c
// Guardar calibración en NVS
esp_err_t sensor_reader_calibrate_soil(uint8_t sensor_index, uint16_t dry_mv, uint16_t wet_mv);

// Leer calibración desde NVS
esp_err_t sensor_reader_get_soil_calibration(uint8_t sensor_index, uint16_t* dry_mv, uint16_t* wet_mv);
```

**Estado**: Funciones implementadas pero documentadas como "NO USADAS - Fase 2"

### **2. device_config (✅ COMPLETADO)**

**Ubicación**: `components_new/device_config/`

**Estado**: Implementado y compilando correctamente

**Archivos**:
```
device_config/
├── device_config.h           ✅ Header completo con API pública (445 líneas)
├── device_config.c           ✅ Implementación completa (1090 líneas)
└── CMakeLists.txt           ✅ Configurado correctamente
```

**Funcionalidad Implementada**:
- ✅ Inicialización y deinicialización NVS
- ✅ Device Info API (name, location, crop, firmware version, MAC address)
- ✅ WiFi Config API (SSID, password, is_configured status)
- ✅ MQTT Config API (broker URI, port, client ID generation)
- ✅ Irrigation Config API (soil threshold, max duration)
- ✅ Sensor Config API (sensor count, reading interval)
- ✅ System Management (load/save/reset/erase categories, factory reset)
- ✅ Thread-safe operations (mutex-protected)
- ✅ Default values for all configurations

**API Pública**:
```c
// Initialization
esp_err_t device_config_init(void);
esp_err_t device_config_deinit(void);

// Device Info
esp_err_t device_config_get_device_name(char* name, size_t name_len);
esp_err_t device_config_set_device_name(const char* name);
esp_err_t device_config_get_crop_name(char* crop, size_t crop_len);
esp_err_t device_config_set_crop_name(const char* crop);
esp_err_t device_config_get_device_location(char* location, size_t location_len);
esp_err_t device_config_set_device_location(const char* location);
esp_err_t device_config_get_firmware_version(char* version, size_t version_len);
esp_err_t device_config_get_mac_address(char* mac, size_t mac_len);
esp_err_t device_config_get_all(system_config_t* config);

// WiFi Config
esp_err_t device_config_get_wifi_ssid(char* ssid, size_t ssid_len);
esp_err_t device_config_get_wifi_password(char* password, size_t password_len);
esp_err_t device_config_set_wifi_credentials(const char* ssid, const char* password);
bool device_config_is_wifi_configured(void);

// MQTT Config
esp_err_t device_config_get_mqtt_broker(char* broker_uri, size_t uri_len);
esp_err_t device_config_get_mqtt_port(uint16_t* port);
esp_err_t device_config_get_mqtt_client_id(char* client_id, size_t client_id_len);
esp_err_t device_config_set_mqtt_broker(const char* broker_uri, uint16_t port);
bool device_config_is_mqtt_configured(void);

// Irrigation Config
esp_err_t device_config_get_soil_threshold(float* threshold);
esp_err_t device_config_set_soil_threshold(float threshold);
esp_err_t device_config_get_max_irrigation_duration(uint16_t* duration);
esp_err_t device_config_set_max_irrigation_duration(uint16_t duration);

// Sensor Config
esp_err_t device_config_get_soil_sensor_count(uint8_t* count);
esp_err_t device_config_set_soil_sensor_count(uint8_t count);
esp_err_t device_config_get_reading_interval(uint16_t* interval);
esp_err_t device_config_set_reading_interval(uint16_t interval);

// System Management
esp_err_t device_config_load(config_category_t category);
esp_err_t device_config_save(config_category_t category);
esp_err_t device_config_reset(config_category_t category);
esp_err_t device_config_erase_all(void);
esp_err_t device_config_get_status(config_status_t* status);
```

**Diferencias vs Hexagonal Architecture**:
- ❌ Removida abstracción de "domain service"
- ✅ Agregadas configuraciones WiFi (antes en wifi_adapter)
- ✅ Agregadas configuraciones MQTT (antes en mqtt_adapter)
- ✅ Agregadas configuraciones irrigation (NUEVO)
- ✅ Agregadas configuraciones sensor (NUEVO)
- ✅ Sistema de categorías para save/load selectivo
- ✅ Factory reset completo
- ✅ Thread-safe con mutex

---

## 🔄 **INTEGRACIÓN CON MAIN.C**

**Archivo**: `main/iot-soc-smart-irrigation.c`

**Cambios Implementados**:
1. ✅ Includes actualizados: `sensor_reader.h` reemplaza `dht_sensor.h`
2. ✅ Inicialización de `sensor_reader` con configuración completa
3. ✅ Task `sensor_publishing_task()` reescrita para usar `sensor_reader_get_all()`
4. ✅ Logging cada 30s con datos DHT22 + 3 sensores suelo
5. ✅ Publicación MQTT (adaptación temporal para compatibilidad)

**Ciclo de Publicación (cada 30 segundos)**:
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

## 📊 **USO DE MEMORIA**

| Componente | Flash | RAM | Notas |
|------------|-------|-----|-------|
| **sensor_reader.c** | ~8.5 KB | ~1 KB | Incluye health tracking |
| **moisture_sensor.c** | ~2.5 KB | ~500 bytes | Con nueva API RAW |
| **dht.c** (driver DHT22) | ~4 KB | ~200 bytes | Con retry automático |
| **device_config.c** | ~8 KB | ~1 KB | Config management + thread-safe |
| **Total migrados** | **~23 KB** | **~2.7 KB** | 2 componentes |
| **Binary total** | **934 KB** | **~180 KB** | Dentro de límites (56% free) |

**Optimizaciones Aplicadas**:
- ✅ No se agregó `esp_console` (~20 KB ahorrados)
- ✅ Logs DEBUG solo cuando se activan
- ✅ Estructuras estáticas en lugar de malloc
- ✅ Buffers mínimos para logs compactos

---

## 🔌 **HARDWARE CONFIGURADO**

### **GPIOs Asignados**
```c
// Definido en: include/common_types.h

// Sensor ambiental
#define GPIO_DHT22              18  // DHT22 data pin

// Sensores de humedad del suelo (ADC input-only)
#define ADC_SOIL_SENSOR_1       36  // ADC1_CH0 - Sensor 1
#define ADC_SOIL_SENSOR_2       39  // ADC1_CH3 - Sensor 2
#define ADC_SOIL_SENSOR_3       34  // ADC1_CH6 - Sensor 3

// Válvulas (para fase futura)
#define GPIO_VALVE_1            21  // Relay IN1
#define GPIO_VALVE_2            22  // Relay IN2

// LED de estado
#define GPIO_STATUS_LED         23  // Status LED
```

### **Configuración ADC**
```c
- Resolución: 12 bits (0-4095)
- Atenuación: ADC_ATTEN_DB_11 (rango 0-3.3V)
- Unidad: ADC_UNIT_1
- Tipo de sensor: Capacitivo (TYPE_CAP)
```

---

## 🚀 **PLAN DE MIGRACIÓN (PRÓXIMOS PASOS)**

### ✅ **COMPLETADO**
- [x] Componente sensor_reader migrado y funcional
- [x] Estructura components_new/ creada
- [x] common_types.h definido

---

### 🔴 **FASE 1: MIGRACIÓN DE COMPONENTES EXISTENTES** (Prioridad CRÍTICA)

**Objetivo**: Sistema funcional completo con arquitectura component-based usando código ya probado

**Duración Estimada**: 1-2 días
**Criterio de Éxito**: Sistema equivalente a arquitectura hexagonal compilando y funcionando

#### **Pasos de Migración**

1. **[ ] Migrar wifi_manager**
   - Origen: `components/infrastructure/adapters/wifi_adapter/`
   - Consolidar: wifi_adapter.c + wifi_connection_manager.c + wifi_prov_manager.c + boot_counter.c
   - Destino: `components_new/wifi_manager/wifi_manager.c` (1 archivo)
   - Funcionalidad: Provisioning, reconexión, NVS credentials
   - **Estado**: ⏳ PENDIENTE

2. **[ ] Migrar mqtt_client**
   - Origen: `components/infrastructure/adapters/mqtt_adapter/` + `components/application/use_cases/publish_sensor_data.c`
   - Consolidar: mqtt_adapter.c + mqtt_client_manager.c + publish_sensor_data.c
   - Destino: `components_new/mqtt_client/mqtt_client.c` (1 archivo)
   - Funcionalidad: WebSocket MQTT, publishing, registration
   - **Estado**: ⏳ PENDIENTE

3. **[ ] Migrar http_server**
   - Origen: `components/infrastructure/adapters/http_adapter/`
   - Consolidar: http_adapter.c + server.c + endpoints/*.c + middleware (simplificado)
   - Destino: `components_new/http_server/http_server.c` (1 archivo)
   - Funcionalidad: Endpoints /whoami, /sensors, /ping
   - **Estado**: ⏳ PENDIENTE

4. **[ ] Migrar device_config**
   - Origen: `components/domain/services/device_config_service/`
   - Simplificar: Remover abstracción de dominio
   - Destino: `components_new/device_config/device_config.c` (1 archivo)
   - Funcionalidad: NVS management directo
   - **Estado**: ⏳ PENDIENTE

5. **[ ] Actualizar main.c**
   - Cambiar includes: de components/ a components_new/
   - Simplificar inicialización: llamadas directas a componentes
   - Eliminar capas de abstracción hexagonal
   - **Estado**: ⏳ PENDIENTE

6. **[ ] Compilar sistema completo**
   - Resolver errores de compilación
   - Verificar tamaño de binary (objetivo: <1MB Flash, <200KB RAM)
   - **Estado**: ⏳ PENDIENTE

7. **[ ] Validar en hardware**
   - Flashear ESP32
   - Probar WiFi provisioning
   - Probar MQTT publishing (sensor_reader data)
   - Probar HTTP endpoints
   - **Criterio**: Sistema funciona igual que arquitectura hexagonal
   - **Estado**: ⏳ PENDIENTE

---

### 🟡 **FASE 2: NUEVAS FUNCIONALIDADES** (Después de Fase 1)

**Requisito**: Fase 1 completada y validada en hardware

**Objetivo**: Agregar control de riego y monitoreo avanzado

**Duración Estimada**: 2-3 semanas

#### **Pasos de Implementación**

8. **[ ] Implementar irrigation_controller** (NUEVO)
   - Crear desde cero basado en `Logica_de_riego.md`
   - Implementación: irrigation_controller.c + valve_driver.c + safety_logic.c
   - Funcionalidad: Control de válvulas, lógica de riego, modo offline
   - **Estado**: ❌ NO INICIADO

9. **[ ] Implementar system_monitor** (NUEVO)
   - Crear desde cero para health monitoring
   - Funcionalidad: Component health checks, memory monitoring
   - **Estado**: ❌ NO INICIADO

10. **[ ] Calibración dinámica de sensores**
    - WiFi Provisioning UI con calibración
    - Guardar/cargar calibración en NVS
    - API REST para reconfiguración remota
    - **Estado**: ❌ NO INICIADO

11. **[ ] Lógica de riego offline**
    - 4 modos adaptativos (Normal/Warning/Critical/Emergency)
    - Thresholds basados en dataset Colombia
    - Safety interlocks
    - **Estado**: ❌ NO INICIADO

---

### 🟢 **FASE 3: OPTIMIZACIÓN Y PRODUCCIÓN** (Largo Plazo)

12. **[ ] Optimización energética** (sleep modes)
13. **[ ] Testing completo** (unitario e integración)
14. **[ ] Dashboard web** en tiempo real
15. **[ ] Machine learning** para predicción de riego

---

## 📝 **DOCUMENTACIÓN ACTUALIZADA**

### **Archivos Modificados en Esta Sesión** (device_config migration)
1. ✅ `components_new/device_config/device_config.c` - Implementación completa (1090 líneas)
2. ✅ `components_new/device_config/device_config.h` - Ya existía (445 líneas)
3. ✅ `components_new/device_config/CMakeLists.txt` - Ya existía
4. ✅ `main/iot-soc-smart-irrigation.c` - Cambio include device_config_service.h → device_config.h
5. ✅ `main/CMakeLists.txt` - Agregada dependencia device_config
6. ✅ `CMakeLists.txt` (raíz) - Agregado device_config a EXTRA_COMPONENT_DIRS
7. ✅ `ESTADO_ACTUAL_IMPLEMENTACION.md` - Este archivo, actualizado con device_config

### **Documentación de Referencia**
- `CLAUDE.md` - Guía general del proyecto (arquitectura hexagonal)
- `detalles_implementacion_nva_arqutectura.md` - Arquitectura component-based
- `ESTADO_ACTUAL_IMPLEMENTACION.md` - Este archivo (estado actual)

---

## 🎓 **LECCIONES APRENDIDAS**

### **Decisiones de Migración**
1. 🔴 **MIGRAR PRIMERO, FEATURES DESPUÉS** (CRÍTICO)
   - ✅ Evita mezclar cambios de arquitectura con funcionalidad nueva
   - ✅ Permite validación incremental del sistema
   - ✅ Reduce riesgo al mantener components/ como respaldo
   - ✅ Facilita debugging al aislar problemas de migración vs implementación
   - ❌ NO probar hardware sin sistema compilable completo
   - ❌ NO implementar features nuevas antes de migrar existentes

### **Decisiones Arquitecturales**
2. ✅ **Component-Based > Hexagonal** para ESP32 embedded
   - Menos overhead de memoria
   - APIs más directas y simples
   - Mejor para sistemas resource-constrained

3. ✅ **Calibración manual primero, dinámica después**
   - Evita complejidad prematura
   - Ahorra ~20 KB Flash (sin esp_console)
   - Funcional para prototipo y pruebas de campo

4. ✅ **Logs DEBUG en lugar de comandos de consola**
   - Mismo propósito, cero overhead
   - Activable/desactivable en menuconfig
   - Formato compacto evita spam

### **Integración de Drivers**
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

## 🔧 **COMANDOS ÚTILES**

### **Compilación**
```bash
# Activar ESP-IDF
. ~/esp/esp-idf/export.sh

# Compilar proyecto
idf.py build

# Flashear
idf.py flash

# Monitorear logs
idf.py monitor

# Todo en uno
idf.py build flash monitor
```

### **Activar Logs DEBUG (para calibración)**
```bash
idf.py menuconfig
# Navegar a: Component config → Log output → Default log verbosity
# Seleccionar: Debug
# Guardar y salir

idf.py build flash monitor
# Ahora verás logs DEBUG con valores RAW
```

### **Ver Tamaño de Binary**
```bash
idf.py size
idf.py size-components
```

---

## ✅ **CRITERIOS DE ÉXITO**

### **Fase Actual: Migración Componentes Existentes - 🚧 EN PROGRESO (40% completado)**

#### ✅ **Componentes Migrados (2/5)**

**sensor_reader - ✅ COMPLETADO**
- [x] Componente `sensor_reader` migrado sin errores
- [x] Lectura DHT22 funcional
- [x] Lectura 3 sensores de suelo funcional
- [x] Logs DEBUG con valores RAW
- [x] Health monitoring implementado
- [x] Memoria optimizada

**device_config - ✅ COMPLETADO**
- [x] Componente `device_config` migrado sin errores (1090 líneas)
- [x] Device Info API completa (name, location, crop, version, MAC)
- [x] WiFi Config API completa (SSID, password, status)
- [x] MQTT Config API completa (broker, port, client_id)
- [x] Irrigation Config API (soil threshold, max duration)
- [x] Sensor Config API (sensor count, reading interval)
- [x] System Management (load/save/reset/erase, factory reset)
- [x] Thread-safe con mutex
- [x] Compilando sin errores
- [x] Integrado en main.c

### **Siguiente Fase: Migración Componentes Existentes - ⏳ PENDIENTE**
**Criterio de Éxito Global**: Sistema completo compila y funciona equivalente a arquitectura hexagonal

- [ ] wifi_manager migrado y compilando
- [ ] mqtt_client migrado y compilando
- [ ] http_server migrado y compilando
- [ ] device_config migrado y compilando
- [ ] main.c actualizado a components_new/
- [ ] Sistema compila sin errores
- [ ] WiFi provisioning funcional en hardware
- [ ] MQTT publishing funcional en hardware
- [ ] HTTP endpoints respondiendo en hardware
- [ ] Memoria < 200 KB RAM, < 1 MB Flash
- [ ] Sistema estable 24+ horas

### **Fase Futura: Nuevas Funcionalidades - ❌ NO INICIADO**
- [ ] irrigation_controller implementado
- [ ] system_monitor implementado
- [ ] Calibración dinámica de sensores
- [ ] Lógica de riego offline

---

**Mantenido por**: Liwaisi Tech
**Última actualización**: 2025-10-04

**Development Phases Status**:
- ✅ Phase 1: Basic Infrastructure - COMPLETED
- ✅ Phase 2: Data & Sensors - COMPLETED
- ✅ Phase 3: Data Communication - COMPLETED
- 🚧 Phase 4: Irrigation Control - BLOQUEADO (requiere análisis arquitectural)
- ⏳ Phase 5: Optimization - PENDING

**Progreso Migración Component-Based**: 40% completado (2/5 componentes migrados)

**⚠️ PRÓXIMA ACCIÓN CRÍTICA**:
🔴 **ANÁLISIS ARQUITECTURAL** de componentes migrados (sensor_reader, device_config)
- Validar cumplimiento de Principios Arquitecturales
- Documentar hallazgos y conclusiones
- Aplicar refactorings si es necesario

**SOLO DESPUÉS del análisis**: Continuar con wifi_manager, mqtt_client, http_server

**Próximo Milestone**: Phase 4 - Irrigation Control (bloqueado hasta completar análisis)
