# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Component-Based Architecture (v2.0.0 - Migration Completed 2025-10-09)
# All components now in standard ESP-IDF components/ directory:
# ✅ sensor_reader    - Unified sensor interface (DHT22 + ADC soil sensors)
# ✅ device_config    - NVS configuration management
# ✅ wifi_manager     - WiFi connectivity + provisioning (thread-safe)
# ✅ mqtt_client      - MQTT communication + JSON serialization
# ✅ http_server      - REST API endpoints
# 🟢 irrigation_controller - Ready for Phase 5 implementation
# ⏳ system_monitor   - Pending Phase 5

# ESP-IDF will automatically discover components in components/ directory
# No EXTRA_COMPONENT_DIRS needed - using standard convention

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(iot-soc-smart-irrigation)
