# Sistema de Riego Inteligente IoT

Sistema IoT para ESP32 diseñado para monitorear condiciones ambientales y de suelo en cultivos rurales colombianos, con control automatizado de riego vía MQTT y capacidad offline.

## Características Principales

- **Monitoreo Ambiental**: Temperatura, humedad y sensores de suelo
- **Control de Riego**: Automatizado vía MQTT con modo offline
- **Conectividad**: WiFi con reconexión automática
- **API REST**: Endpoints para información del dispositivo y sensores
- **Arquitectura Hexagonal**: Código limpio y mantenible

## Arquitectura

Implementa **Arquitectura Hexagonal** (Clean Architecture) con **Domain Driven Design**:

```
components/
├── domain/           # Lógica de negocio pura
├── application/      # Casos de uso
└── infrastructure/   # Adaptadores externos
```

## Para Desarrolladores

### Requisitos

- ESP-IDF v5.4.2
- Python 3.10+
- Visual Studio Code con extensión ESP-IDF

### Configuración del Entorno

```bash
# Configurar ESP-IDF
get_idf

# Clonar repositorio
git clone https://github.com/liwaisi-tech/iot-soc-smart-irrigation.git
cd iot-soc-smart-irrigation
```

### Comandos Básicos

```bash
# Configurar proyecto
idf.py menuconfig

# Compilar proyecto
idf.py build

# Flashear dispositivo
idf.py flash

# Monitor serie
idf.py monitor

# Compilar y flashear
idf.py build flash monitor

# Limpiar build
idf.py clean
```

### Configuración WiFi y MQTT

```bash
# Configurar credenciales WiFi
idf.py menuconfig
# Navegar a: Example Configuration -> WiFi SSID/Password

# Configurar broker MQTT
# Navegar a: Example Configuration -> MQTT Broker URL
```

### Desarrollo

1. **Seguir las fases de desarrollo** definidas en `CLAUDE.md`
2. **Mantener código limpio**: funciones pequeñas, nombres descriptivos
3. **Respetar la arquitectura**: Dominio → Aplicación → Infraestructura
4. **Probar incrementalmente**: cada componente debe ser testeable

### Estructura del Proyecto

- `main/` - Punto de entrada de la aplicación
- `components/domain/` - Entidades y lógica de negocio
- `components/application/` - Casos de uso
- `components/infrastructure/` - Drivers, adaptadores de red
- `docs/` - Documentación del proyecto
- `tests/` - Pruebas unitarias e integración

## Formatos JSON

### Registro del Dispositivo
```json
{
  "event_type": "device_registration",
  "mac_address": "AA:BB:CC:DD:EE:FF",
  "ip_address": "192.168.1.100",
  "device_name": "ESP32_Cultivo_01",
  "crop_name": "tomates",
  "firmware_version": "v1.0.0"
}
```

### Datos de Sensores Ambientales
```json
{
  "ambient_temperature": 25.6,
  "ambient_humidity": 65.2
}
```

### Datos de Sensores de Suelo
```json
{
  "soil_humidity_1": 45.8,
  "soil_humidity_2": 42.1,
  "soil_humidity_3": 48.3
}
```

### Payload MQTT Completo
```json
{
  "event_type": "sensor_data",
  "mac_address": "AA:BB:CC:DD:EE:FF",
  "ip_address": "192.168.1.100",
  "ambient_temperature": 25.6,
  "ambient_humidity": 65.2,
  "soil_humidity_1": 45.8,
  "soil_humidity_2": 42.1,
  "soil_humidity_3": 48.3
}
```

---

**Desarrollado por**: [Liwaisi Tech](https://liwaisi.tech)  
**Mercado Objetivo**: Comunidades rurales de Colombia
