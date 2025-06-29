#ifndef INFRASTRUCTURE_ADAPTERS_JSON_DEVICE_SERIALIZER_H
#define INFRASTRUCTURE_ADAPTERS_JSON_DEVICE_SERIALIZER_H

#include "../../domain/services/device_serializer.h"

/**
 * @file json_device_serializer.h
 * @brief Implementación JSON del Device Serializer
 * 
 * Adaptador concreto que implementa la interfaz device_serializer.h
 * para generar formato JSON compatible con MQTT y HTTP endpoints.
 * 
 * Siguiendo Hexagonal Architecture:
 * - Implementa el puerto definido en domain/services/device_serializer.h
 * - Contiene lógica específica de formato JSON
 * - Puede ser reemplazado por otros adaptadores (XML, Protobuf, etc.)
 */

// Todas las funciones están definidas en domain/services/device_serializer.h
// Esta implementación concreta sigue el contrato del puerto

#endif // INFRASTRUCTURE_ADAPTERS_JSON_DEVICE_SERIALIZER_H