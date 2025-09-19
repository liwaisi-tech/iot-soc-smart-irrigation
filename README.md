# Sistema de Riego Inteligente IoT

Sistema IoT para ESP32 diseñado para monitorear condiciones ambientales y de suelo en cultivos rurales colombianos, con control automatizado de riego vía MQTT y capacidad offline.

**Estado del Proyecto**: 🚧 **Fase 3 Completada** - Comunicación de datos funcional, Fase 4 en desarrollo

## Estado Actual del Desarrollo

### ✅ **Implementado y Funcional**
- **Arquitectura Hexagonal**: Implementación completa con Domain/Application/Infrastructure layers
- **WiFi Provisioning**: Interface web para configuración con validación de credenciales en tiempo real
- **Conectividad WiFi**: Reconexión automática con backoff exponencial
- **Sensor DHT22**: Lectura de temperatura y humedad ambiental
- **HTTP API REST**: 3 endpoints funcionales (/whoami, /temperature-and-humidity, /ping)
- **MQTT Client**: Comunicación con broker via WebSockets
- **Publicación de Datos**: Datos de sensores enviados cada 30 segundos
- **Gestión de Recursos**: Sistema de semáforos para concurrencia
- **Optimización de Memoria**: ~72KB optimizados manteniendo funcionalidad

### 🚧 **Pendiente de Implementar (Fase 4)**
- **Control de Riego**: Sistema de comandos MQTT para válvulas ❌ No implementado
- **Sensores de Suelo**: Integración de sensores de humedad del suelo (ADC) ❌ No implementado
- **Modo Offline**: Lógica de riego automático sin conectividad ❌ No implementado
- **Algoritmos de Riego**: Reglas de negocio para decisiones de riego ❌ No implementado

### ⏳ **Pendiente (Fase 5)**
- **Optimización Energética**: Modos de bajo consumo y sleep
- **Testing Completo**: Suite de pruebas unitarias e integración
- **Documentación de Campo**: Guías de instalación y troubleshooting

## Características Principales

- **Monitoreo Ambiental**: Temperatura y humedad (DHT22) ✅
- **Sensores de Suelo**: 1-3 sensores de humedad ❌ Pendiente
- **Control de Riego**: Comandos MQTT, modo offline de MQTT y modo manual/automatico on/off electrovalvula riego ❌ Pendiente
- **WiFi Inteligente**: Provisioning web con validación de credenciales ✅
- **API REST**: Endpoints para dispositivo y sensores ✅
- **Arquitectura Hexagonal**: Clean Code para embedded systems ✅

## Arquitectura

Implementa **Arquitectura Hexagonal** (Clean Architecture) con **Domain Driven Design**:

```
components/
├── domain/                    # 🎯 Lógica de negocio pura (5 value objects, 2 services)
│   ├── entities/             # Entidades core del dominio
│   ├── value_objects/        # ✅ Sensor data, device info, registration messages
│   └── services/             # ✅ Device config, serialization services
├── application/              # 🎯 Casos de uso (1 implementado)
│   ├── use_cases/           # ✅ publish_sensor_data (funcional)
│   └── dtos/                # Data Transfer Objects
└── infrastructure/          # 🎯 Adaptadores externos (6 adapters)
    ├── adapters/           # ✅ WiFi, HTTP, MQTT adapters
    │   ├── wifi_adapter/   # ✅ Provisioning + connection management
    │   ├── http_adapter/   # ✅ REST API con middleware
    │   ├── mqtt_adapter/   # ✅ WebSocket MQTT client
    │   └── shared_resource_manager/ # ✅ Concurrency control
    └── drivers/           # ✅ DHT22 sensor driver
```

**Métricas del Proyecto**: 59 archivos fuente (C/H), Arquitectura DDD completa

## Para Desarrolladores

### Requisitos del Sistema

- **ESP-IDF v5.4.2** (obligatorio)
- **Python 3.10+**
- **Visual Studio Code** con extensión ESP-IDF
- **Hardware**: ESP32 DevKit, DHT22, relays, sensores de suelo (opcional)
- **Conectividad**: Red WiFi 2.4GHz, broker MQTT (WebSocket habilitado)

### Configuración Rápida

```bash
# 1. Configurar ESP-IDF
get_idf

# 2. Clonar repositorio
git clone https://github.com/liwaisi-tech/iot-soc-smart-irrigation.git
cd iot-soc-smart-irrigation

# 3. Compilar y flashear (primera vez)
idf.py build flash monitor
```

### Comandos Básicos

```bash
# Activar framework ESP-IDF
get_idf

# Configurar proyecto (opcional)
idf.py menuconfig

# Compilar proyecto
idf.py build

# Flashear dispositivo
idf.py flash

# Monitor serie
idf.py monitor

# Todo en uno (recomendado)
idf.py build flash monitor

# Limpiar build
idf.py clean

# Ver tamaño de binario
idf.py size
```

### Configuración del Dispositivo

**El sistema NO requiere configuración manual de WiFi/MQTT en menuconfig**. Todo se configura via interfaz web:

1. **Primera vez**: El dispositivo crea red `Liwaisi-Config`
2. **Conectar**: A la red desde móvil/laptop
3. **Configurar**: Abrir `http://192.168.4.1` en el navegador
4. **WiFi**: El sistema valida credenciales en tiempo real
5. **Listo**: Dispositivo se conecta automáticamente

## Guía de Testing y Debugging

### 🧪 Testing del Sistema

#### **1. Verificar Conectividad WiFi**
```bash
# En el monitor serial, buscar:
I (XXXX) SMART_IRRIGATION_MAIN: Dirección IP obtenida: 192.168.1.XX
I (XXXX) SMART_IRRIGATION_MAIN: Adaptador HTTP inicializado correctamente en IP: 192.168.1.XX
```

#### **2. Probar HTTP Endpoints**
```bash
# Desde terminal/Postman (reemplazar IP)
curl http://192.168.1.52/whoami
curl http://192.168.1.52/temperature-and-humidity
curl http://192.168.1.52/ping

# Respuesta esperada /whoami:
{
  "device": {
    "name": "Liwaisi Smart Irrigation",
    "location": "Huerta",
    "version": "v1.0.0",
    "mac_address": "e8:6b:ea:f6:81:b8",
    "ip_address": "192.168.1.52"
  },
  "endpoints": [...]
}
```

#### **3. Verificar Datos de Sensores**
```bash
# En el monitor serial cada 30 segundos:
I (XXXX) publish_sensor_data_use_case: Starting sensor data publishing use case

# Si hay sensor DHT22 conectado:
I (XXXX) DHT_SENSOR: Temperature: 25.6°C, Humidity: 65.2%

# Si NO hay sensor conectado (normal en desarrollo):
E (XXXX) DHT_SENSOR: Phase B timeout - DHT not responding
W (XXXX) publish_sensor_data_use_case: Failed to read sensor data: ESP_ERR_TIMEOUT
```

### 🐛 Debugging Común

#### **Problema: HTTP Endpoints No Responden**
```bash
# Verificar en logs:
I (XXXX) SMART_IRRIGATION_MAIN: Adaptador HTTP inicializado correctamente

# Si no aparece, verificar conectividad WiFi primero
```

#### **Problema: WiFi No Se Conecta**
```bash
# Logs de diagnóstico:
W (XXXX) wifi_connection_manager: WiFi disconnected - reason: 4  # Auth failed
W (XXXX) wifi_connection_manager: WiFi disconnected - reason: 205 # Network not found

# Solución: Usar interfaz web para reconfigurar
```

#### **Problema: Memoria Insuficiente**
```bash
# Verificar memoria libre:
I (XXXX) SMART_IRRIGATION_MAIN: Sistema funcionando - Memoria libre: 210068 bytes

# Si < 150KB, revisar optimizaciones en CLAUDE.md
```

### 📖 Estructura del Proyecto

- **`main/`** - ✅ Punto de entrada (`iot-soc-smart-irrigation.c`)
- **`components/domain/`** - ✅ Entidades y value objects (5 implementados)
- **`components/application/`** - ✅ Use cases (1 funcional)
- **`components/infrastructure/`** - ✅ Adapters y drivers (6 funcionales)
- **`CLAUDE.md`** - 📖 Documentación técnica completa
- **`README.md`** - 📖 Este archivo

### 🚀 Próximos Pasos para Desarrolladores

#### **Fase 4 - Control de Riego (Prioridad Alta)**
1. **MQTT Command Subscription** - Implementar `MQTT_EVENT_DATA` handler en `mqtt_adapter.c`
2. **Valve Control Drivers** - Crear `drivers/valve_control/` con GPIO/relay management
3. **Irrigation Logic Service** - Implementar `services/irrigation_logic.h` con business rules
4. **Control Irrigation Use Case** - Crear `use_cases/control_irrigation.h` para orchestration
5. **Sensores de Suelo** - Implementar `drivers/soil_moisture/` con ADC readings

#### **Casos de Uso de Control de Riego:**
```json
// Comando start valve
{"action":"start","valve":1,"duration":15}

// Comando stop valve
{"action":"stop","valve":1}

// Comando multi-valve
{"action":"start","valves":[1,2],"duration":20}
```

#### **Otras Mejoras**
6. **Testing unitario** (crear directorio tests/)
7. **Optimización energética** (power management)
8. **Safety interlocks** (máximo 30min riego, failsafes)

## API y Formatos de Datos

### 🌐 HTTP Endpoints Disponibles

| Endpoint | Método | Descripción | Estado |
|----------|--------|-------------|--------|
| `/whoami` | GET | Info del dispositivo + endpoints disponibles | ✅ Funcional |
| `/temperature-and-humidity` | GET | Datos de sensor DHT22 en tiempo real | ✅ Funcional |
| `/ping` | GET | Connectivity check (responde "pong") | ✅ Funcional |

### 📡 MQTT Topics

| Topic | Descripción | Formato | Estado |
|-------|-------------|---------|--------|
| `irrigation/register` | Registro de dispositivo | JSON | ✅ Funcional |
| `irrigation/data/{crop}/{mac}` | Datos de sensores | JSON | ✅ Funcional |
| `irrigation/control/{mac}` | Comandos de riego | JSON | ❌ No implementado |

### 📄 Formatos JSON

#### **Registro del Dispositivo** (MQTT)
```json
{
  "event_type": "device_registration",
  "mac_address": "e8:6b:ea:f6:81:b8",
  "ip_address": "192.168.1.52",
  "device_name": "Liwaisi Smart Irrigation",
  "crop_name": "IoT",
  "firmware_version": "v1.0.0"
}
```

#### **Datos HTTP** (/temperature-and-humidity)
```json
{
  "ambient_temperature": 25.6,
  "ambient_humidity": 65.2,
  "timestamp": 1640995200
}
```

#### **Datos MQTT** (Publicación cada 30s)
```json
{
  "event_type": "sensor_data",
  "mac_address": "e8:6b:ea:f6:81:b8",
  "ip_address": "192.168.1.52",
  "ambient_temperature": 25.6,
  "ambient_humidity": 65.2,
  "soil_humidity_1": null,
  "soil_humidity_2": null,
  "soil_humidity_3": null
}
```

### 🔧 Hardware Pinout (ESP32)

```c
// DHT22 Sensor (Temperature/Humidity)
#define DHT22_DATA_GPIO            GPIO_NUM_4

// Soil Moisture Sensors (Planned)
#define SOIL_MOISTURE_1_ADC        ADC1_CHANNEL_0  // GPIO 36
#define SOIL_MOISTURE_2_ADC        ADC1_CHANNEL_3  // GPIO 39
#define SOIL_MOISTURE_3_ADC        ADC1_CHANNEL_6  // GPIO 34

// Irrigation Valves (Planned)
#define IRRIGATION_VALVE_1_GPIO    GPIO_NUM_2
#define IRRIGATION_VALVE_2_GPIO    GPIO_NUM_5
#define IRRIGATION_VALVE_3_GPIO    GPIO_NUM_18
```

## 🔄 Actualizaciones Recientes

### v1.1.0 - Optimizada y Estable ✅
- **WiFi Provisioning Inteligente**: Validación de credenciales en tiempo real
- **Optimización de Memoria**: 72KB liberados manteniendo funcionalidad
- **HTTP Server Robusto**: Inicialización confiable después de conexión WiFi
- **Arquitectura Hexagonal**: Implementación completa DDD
- **Logging Optimizado**: Sistema de middleware con control de memoria

### Fixes Críticos Aplicados
- ✅ **HTTP Adapter**: Se inicializa correctamente tras obtener IP (no solo en aprovisionamiento)
- ✅ **WiFi Credential Validation**: Valida credenciales antes de confirmar éxito
- ✅ **Memory Optimization**: JSON serialization, middleware, y componentes WiFi optimizados
- ✅ **Concurrency**: Sistema de semáforos para prevenir deadlocks durante provisioning
- ✅ **Error Handling**: Detección específica de errores WiFi (auth fail, network not found, timeout)

---

## 👥 Información del Proyecto

**Desarrollado por**: [Liwaisi Tech](https://liwaisi.tech)
**Mercado Objetivo**: Comunidades rurales de Colombia
**Licencia**: MIT
**Versión Actual**: v1.1.0 (Estable para desarrollo)

**Estado**: 🚧 **Fase 3 Completada** - Sistema base funcional, listo para implementar control de riego

Para documentación técnica completa, ver [`CLAUDE.md`](./CLAUDE.md)
