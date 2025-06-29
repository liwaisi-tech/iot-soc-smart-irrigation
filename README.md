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

---

**Desarrollado por**: [Liwaisi Tech](https://liwaisi.tech)  
**Mercado Objetivo**: Comunidades rurales de Colombia
