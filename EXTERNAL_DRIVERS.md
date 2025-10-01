# External Drivers Import Guide

## Overview

This document lists external drivers and code that need to be imported from other repositories for the component-based architecture migration.

**Status:** ⚠️ REQUIRED BEFORE COMPILATION

---

## Required Drivers

### 1. Soil Moisture Sensor Drivers (ADC-based)

**Component:** `sensor_reader`

**Required Files:**
```
sensor_reader/
├── soil_adc_driver.c       ⚠️ NEEDS IMPORT
└── soil_adc_driver.h       ⚠️ NEEDS IMPORT (or inline in sensor_reader.h)
```

**Functionality Needed:**
- ADC1 channel configuration (CH0, CH3, CH6 for GPIO 36, 39, 34)
- 12-bit ADC reading with attenuation (ADC_ATTEN_DB_11 for 0-3.3V)
- Raw voltage to percentage conversion
- Calibration support (dry/wet voltage mapping)
- Moving average filtering (5-sample window)

**Where to Find:**
- **Option 1:** Check Liwaisi Tech internal repositories for existing soil sensor driver
- **Option 2:** ESP-IDF ADC examples: `examples/peripherals/adc/oneshot_read`
- **Option 3:** Implement from scratch using esp_adc library

**Implementation Notes:**
```c
// Required ADC configuration
adc_oneshot_unit_handle_t adc1_handle;
adc_oneshot_chan_cfg_t config = {
    .bitwidth = ADC_BITWIDTH_12,        // 12-bit resolution (0-4095)
    .atten = ADC_ATTEN_DB_11,           // 0-3.3V range
};

// Channels for soil sensors
adc_channel_t soil_channels[3] = {
    ADC_CHANNEL_0,  // GPIO 36
    ADC_CHANNEL_3,  // GPIO 39
    ADC_CHANNEL_6,  // GPIO 34
};

// Conversion formula (example - needs calibration)
float raw_to_percentage(int raw_value, uint16_t dry_mv, uint16_t wet_mv) {
    float voltage_mv = (raw_value * 3300.0) / 4095.0;
    float percentage = 100.0 * (dry_mv - voltage_mv) / (dry_mv - wet_mv);
    return CLAMP(percentage, 0.0, 100.0);
}
```

**Calibration Values (Typical for Capacitive Sensors):**
- Dry soil: ~2800 mV
- Wet soil: ~1200 mV
- These should be configurable via NVS

**References:**
- ESP-IDF ADC Guide: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/adc_oneshot.html
- Capacitive Soil Sensor Datasheet (if available)

**Priority:** 🔴 CRITICAL - Required for sensor_reader component

---

### 2. DHT22 Driver (Temperature/Humidity)

**Component:** `sensor_reader`

**Current Status:** ✅ IMPLEMENTED in hexagonal architecture

**Source Location:**
```
components/infrastructure/drivers/dht_sensor/
├── src/dht_sensor.c           ✅ EXISTS
└── include/dht_sensor.h       ✅ EXISTS
```

**Migration Action:**
```bash
# Copy existing driver to new component
cp components/infrastructure/drivers/dht_sensor/src/dht_sensor.c \
   components_new/sensor_reader/dht22_driver.c

# Extract header content to merge into sensor_reader.h or keep separate
cp components/infrastructure/drivers/dht_sensor/include/dht_sensor.h \
   components_new/sensor_reader/dht22_driver.h
```

**Functionality:**
- One-wire protocol implementation
- Temperature reading (-40 to 80°C)
- Humidity reading (0-100%)
- CRC checksum validation
- Error handling and retry logic

**GPIO Configuration:**
- Current: GPIO_NUM_4 (in old code)
- **NEW:** GPIO_NUM_18 (per Logica_de_riego.md - safer boot pin)

**Required Changes:**
```c
// Update GPIO in sensor_reader_init()
#define DHT22_GPIO  GPIO_NUM_18  // Changed from GPIO_NUM_4
```

**Priority:** ✅ **NIVEL 1 COMPLETED** - Ready for sensor_reader integration

**NIVEL 1 Enhancements (✅ COMPLETED):**
- `dht_read_ambient_data()` wrapper for `ambient_data_t` compatibility
- Automatic retry with backoff (3 attempts, 2.5s delay)
- Automatic timestamp generation
- Enhanced error logging

**Post-Migration Improvements Planned:**
See detailed roadmap in section "DHT22 Driver Improvement Roadmap" below.

---

## DHT22 Driver Improvement Roadmap (Post-Migration)

**Current Status:** ✅ NIVEL 1 implemented
**Remaining Work:** NIVEL 2-4 optimizations

### NIVEL 2: Production Readiness 🔴 (3 hours - HIGH PRIORITY)

**Features:**
1. **Throttling/Cache:** Prevent reads <2s, cache last valid reading
2. **Range Validation:** Reject impossible values (-40-80°C, 0-100%)

**Expected Impact:** 90% reduction in timing errors, 100% invalid reading rejection

**When:** Before first field deployment

---

### NIVEL 3: Field Optimization 🟡 (2 hours - MEDIUM PRIORITY)

**Features:**
1. **Dynamic Configuration:** Adjust retries/delays via NVS/MQTT
2. **Diagnostic Logging:** Phase timing, retry patterns

**Expected Impact:** 30% error reduction via field-specific tuning, 60% less debugging time

**When:** After 1 week production data

---

### NIVEL 4: Advanced Monitoring 🟢 (2 hours - LOW PRIORITY)

**Features:**
1. **Health Statistics API:** Track success rates, detect sensor degradation

**Expected Impact:** 2-7 days advance warning of failures, predictive maintenance

**When:** After 1 month production

---

**Implementation Timeline:**
- NIVEL 1: 2h ✅ COMPLETED
- NIVEL 2: 3h → Before deployment 🔴
- NIVEL 3: 2h → After 1 week 🟡
- NIVEL 4: 2h → After 1 month 🟢

**Total:** 9 hours for complete optimization

See `MIGRATION_MAP.md` section "Post-Migration Improvements - DHT22 Driver Optimization" for detailed technical specs.

---

### 3. Relay/Valve Control Driver

**Component:** `irrigation_controller`

**Required Files:**
```
irrigation_controller/
└── valve_driver.c             ⚠️ NEEDS IMPLEMENTATION
```

**Functionality Needed:**
- GPIO output configuration (GPIO 21, 22)
- Relay control (active HIGH for IN1, IN2)
- Valve state tracking
- Thread-safe valve switching

**Implementation Notes:**
```c
// GPIO configuration for relay module
#define VALVE_1_GPIO    GPIO_NUM_21  // Relay IN1
#define VALVE_2_GPIO    GPIO_NUM_22  // Relay IN2

// Relay is active HIGH (pulso)
#define VALVE_ON        1
#define VALVE_OFF       0

gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << VALVE_1_GPIO) | (1ULL << VALVE_2_GPIO),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_ENABLE,  // Pull down when off
    .intr_type = GPIO_INTR_DISABLE,
};
gpio_config(&io_conf);

// Valve control
void valve_set_state(uint8_t valve_num, bool state) {
    gpio_num_t gpio = (valve_num == 1) ? VALVE_1_GPIO : VALVE_2_GPIO;
    gpio_set_level(gpio, state ? VALVE_ON : VALVE_OFF);
}
```

**Hardware Specs:**
- Relay Module: 2-channel, 5V input (VCC)
- Control: HIGH pulse activates relay
- Output: K1, K2 (normally open contacts)
- Valve: 4.7V latch-type solenoid valve

**Priority:** 🟡 HIGH - Required for irrigation_controller component

---

## Optional/Future Drivers

### 4. Flow Meter Driver (Future Enhancement)

**Status:** ⏳ NOT REQUIRED FOR v2.0.0

**Use Case:** Water flow measurement for precise irrigation control

**If Implemented:**
- GPIO interrupt-based pulse counting
- Flow rate calculation (L/min)
- Total volume tracking

---

### 5. RTC Module Driver (Future Enhancement)

**Status:** ⏳ NOT REQUIRED FOR v2.0.0

**Use Case:** Accurate timekeeping for irrigation scheduling (nocturnal watering)

**Current Solution:** Use NTP time sync when WiFi available, ESP32 internal RTC otherwise

---

## Import Checklist

### Before Compilation:

- [ ] **DHT22 Driver:**
  - [ ] Copy `dht_sensor.c` → `dht22_driver.c`
  - [ ] Update GPIO to GPIO_NUM_18
  - [ ] Test reading functionality

- [ ] **Soil ADC Driver:**
  - [ ] Locate existing driver OR implement from scratch
  - [ ] Create `soil_adc_driver.c`
  - [ ] Implement ADC configuration
  - [ ] Implement raw-to-percentage conversion
  - [ ] Add calibration support
  - [ ] Test with physical sensors

- [ ] **Valve Driver:**
  - [ ] Implement `valve_driver.c`
  - [ ] Configure GPIO 21, 22 as outputs
  - [ ] Implement active-HIGH relay control
  - [ ] Add safety mutex for thread-safe operation
  - [ ] Test with relay module

### During Implementation:

- [ ] Integrate DHT22 driver into `sensor_reader.c`
- [ ] Integrate soil driver into `sensor_reader.c`
- [ ] Integrate valve driver into `irrigation_controller.c`
- [ ] Test unified sensor reading
- [ ] Test irrigation control

### Validation:

- [ ] DHT22 reads valid temperature and humidity
- [ ] All 3 soil sensors read valid percentages
- [ ] Calibration adjusts readings correctly
- [ ] Valve activation works reliably
- [ ] No GPIO conflicts or boot issues

---

## Implementation Recommendations

### DHT22 Driver Migration:
```bash
# 1. Copy existing driver
mkdir -p components_new/sensor_reader/
cp components/infrastructure/drivers/dht_sensor/src/dht_sensor.c \
   components_new/sensor_reader/dht22_driver.c

# 2. Update GPIO constant
sed -i 's/GPIO_NUM_4/GPIO_NUM_18/g' components_new/sensor_reader/dht22_driver.c

# 3. Test compilation
cd components_new/sensor_reader/
# Add to CMakeLists.txt and build
```

### Soil Driver Implementation:
```bash
# Option 1: Find existing driver
# Check internal repos, ask team

# Option 2: Use ESP-IDF example as template
cp $IDF_PATH/examples/peripherals/adc/oneshot_read/main/oneshot_read_main.c \
   components_new/sensor_reader/soil_adc_driver.c
# Then adapt for irrigation system

# Option 3: Implement from scratch
# Create file and implement ADC logic
```

### Valve Driver Implementation:
```c
// Start with this minimal implementation in valve_driver.c

#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char* TAG = "valve_driver";
static SemaphoreHandle_t valve_mutex = NULL;

esp_err_t valve_driver_init(void) {
    valve_mutex = xSemaphoreCreateMutex();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_21) | (1ULL << GPIO_NUM_22),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret == ESP_OK) {
        gpio_set_level(GPIO_NUM_21, 0);
        gpio_set_level(GPIO_NUM_22, 0);
        ESP_LOGI(TAG, "Valve driver initialized");
    }
    return ret;
}

esp_err_t valve_set_state(uint8_t valve_num, bool state) {
    if (valve_num < 1 || valve_num > 2) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(valve_mutex, portMAX_DELAY);
    gpio_num_t gpio = (valve_num == 1) ? GPIO_NUM_21 : GPIO_NUM_22;
    gpio_set_level(gpio, state ? 1 : 0);
    xSemaphoreGive(valve_mutex);

    ESP_LOGI(TAG, "Valve %d %s", valve_num, state ? "ON" : "OFF");
    return ESP_OK;
}
```

---

## External Resources

### ESP-IDF Documentation:
- ADC Oneshot: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/adc_oneshot.html
- GPIO: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html
- FreeRTOS Semaphores: https://www.freertos.org/a00113.html

### Hardware Datasheets:
- DHT22 Datasheet: https://www.sparkfun.com/datasheets/Sensors/Temperature/DHT22.pdf
- Capacitive Soil Sensor: (varies by manufacturer - check specific sensor used)
- 2-Channel Relay Module: (generic 5V relay module spec)

### Liwaisi Tech Repositories:
- **TODO:** Add links to internal repos with existing drivers
- **DHT22 Driver:** [Repository URL]
- **Soil Sensor Driver:** [Repository URL]
- **Irrigation System Examples:** [Repository URL]

---

## Contact for Support

**For Driver Questions:**
- **DHT22 Issues:** Check existing implementation in `components/infrastructure/drivers/dht_sensor/`
- **Soil Sensor Issues:** [Team contact or repo]
- **Relay Control Issues:** [Team contact or repo]

**For Migration Questions:**
- See `MIGRATION_MAP.md` for component mapping
- See `CLAUDE.md` for project documentation
- See `Logica_de_riego.md` for irrigation logic specs

---

**Next Steps:**
1. Review this document
2. Locate/implement required drivers
3. Proceed with component implementation (Phase 2 of migration)
