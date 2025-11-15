# Battery Monitoring System Documentation

## Overview

A complete battery monitoring and visual indicator system for the ESP32-S3 asset tracking device with a 3.7V 2200mAh Li-Po rechargeable battery.

---

## Hardware Configuration

### Battery Specifications
- **Model:** 3.7V 2200mAh Li-Po Rechargeable
- **Voltage Range:** 3.0V - 4.2V
- **Nominal Voltage:** 3.7V
- **Chemistry:** Lithium Polymer (Li-Po)
- **Energy:** 8.14 Wh
- **Reference:** https://quartzcomponents.com/products/3-7v-2200mah-li-po-rechargeable-battery-783049

### ADC Circuit (on GPIO 4)
```
VBAT (Battery)
   │
   ├─ R11 (220K) ─┬── [Divider Node]
   │              │
   └─ R12 (68K) ──┴── GND
                   │
                   ├─ R68 (1K) ─── BATTERY_ADC (GPIO 4)
                   │              │
                   └─ C26 (100nF) ┴── GND
```

**Circuit Components:**
- **Voltage Divider:** R11 (220K) and R12 (68K) scale down battery voltage
- **RC Low-Pass Filter:** R68 (1K) and C26 (100nF) filter high-frequency noise
  - Cutoff frequency: ~1.59 kHz (1/(2π × R68 × C26))
  - Filters noise while preserving DC battery voltage
- **ADC Input:** ESP32-S3 ADC (high impedance >1MΩ) reads filtered voltage

**Voltage Divider Calculation:**
- Divider Ratio: (R11 + R12) / R12 = (220K + 68K) / 68K = 4.235
- Note: R68 (1K) doesn't affect divider calculation due to high ADC input impedance
- ADC Reference: 3.3V
- ADC Attenuation: ADC_11db (full 0-3.3V range)
- Battery Voltage = ADC_Voltage × 4.235
- Measures: 3.0V (0%) to 4.2V (100%) battery range

### Battery Indicator LED (GPIO 38)
- **Type:** WS2812B NeoPixel RGB LED
- **Pin:** GPIO 38
- **Color Indication:**
  - **Green:** 75-100% (Excellent)
  - **Yellow:** 25-75% (Low)
  - **Red:** 0-24% (Critical)

---

## Software Components

### 1. `battery_monitor.h` - Core Battery Reading

**Main Class:** `BatteryMonitor`

**Key Methods:**

```cpp
// Initialize ADC
BatteryMonitor::initializeADC();

// Read battery voltage (in volts)
float voltage = BatteryMonitor::readBatteryVoltage();

// Get battery percentage (0-100%)
int percentage = BatteryMonitor::getBatteryPercentage();

// Get battery status string
String status = BatteryMonitor::getBatteryStatus();

// Get RGB color based on percentage
uint32_t color = BatteryMonitor::getBatteryColor();

// Get health status
String health = BatteryMonitor::getHealthStatus();

// Print full battery info
BatteryMonitor::printBatteryInfo();
```

**Calculation Logic:**

```cpp
// Voltage Reading (from battery_monitor.h)
ADC_Value (0-4095) → ADC_Voltage (0-3.3V) → Battery_Voltage
ADC_Voltage = (ADC_Reading / 4095) × 3.3V
Battery_Voltage = ADC_Voltage × 4.235

// Percentage Calculation
Percentage = ((Voltage - 3.0V) / (4.2V - 3.0V)) × 100%
- Clamped to 0-100%
- Linear interpolation between min (3.0V) and max (4.2V) voltage
- Returns integer percentage (0-100)

// Color Assignment (RGB format for NeoPixel)
75-100%: GREEN   (0x00FF00) - Green component only
25-75%:  YELLOW  Interpolated between green (75%) and red (25%)
         Green = 255 × (Percentage - 25) / 50
         Red = 255 - Green
0-24%:   RED     (0xFF0000) - Red component only
```

---

### 2. `battery_indicator_led.h` - LED Control

**Main Class:** `BatteryIndicatorLED`

**Key Methods:**

```cpp
// Create instance
BatteryIndicatorLED batteryIndicatorLED;

// Initialize LED
batteryIndicatorLED.begin();

// Update LED color based on battery
batteryIndicatorLED.updateBatteryLED();

// Set custom color (R, G, B)
batteryIndicatorLED.setColor(255, 128, 0); // Orange

// Pulse LED (low battery warning)
batteryIndicatorLED.pulseLED(500); // 500ms delay

// Double blink (critical alert)
batteryIndicatorLED.doubleBlink();

// Turn off
batteryIndicatorLED.turnOff();

// Get current percentage
int percent = batteryIndicatorLED.getBatteryPercentage();

// Get voltage
float volts = batteryIndicatorLED.getBatteryVoltage();

// Print status
batteryIndicatorLED.printStatus();

// Get color name
String color = batteryIndicatorLED.getColorName();
```

---

## Integration in Main Sketch

**File:** `juxta-asset-tag-main.ino`

### Includes
```cpp
#include "battery_monitor.h"
#include "battery_indicator_led.h"
```

### Global Instance
```cpp
// Battery Indicator LED instance (separate from status LED)
BatteryIndicatorLED batteryIndicatorLED;
```

### Setup() Initialization
```cpp
// Initialize Battery Monitor ADC
BatteryMonitor::initializeADC();
delay(100);

// Initialize Battery Indicator LED
batteryIndicatorLED.begin();
batteryIndicatorLED.updateBatteryLED(); // Set initial color based on battery
delay(100);
```

### Main Loop Integration
```cpp
// Update battery indicator LED every 5 seconds
static unsigned long lastBatteryUpdate = 0;
unsigned long currentTime = millis();

if (currentTime - lastBatteryUpdate >= 5000) {
  batteryIndicatorLED.updateBatteryLED();
  lastBatteryUpdate = currentTime;
  
  // Print battery info to serial
  Serial.println(BatteryMonitor::getBatteryStatus());
  
  // Critical battery warning
  if (BatteryMonitor::getBatteryPercentage() < 10) {
    Serial.println("!!! CRITICAL BATTERY !!!");
    batteryIndicatorLED.doubleBlink();
  }
}
```

**Note:** Battery monitoring runs continuously in the main loop, independent of the 30-second sensor reading cycle.

---

## LED Color Behavior

### Normal Operation

| Battery % | Voltage | LED Color | Meaning |
|-----------|---------|-----------|---------|
| 100% | 4.2V | Green | Fully charged |
| 75% | 3.975V | Green | Excellent |
| 50% | 3.6V | Yellow | Good, mid-life |
| 25% | 3.225V | Yellow | Getting low |
| 10% | 3.03V | Red | Critical |
| 0% | 3.0V | Red | Empty |

### Special States

**Pulse (Low Battery):**
- Triggered when battery < 25%
- LED fades in and out repeatedly
- Alerts user without stopping device

**Double Blink (Critical):**
- Triggered when battery < 10%
- Red LED blinks 2 times
- Occurs every 5-second battery check
- Device should be charged immediately

---

## Serial Output Examples

### Battery Status Output
```
Battery: 3.85V | 71% | Good
Battery: 3.50V | 33% | Low
Battery: 3.05V | 2% | Critical
```

### Detailed Battery Info
```
=== Battery Monitor Info ===
Voltage: 3.85V
Percentage: 71%
Status: Battery: 3.85V | 71% | Good
Health: HEALTHY
Raw ADC: 2456
===========================
```

### Critical Alert
```
Battery: 3.02V | 2% | Critical
!!! CRITICAL BATTERY !!!
```

---

## LED Pin Configuration

### GPIO 38 - Battery Indicator LED
- **Type:** NeoPixel WS2812B RGB LED
- **Function:** Battery level indication (color-coded)
- **Voltage:** 5V (with level shifter for 3.3V GPIO)
- **Current:** 5-8 mA (at configured brightness)
- **Brightness:** 200/255 (78% - dimmer than status LED)
- **Update Frequency:** Every 5 seconds
- **Colors:** Green (75-100%), Yellow (25-75%), Red (0-24%)

### GPIO 39 - Device Status LED (Separate)
- **Type:** NeoPixel WS2812B RGB LED
- **Function:** IMU/GPS initialization status
- **Color:** Green (OK) or Red (Error)
- **Brightness:** 255/255 (full brightness)
- **Current:** 5-10 mA
- **Always On:** Yes (for status indication)

### GPIO 40 - WiFi TX Pulse LED (Separate)
- **Type:** Standard GPIO output
- **Function:** WiFi transmission indicator
- **Voltage:** 3.3V
- **Current:** 2-10 mA (during pulse)
- **Duration:** 20ms pulse on WiFi TX event

---

## Calculation Details

### Voltage Divider Formula
```
V_Battery = V_ADC × (R11 + R12) / R12
V_Battery = V_ADC × (220K + 68K) / 68K
V_Battery = V_ADC × 4.235
```

### ADC to Voltage Conversion
```
V_ADC = (ADC_Reading / 4095) × 3.3V
```

### Percentage Formula
```
Battery_Percentage = ((V_Battery - V_Min) / (V_Max - V_Min)) × 100
Battery_Percentage = ((V_Battery - 3.0) / (4.2 - 3.0)) × 100
Battery_Percentage = ((V_Battery - 3.0) / 1.2) × 100
```

### Color Interpolation (Yellow Zone)
```
At 75%:  Red=0,   Green=255 (Pure green)
At 25%:  Red=255, Green=0   (Pure red)
At 50%:  Red=128, Green=128 (Yellow/orange)

Green_Component = 255 × (Percentage - 25) / 50
Red_Component = 255 - Green_Component
```

---

## Testing Procedures

### 1. Voltage Reading Test
```cpp
// In loop or serial command
float voltage = BatteryMonitor::readBatteryVoltage();
Serial.println("Battery Voltage: " + String(voltage, 2) + "V");

// Verify with multimeter across battery terminals
// Should match within ±0.05V
```

### 2. LED Color Test
```cpp
// Manually set battery LED to different colors
batteryIndicatorLED.setColor(0, 255, 0);   // Test green
batteryIndicatorLED.setColor(255, 255, 0); // Test yellow
batteryIndicatorLED.setColor(255, 0, 0);   // Test red
```

### 3. Critical Alert Test
```cpp
// Simulate low battery
if (BatteryMonitor::getBatteryPercentage() < 10) {
  batteryIndicatorLED.doubleBlink();
}
```

### 4. ADC Accuracy Calibration
```
Measure with multimeter:
1. Battery voltage (V_battery) at VBAT
2. ADC pin voltage (V_ADC) at GPIO 4 (after R68)
3. Calculate actual divider ratio: V_battery / V_ADC
4. Expected ratio: 4.235 (from R11=220K, R12=68K)
5. Update VOLTAGE_DIVIDER_RATIO in battery_monitor.h if needed
Note: R68 (1K) should not significantly affect measurement due to high ADC impedance
```

---

## Troubleshooting

### LED not showing correct color
- Check GPIO 38 connection
- Verify NeoPixel power supply (5V)
- Check level shifter for 3.3V to 5V conversion
- Test with `BatteryIndicatorLED::setColor()`

### Battery percentage incorrect
- Run `BatteryMonitor::printBatteryInfo()`
- Measure actual battery voltage with multimeter
- Compare with displayed voltage
- Calibrate divider ratio if significant difference

### ADC reading unstable
- Add delay between readings
- Verify C26 capacitor (100nF) is connected
- Check for EMI near ADC pin
- Ensure WiFi radio is not causing interference

### Battery draining too fast
- Check if LEDs are always on (reduce brightness)
- Monitor WiFi connection time
- Check for sensor power-down issues
- Review READING_INTERVAL setting

---

## Performance Impact

### ADC Sampling
- **Time:** <1ms per reading
- **Power:** <1mA (part of ESP32 current)
- **Frequency:** Every 5 seconds in main loop

### LED Updates
- **Time:** <10ms per update
- **Power:** 5-20mA depending on color
- **Frequency:** Every 5 seconds

### Total Overhead
- **Battery Monitoring:** ~0.5% of total power budget
- **LED Indicator:** ~5-15% of total power budget (depends on usage)

---

## Future Enhancements

1. **Battery Capacity Estimation**
   - Track voltage drop rate
   - Predict remaining runtime
   
2. **Charge Cycle Tracking**
   - Count full charge/discharge cycles
   - Alert when nearing end-of-life (800+ cycles)

3. **Temperature Compensation**
   - Adjust percentage based on temperature
   - Cold/hot weather adaptation

4. **Low Power Mode**
   - Disable WiFi when battery < 5%
   - Increase cycle interval to 60+ seconds
   - Store data locally until charged

5. **Battery Health Monitoring**
   - Track voltage curve over time
   - Detect battery degradation
   - Estimate replacement time

---

## Reference Links

- **Battery Product:** https://quartzcomponents.com/products/3-7v-2200mah-li-po-rechargeable-battery-783049
- **ESP32-S3 ADC:** https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/adc.html
- **Adafruit NeoPixel:** https://learn.adafruit.com/adafruit-neopixel-uberguide
- **Li-Po Safety:** https://www.adafruit.com/product/2750

---
