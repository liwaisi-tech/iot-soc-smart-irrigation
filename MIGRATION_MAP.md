# Migration Map: Hexagonal Architecture → Component-Based Architecture

## Overview

This document maps the migration from the current hexagonal architecture to the new component-based architecture for the Smart Irrigation System.

**Migration Strategy:** Incremental migration - new architecture in `components_new/` directory

**Date Updated:** 2025-10-03
**Version:** From v1.1.0 (Hexagonal) → v2.0.0 (Component-Based)

---

## 🔴 MIGRATION PRINCIPLE

### **MIGRATE EXISTING FIRST, NEW FEATURES SECOND**

**Current Status**: Only `sensor_reader` component migrated

**Critical Rule**:
✅ Complete migration of ALL existing components BEFORE implementing new features
❌ DO NOT implement `irrigation_controller` or `system_monitor` until migration is complete

**Rationale**:
1. Validates component-based architecture with proven code
2. Isolates migration issues from new feature bugs
3. Maintains `components/` as working fallback
4. Enables incremental testing and validation

---

## Architecture Comparison

### Before: Hexagonal Architecture (3 Layers)
```
components/
├── domain/              # Pure business logic (8 files)
│   ├── entities/
│   ├── value_objects/
│   ├── repositories/
│   └── services/
├── application/         # Use cases (2 files)
│   ├── use_cases/
│   ├── dtos/
│   └── ports/
└── infrastructure/      # Adapters & drivers (33 files)
    ├── adapters/
    │   ├── wifi_adapter/
    │   ├── mqtt_adapter/
    │   └── http_adapter/
    └── drivers/
        └── dht_sensor/
```

**Issues:**
- Excessive abstraction for embedded systems
- 43 total source files with complex dependencies
- Memory overhead from abstraction layers
- Difficult to navigate for ESP32 development

### After: Component-Based Architecture (7 Components)
```
components_new/
├── wifi_manager/        # WiFi connectivity (1-2 files)
├── mqtt_client/         # MQTT communication (1 file)
├── sensor_reader/       # All sensors unified (3 files)
├── irrigation_controller/ # NEW - irrigation logic (3 files)
├── device_config/       # Configuration management (1 file)
├── system_monitor/      # NEW - health monitoring (1 file)
└── http_server/         # HTTP REST API (1 file)
```

**Benefits:**
- Direct dependencies, minimal abstraction
- ~15 total source files (65% reduction)
- Single Responsibility per component
- ESP32-optimized architecture

---

## Component Migration Matrix

| Prioridad | Old (Hexagonal) | New (Component-Based) | Files to Migrate | Status |
|-----------|-----------------|----------------------|------------------|--------|
| **COMPLETADO** | `infrastructure/drivers/dht_sensor/` + soil drivers | `sensor_reader/` | dht_sensor.c + moisture drivers | ✅ Migrado |
| 🔴 **Paso 1** | `infrastructure/adapters/wifi_adapter/` | `wifi_manager/` | wifi_adapter.c, wifi_connection_manager.c, wifi_prov_manager.c, boot_counter.c | ⏳ Pendiente |
| 🔴 **Paso 2** | `infrastructure/adapters/mqtt_adapter/` + `application/use_cases/publish_sensor_data.c` | `mqtt_client/` | mqtt_adapter.c, mqtt_client_manager.c, publish_sensor_data.c | ⏳ Pendiente |
| 🟡 **Paso 3** | `infrastructure/adapters/http_adapter/` | `http_server/` | http_adapter.c, server.c, endpoints/*.c | ⏳ Pendiente |
| 🟡 **Paso 4** | `domain/services/device_config_service.c` | `device_config/` | device_config_service.c | ⏳ Pendiente |
| 🔴 **Paso 5** | `main/iot-soc-smart-irrigation.c` | Actualizar a components_new/ | main.c | ⏳ Pendiente |
| **DESPUÉS** | **NEW** - Funcionalidad NUEVA | `irrigation_controller/` | **NEW implementation** con Logica_de_riego.md | ❌ NO Iniciado |
| **DESPUÉS** | **NEW** - Funcionalidad NUEVA | `system_monitor/` | **NEW implementation** | ❌ NO Iniciado |
| Integrar | `infrastructure/adapters/json_device_serializer.c` | Integrar en `mqtt_client/` y `http_server/` | json_device_serializer.c | ⏳ Pendiente |
| Eliminar | `infrastructure/adapters/shared_resource_manager.c` | Usar semáforos directos | shared_resource_manager.c | ⏳ Pendiente |

---

## Detailed Component Migration Plans

### 1. wifi_manager Component

**Source Files (Hexagonal):**
```
infrastructure/adapters/wifi_adapter/
├── src/wifi_adapter.c              → wifi_manager.c (core logic)
├── src/wifi_connection_manager.c   → wifi_manager.c (merge reconnection)
├── src/wifi_prov_manager.c         → wifi_manager.c (merge provisioning)
├── src/boot_counter.c              → wifi_manager.c (merge boot detection)
└── include/wifi_adapter.h          → DELETE (replaced by wifi_manager.h)
```

**Migration Steps:**
1. ✅ Create `wifi_manager.h` with simplified API
2. ✅ Create `CMakeLists.txt` with dependencies
3. ⏳ Create `wifi_manager.c` merging all 4 source files
4. ⏳ Remove abstraction layers (direct WiFi stack calls)
5. ⏳ Keep provisioning mode for rural deployment
6. ⏳ Implement exponential backoff reconnection

**Key Changes:**
- Consolidate 4 files → 1 file
- Remove adapter pattern abstraction
- Keep: Provisioning, reconnection, NVS credentials
- Remove: Unnecessary event forwarding layers

---

### 2. mqtt_client Component

**Source Files (Hexagonal):**
```
infrastructure/adapters/mqtt_adapter/
├── src/mqtt_adapter.c              → mqtt_client.c (core)
├── src/mqtt_client_manager.c       → mqtt_client.c (merge)
└── include/mqtt_adapter.h          → DELETE

application/use_cases/
└── publish_sensor_data.c           → mqtt_client.c (merge publishing logic)

infrastructure/adapters/
└── json_device_serializer.c        → mqtt_client.c (merge JSON serialization)
```

**Migration Steps:**
1. ✅ Create `mqtt_client.h` with unified API
2. ✅ Create `CMakeLists.txt`
3. ⏳ Create `mqtt_client.c` merging 4 source files
4. ⏳ Integrate JSON serialization directly (no separate component)
5. ⏳ Implement device registration publishing
6. ⏳ Implement sensor data publishing
7. ⏳ Implement irrigation command subscription

**Key Changes:**
- Consolidate 4 files → 1 file
- Merge use case logic into component
- Direct JSON formatting (cJSON library)
- Keep: WebSocket support, QoS 1, auto-reconnect

---

### 3. sensor_reader Component

**Source Files (Hexagonal):**
```
infrastructure/drivers/dht_sensor/
└── src/dht_sensor.c                → sensor_reader/dht22_driver.c

domain/value_objects/
├── ambient_sensor_data.h           → DELETE (replaced by common_types.h)
└── soil_sensor_data.h              → DELETE (replaced by common_types.h)

EXTERNAL (to import):
├── soil_adc_driver.c               → sensor_reader/soil_adc_driver.c (IMPORT)
└── soil_calibration.c              → sensor_reader/soil_adc_driver.c (IMPORT)
```

**Migration Steps:**
1. ✅ Create `sensor_reader.h` with unified sensor API
2. ✅ Create `CMakeLists.txt` with ADC dependencies
3. ⏳ Migrate `dht_sensor.c` → `dht22_driver.c`
4. ⏳ **IMPORT external soil sensor drivers** (see EXTERNAL_DRIVERS.md)
5. ⏳ Create `sensor_reader.c` orchestrating both sensor types
6. ⏳ Implement filtering and validation logic
7. ⏳ Implement health monitoring per sensor

**Key Changes:**
- Unified interface for all sensors
- Direct ADC usage for soil sensors
- Remove value objects (use common_types.h)
- Add calibration management

**External Dependencies:**
- DHT22 driver: Already implemented ✅
- Soil ADC driver: **NEEDS IMPORT** ⚠️

---

### 4. irrigation_controller Component (NEW)

**Source Files:**
- **NO MIGRATION** - Completely new implementation

**Logic Sources:**
```
docs/
├── Logica_de_riego.md              → Business rules source
└── CLAUDE.md                       → GPIO configuration
```

**Implementation Files:**
```
irrigation_controller/
├── irrigation_controller.c         → Main control logic
├── valve_driver.c                  → GPIO relay control
└── safety_logic.c                  → Safety interlocks
```

**Implementation Steps:**
1. ✅ Create `irrigation_controller.h` with complete API
2. ✅ Create `CMakeLists.txt`
3. ⏳ Implement `valve_driver.c`:
   - GPIO 21, 22 for relay control
   - Active HIGH relay control
   - Valve state tracking
4. ⏳ Implement `safety_logic.c`:
   - 2h max session duration
   - 4h minimum interval between sessions
   - Thermal protection (>40°C auto-stop)
   - Over-irrigation protection (>80% auto-stop)
5. ⏳ Implement `irrigation_controller.c`:
   - Decision logic (Colombian dataset thresholds)
   - Online mode: MQTT command execution
   - Offline mode: 4-level adaptive irrigation
   - Session management and statistics

**Key Features:**
- **Thresholds:** 30% critical, 75% optimal, 80% max
- **Safety:** Thermal protection, duration limits, interval enforcement
- **Offline Modes:** Normal (2h), Warning (1h), Critical (30min), Emergency (15min)
- **Statistics:** Session history, runtime tracking

---

### 5. device_config Component

**Source Files (Hexagonal):**
```
domain/services/
├── device_config_service.c         → device_config.c
└── device_config_service.h         → DELETE (replaced)
```

**Migration Steps:**
1. ✅ Create `device_config.h` with NVS API
2. ✅ Create `CMakeLists.txt`
3. ⏳ Migrate `device_config_service.c` → `device_config.c`
4. ⏳ Simplify API (remove domain service abstraction)
5. ⏳ Add configuration categories (wifi, mqtt, irrigation, sensors)
6. ⏳ Implement factory reset functionality

**Key Changes:**
- Remove domain service abstraction
- Direct NVS operations
- Centralized configuration management
- Category-based save/load

---

### 6. system_monitor Component (NEW)

**Source Files:**
- **NO MIGRATION** - Completely new implementation

**Implementation Files:**
```
system_monitor/
└── system_monitor.c                → Complete monitoring logic
```

**Implementation Steps:**
1. ✅ Create `system_monitor.h` with health check API
2. ✅ Create `CMakeLists.txt`
3. ⏳ Implement component health checks:
   - WiFi manager status
   - MQTT client status
   - Sensor reader status
   - Irrigation controller status
4. ⏳ Implement connectivity monitoring:
   - WiFi connection state
   - MQTT connection state
   - Combined connectivity status
5. ⏳ Implement memory monitoring:
   - Free heap tracking
   - Minimum heap tracking
   - Memory warnings/alerts
6. ⏳ Implement event logging:
   - Component events
   - System events
   - Circular buffer for last 20 events

**Key Features:**
- Periodic health checks (60s interval)
- Memory monitoring (30s interval)
- Auto-recovery on component failures
- Event log for diagnostics

---

### 7. http_server Component

**Source Files (Hexagonal):**
```
infrastructure/adapters/http_adapter/
├── src/http_adapter.c              → http_server.c (merge all)
├── src/http/server.c               → http_server.c (merge)
├── src/http/endpoints/whoami.c     → http_server.c (merge)
├── src/http/endpoints/temperature_humidity.c → http_server.c (merge)
├── src/http/endpoints/ping.c       → http_server.c (merge)
├── src/http/middleware/logging_middleware.c → http_server.c (simple logging)
└── src/http/middleware/middleware_manager.c → REMOVE (not needed)

infrastructure/adapters/
└── json_device_serializer.c        → http_server.c (merge)
```

**Migration Steps:**
1. ✅ Create `http_server.h` with endpoint definitions
2. ✅ Create `CMakeLists.txt`
3. ⏳ Create `http_server.c` merging all endpoints
4. ⏳ Implement endpoints:
   - `/whoami` - Device info
   - `/sensors` - Current readings
   - `/ping` - Health check
   - `/status` - System status
   - `/irrigation` - Irrigation status
   - `/config` - Device configuration
5. ⏳ Remove middleware abstraction (direct logging)
6. ⏳ Add CORS headers for web clients

**Key Changes:**
- Consolidate 7 files → 1 file
- Remove middleware pattern (overkill for ESP32)
- Direct endpoint handlers
- Keep: JSON responses, request logging

---

## Domain Layer Removal

**Files to Remove (No Migration):**

```
domain/
├── entities/                       → REMOVE (not needed)
│   ├── sensor.h
│   ├── irrigation.h
│   └── device.h
├── value_objects/                  → REPLACE with common_types.h
│   ├── ambient_sensor_data.h
│   ├── soil_sensor_data.h
│   ├── complete_sensor_data.h
│   ├── device_info.h
│   └── device_registration_message.h
├── repositories/                   → REMOVE (direct NVS access)
└── services/                       → MIGRATE to components
    ├── irrigation_logic.h          → irrigation_controller component
    ├── sensor_manager.h            → sensor_reader component
    └── device_config_service.h     → device_config component
```

**Rationale:**
- Value objects → Replaced by `common_types.h` (single source of truth)
- Entities → Not needed (direct structs in common_types.h)
- Repositories → Overkill (direct NVS operations in components)
- Services → Business logic moved to respective components

---

## Application Layer Removal

**Files to Remove/Migrate:**

```
application/
├── use_cases/
│   └── publish_sensor_data.c       → MIGRATE to mqtt_client component
├── dtos/                           → REMOVE (use common_types.h)
└── ports/                          → REMOVE (direct dependencies)
```

**Rationale:**
- Use cases → Integrated into components (no separate layer)
- DTOs → Not needed (use common types directly)
- Ports → Component-based uses direct dependencies

---

## Shared Utilities Handling

### JSON Serialization
**Old:** `infrastructure/adapters/json_device_serializer.c`
**New:** Integrated into `mqtt_client` and `http_server` components

**Why:** JSON formatting is simple enough to embed directly in components that use it

### Resource Management
**Old:** `infrastructure/adapters/shared_resource_manager.c`
**New:** Direct semaphore usage in components that need synchronization

**Why:** Abstraction adds complexity without value for ESP32

---

## Data Flow Changes

### Before (Hexagonal):
```
main.c → Use Case → Domain Service → Repository Interface → Adapter → ESP-IDF
```

### After (Component-Based):
```
main.c → Component → ESP-IDF
```

**Example Flow: Sensor Reading + Publishing**

**Before:**
```
sensor_publishing_task()
  → publish_sensor_data_use_case()
    → sensor_manager_read() [domain service]
      → dht_sensor_read() [infrastructure driver]
    → json_device_serializer_serialize() [infrastructure adapter]
    → mqtt_adapter_publish() [infrastructure adapter]
      → mqtt_client_manager_publish() [infrastructure internal]
```

**After:**
```
sensor_publishing_task()
  → sensor_reader_get_all()  [direct component call]
  → mqtt_client_publish_sensor_data()  [direct component call]
```

**Result:** 7 function calls → 2 function calls

---

## New main.c Structure

```c
// main/main.c - Component-Based
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "sensor_reader.h"
#include "irrigation_controller.h"
#include "device_config.h"
#include "system_monitor.h"
#include "http_server.h"

void app_main(void) {
    // 1. Initialize base system
    nvs_flash_init();
    esp_event_loop_create_default();

    // 2. Initialize components
    device_config_init();
    sensor_reader_init(NULL);
    irrigation_controller_init(NULL);
    wifi_manager_init();
    mqtt_client_init();
    http_server_init(NULL);
    system_monitor_init(NULL);

    // 3. Start components
    wifi_manager_start();
    mqtt_client_start();
    http_server_start();
    system_monitor_start();

    // 4. Create application tasks
    xTaskCreate(sensor_reading_task, "sensor", 4096, NULL, 5, NULL);
    xTaskCreate(irrigation_task, "irrigation", 4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "System ready");
}

// Sensor reading task (direct calls)
void sensor_reading_task(void* params) {
    sensor_reading_t reading;
    while (1) {
        sensor_reader_get_all(&reading);
        mqtt_client_publish_sensor_data(&reading);
        irrigation_controller_evaluate_and_act(&reading.soil, &reading.ambient, NULL);
        vTaskDelay(pdMS_TO_TICKS(60000)); // 60s
    }
}
```

**Key Differences:**
- Direct component initialization (no adapters)
- Simplified task implementation
- Clear data flow: sensor → mqtt + irrigation

---

## Memory Impact Analysis

### Before (Hexagonal):
- **Total Files:** 43 source files
- **Memory Overhead:** ~30KB from abstraction layers
- **Stack Usage:** Multiple function call overhead
- **Heap Usage:** Dynamic allocation in adapters

### After (Component-Based):
- **Total Files:** ~15 source files (65% reduction)
- **Memory Overhead:** ~5KB (direct calls)
- **Stack Usage:** Reduced call depth
- **Heap Usage:** Static allocation preferred

**Expected Savings:** ~25KB RAM, ~30KB Flash

---

## Testing Strategy

### Phase 1: Individual Component Testing
1. Test each component in isolation
2. Mock dependencies as needed
3. Verify component health checks

### Phase 2: Integration Testing
1. Test component interactions (sensor → mqtt)
2. Test offline mode transitions
3. Test safety interlocks

### Phase 3: System Testing
1. Full system integration
2. Long-term stability testing
3. Power consumption validation

---

## Migration Checklist

### ✅ Preparación (COMPLETADO)
- [x] Create `components_new/` structure
- [x] Create all component headers (.h files)
- [x] Create all CMakeLists.txt files
- [x] Create common_types.h
- [x] Implement sensor_reader.c (+ moisture sensor drivers)

### 🔴 FASE 1: Migrar Componentes Existentes (CRÍTICO - EN PROGRESO)
- [ ] **Paso 1**: Implement wifi_manager.c
- [ ] **Paso 2**: Implement mqtt_client.c
- [ ] **Paso 3**: Implement http_server.c
- [ ] **Paso 4**: Implement device_config.c
- [ ] **Paso 5**: Update main.c to use components_new/ exclusively
- [ ] **Paso 6**: Test compilation (sistema completo compila)
- [ ] **Paso 7**: Validate on hardware (WiFi + MQTT + HTTP functional)

### 🟡 FASE 2: Nuevas Funcionalidades (DESPUÉS DE FASE 1)
- [ ] Implement irrigation_controller.c (NUEVO - no migración)
- [ ] Implement system_monitor.c (NUEVO - no migración)
- [ ] Test offline irrigation logic
- [ ] Test safety interlocks

### 🟢 FASE 3: Limpieza Final
- [ ] Performance validation (memoria, estabilidad)
- [ ] Remove old `components/` directory (solo después de validar todo)
- [ ] Update root CMakeLists.txt (final cleanup)

---

## Timeline Estimate (ACTUALIZADO)

| Phase | Task | Duration | Status |
|-------|------|----------|--------|
| Prep | Structure & Headers + sensor_reader | 3 hours | ✅ COMPLETADO |
| **FASE 1** | **Migración Componentes Existentes** | **8-12 horas** | **⏳ PENDIENTE** |
| 1.1 | wifi_manager migration | 2-3 hours | ⏳ Pendiente |
| 1.2 | mqtt_client migration | 2-3 hours | ⏳ Pendiente |
| 1.3 | http_server migration | 2-3 hours | ⏳ Pendiente |
| 1.4 | device_config migration | 1 hour | ⏳ Pendiente |
| 1.5 | main.c update + compilation | 1-2 hours | ⏳ Pendiente |
| 1.6 | Hardware validation | 2 hours | ⏳ Pendiente |
| **FASE 2** | **Nuevas Funcionalidades** | **12-16 horas** | **❌ NO INICIADO** |
| 2.1 | irrigation_controller (NEW) | 6-8 hours | ❌ NO Iniciado |
| 2.2 | system_monitor (NEW) | 3-4 hours | ❌ NO Iniciado |
| 2.3 | Testing & validation | 3-4 hours | ❌ NO Iniciado |
| **FASE 3** | **Cleanup & Optimization** | **2-4 horas** | **❌ NO INICIADO** |
| **Total** | | **~30 hours** | **10% completado** |

---

## Risk Mitigation

**Risks:**
1. Sensor driver compatibility issues
2. MQTT WebSocket connection stability
3. Memory constraints during transition

**Mitigation:**
1. Test drivers individually before integration
2. Keep original code until new architecture proven stable
3. Monitor memory usage at each migration step
4. Maintain `components/` directory until full validation

---

## Success Criteria

### ✅ FASE 1 - Migración Componentes Existentes (CRÍTICO)

**Criterio de Éxito**: Sistema funciona IGUAL que arquitectura hexagonal

- [ ] Todos los componentes existentes migrados (wifi, mqtt, http, config)
- [ ] Sistema compila sin errores
- [ ] WiFi provisioning funcional en hardware
- [ ] MQTT publishing funcional en hardware
- [ ] HTTP endpoints respondiendo en hardware
- [ ] Memoria usage <200KB RAM, <1MB Flash
- [ ] Sistema estable 24+ horas
- [ ] No regressions vs arquitectura hexagonal

### 🟡 FASE 2 - Nuevas Funcionalidades (DESPUÉS)

**Criterio de Éxito**: Features nuevas implementadas y funcionando

- [ ] irrigation_controller implementado y probado
- [ ] system_monitor implementado y probado
- [ ] Lógica de riego offline funcional (4 modos)
- [ ] Safety interlocks validados
- [ ] Calibración dinámica de sensores funcional

### 🟢 FASE 3 - Limpieza Final

- [ ] Arquitectura limpia, components/ removido
- [ ] Documentación actualizada
- [ ] Performance validated
- [ ] Ready for production

---

## Post-Migration Improvements - DHT22 Driver Optimization

### Overview

The DHT22 driver has been migrated with **NIVEL 1** improvements (wrapper + retry logic) implemented. This section documents planned improvements for **NIVEL 2-4** to be executed after the complete component-based migration is stable.

**Current Implementation Status:**
- ✅ **NIVEL 1 COMPLETED:** `dht_read_ambient_data()` wrapper with automatic retry (3 attempts, 2.5s backoff)
- ⏳ **NIVEL 2-4:** Planned for post-migration optimization

---

### Phase 1: NIVEL 2 - Production Readiness (HIGH PRIORITY) 🔴

**When to Execute:**
- Trigger: Component migration completed and tested
- Trigger: First field deployment scheduled
- Trigger: Rural connectivity issues observed in testing

**Duration:** 3 hours
**Impact:** 🔴 **CRITICAL** - Essential for reliable field operation

#### 2.1 Intelligent Throttling & Cache System
**Problem:** DHT22 requires minimum 2s between reads, rapid calls cause sensor lockup
**Expected Impact:** Prevents 90% of timing errors, reduces power consumption

#### 2.2 Range Validation
**Problem:** Corrupted readings can pass checksum but contain impossible values
**Expected Impact:** Eliminates 100% of out-of-range readings affecting irrigation decisions

---

### Phase 2: NIVEL 3 - Field Optimization (MEDIUM PRIORITY) 🟡

**When to Execute:**
- After 1 week of production data
- Retry statistics show optimization opportunities

**Duration:** 2 hours
**Impact:** 🟡 **MEDIUM** - Enables remote tuning and diagnostics

#### 3.1 Dynamic Configuration
**Problem:** Different field conditions need different retry strategies
**Expected Impact:** 30% reduction in false errors via field-specific tuning

#### 3.2 Enhanced Diagnostic Logging
**Problem:** Remote troubleshooting is difficult without detailed timing data
**Expected Impact:** 60% reduction in field debugging time

---

### Phase 3: NIVEL 4 - Advanced Monitoring (LOW PRIORITY) 🟢

**When to Execute:**
- After 1 month production operation
- Predictive maintenance program initiated

**Duration:** 2 hours
**Impact:** 🟢 **LOW** - Predictive maintenance capabilities

#### 4.1 Health Statistics API
**Features:** Track success rates, detect failing sensors before complete failure
**Expected Impact:** 2-7 days advance warning of sensor failures

---

## Implementation Timeline

| Phase | Priority | Duration | Cumulative | Trigger |
|-------|----------|----------|------------|---------|
| NIVEL 1 | 🔴 CRITICAL | 2h | 2h | Before migration ✅ |
| NIVEL 2 | 🔴 HIGH | 3h | 5h | Pre-deployment |
| NIVEL 3 | 🟡 MEDIUM | 2h | 7h | After 1 week |
| NIVEL 4 | 🟢 LOW | 2h | 9h | After 1 month |

See `EXTERNAL_DRIVERS.md` section "DHT22 Driver Improvement Roadmap" for detailed technical implementation.

---

**Next Document:** See `EXTERNAL_DRIVERS.md` for drivers to import from external repositories.
