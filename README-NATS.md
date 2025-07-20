# NATS MQTT Server Setup for Smart Irrigation System

## Overview

This setup provides a local NATS server with MQTT and WebSocket support for testing the IoT Smart Irrigation System. The configuration is optimized for ESP32 devices that need to connect via MQTT over WebSockets.

## Quick Start

### 1. Start the NATS Server

```bash
# Start the NATS server
docker-compose up -d nats-server

# Check server status
docker-compose logs nats-server

# Check if all ports are listening
docker-compose ps
```

### 2. Test Connections

#### Test MQTT Connection (Port 1883)
```bash
# Using mosquitto_pub/mosquitto_sub (if installed)
mosquitto_pub -h localhost -p 1883 -t "irrigation/test" -m "Hello from MQTT"
mosquitto_sub -h localhost -p 1883 -t "irrigation/test"
```

#### Test WebSocket Connection (Port 8080)
```bash
# Using NATS CLI (start the CLI container)
docker-compose --profile tools up -d nats-cli

# Connect to CLI container
docker exec -it smart_irrigation_nats_cli sh

# Test WebSocket connection from inside container
nats pub --server ws://nats-server:8080 irrigation.test "Hello from WebSocket"
nats sub --server ws://nats-server:8080 "irrigation.>"
```

#### Test HTTP Monitoring (Port 8222)
```bash
# Check server info
curl http://localhost:8222/varz

# Check connections
curl http://localhost:8222/connz

# Check subscriptions
curl http://localhost:8222/subsz
```

### 3. ESP32 Configuration

For your ESP32 IoT device, use these connection parameters:

#### MQTT Configuration:
```c
// MQTT Broker Settings
#define MQTT_BROKER_HOST "your-local-ip"  // Replace with your machine's IP
#define MQTT_BROKER_PORT 1883
#define MQTT_CLIENT_ID "smart_irrigation_device"

// Topics for your irrigation system
#define TOPIC_REGISTER "irrigation/register"
#define TOPIC_DATA "irrigation/data/crop_name/device_mac"
#define TOPIC_CONTROL "irrigation/control/device_mac"
```

#### WebSocket Configuration:
```c
// For MQTT over WebSockets (ESP32 with WebSocket library)
#define WEBSOCKET_HOST "your-local-ip"  // Replace with your machine's IP
#define WEBSOCKET_PORT 8080
#define WEBSOCKET_PATH "/"
```

## Server Configuration Details

### Enabled Protocols:
- **NATS Native**: Port 4222 (for NATS clients)
- **MQTT**: Port 1883 (standard MQTT)
- **WebSocket**: Port 8080 (MQTT over WebSockets for ESP32)
- **HTTP Monitoring**: Port 8222 (server stats and monitoring)

### Key Features:
- **JetStream**: Enabled for message persistence and session management
- **No Authentication**: Configured for development (add auth for production)
- **No TLS**: Disabled for local testing (enable for production)
- **CORS**: Allows all origins for development

### MQTT Topic Translation:
NATS automatically translates between MQTT topics and NATS subjects:
- MQTT: `irrigation/data/sensor1` → NATS: `irrigation.data.sensor1`
- MQTT wildcards: `irrigation/+/status` → NATS: `irrigation.*.status`
- MQTT wildcards: `irrigation/#` → NATS: `irrigation.>`

## Production Considerations

When deploying to production, update the configuration to include:

1. **TLS/SSL**: Enable TLS for secure connections
2. **Authentication**: Add proper user authentication
3. **Authorization**: Restrict topic access per user/device
4. **Persistent Storage**: Use file-based JetStream storage
5. **Resource Limits**: Set appropriate memory and connection limits
6. **Monitoring**: Set up proper logging and monitoring

## Troubleshooting

### Common Issues:

1. **Port Already in Use**:
   ```bash
   # Check what's using the ports
   netstat -tulpn | grep -E ":(1883|4222|8080|8222)"
   
   # Stop conflicting services
   sudo systemctl stop mosquitto  # If mosquitto is running
   ```

2. **Connection Refused**:
   - Ensure Docker containers are running: `docker-compose ps`
   - Check firewall settings
   - Verify your local IP address for ESP32 connections

3. **WebSocket Connection Issues**:
   - Ensure ESP32 WebSocket library supports binary frames
   - Check CORS settings if connecting from browser
   - Verify WebSocket path is "/"

4. **MQTT Client Issues**:
   - ESP32 MQTT library must support MQTT 3.1.1
   - Check QoS settings (NATS MQTT supports QoS 0 and 1)
   - Verify topic naming follows MQTT standards

### Logs and Debugging:
```bash
# View real-time logs
docker-compose logs -f nats-server

# Check server statistics
curl http://localhost:8222/varz | jq

# Monitor active connections
watch -n 1 'curl -s http://localhost:8222/connz | jq .num_connections'
```

## Integration with Smart Irrigation System

This NATS server is configured to work with the ESP32-based Smart Irrigation System that follows these patterns:

### Device Registration:
```json
{
  "device_id": "esp32_001",
  "mac_address": "AA:BB:CC:DD:EE:FF",
  "crop_name": "tomatoes",
  "location": "greenhouse_1",
  "sensors": ["temperature", "humidity", "soil_moisture"],
  "firmware_version": "1.0.0"
}
```

### Sensor Data Publishing:
```json
{
  "timestamp": "2025-01-20T10:30:00Z",
  "device_id": "esp32_001",
  "sensors": {
    "temperature": 24.5,
    "humidity": 65.2,
    "soil_moisture": 45.8
  },
  "battery_level": 87
}
```

### Irrigation Control Commands:
```json
{
  "command": "start_irrigation",
  "duration_minutes": 15,
  "valve_id": "valve_1",
  "timestamp": "2025-01-20T10:35:00Z"
}
```

The server is ready to handle these message patterns and provides the necessary persistence and reliability features for IoT deployments.