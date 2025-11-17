# Battery Life & Power Consumption Analysis

## Project: ESP32-S3 Asset Tracking Device with IMU & GPS

---

## Executive Summary

This document provides detailed battery life calculations and component power requirements for the ESP32-S3 based asset tracking system. The device operates on a 30-second duty cycle with WiFi transmission, sensor data collection, and BLE advertising for WiFi credential configuration.

**Average Power Consumption: 65.45 mA** (including battery monitoring LED, BLE advertising, and status LEDs)

**Recommended Battery:** 2200 mAh Li-Po (3.7V)  
**Expected Runtime:** ~33.6 hours (ideal) / ~28.6 hours (realistic with 15% margin)  
**Sensor Readings:** ~4,032 per battery charge  
**Deployment Duration:** 1.2 days (1.19 days) per charge cycle

**Key Power Consumers:**
- ESP32-S3 active/idle: ~15-100 mA (varies by state)
- BLE advertising: ~5 mA (always on for credential setup)
- Status LED (GPIO 39): ~7.5 mA (always on)
- Battery Indicator LED (GPIO 38): ~7.5 mA (always on)
- WiFi transmission: ~115 mA (during active transmission)
- Sensors: ~33.5 mA (active during data collection only)


---

## Component Power Requirements

### ESP32-S3 Microcontroller

| State | Current Draw | Duration/Cycle | Notes |
|-------|-------------|-----------------|-------|
| Active Processing | 80-120 mA | ~6-7 seconds | Running code, sensor updates, JSON creation |
| Idle (delay loop) | 10-20 mA | ~23 seconds | Just looping, not in true deep sleep |
| **Recommendation** | - | - | Implement deep sleep for 10x power reduction |

**Specs:**
- Operating Voltage: 3.3V
- Max Current: 240 mA (all cores + radio active)
- Typical Active: 100 mA

---

### BNO085 IMU Sensor (9-axis)

| State | Current Draw | Duration/Cycle | Notes |
|-------|-------------|-----------------|-------|
| Active (measuring) | 3.5 mA | ~3 seconds | Quaternion, Euler, Accel, Gyro, Mag readings |
| Sleep Mode | 0.005 mA | ~27 seconds | Low power standby |
| Initialization | 5 mA | ~500 ms | Only on first boot |

**Specs:**
- Operating Voltage: 3.3V
- I2C Interface: 400 kHz
- Output: 9-axis sensor fusion
- Accuracy: High precision quaternion data

---

### NEO-M9N GPS Receiver

| State | Current Draw | Duration/Cycle | Notes |
|-------|-------------|-----------------|-------|
| Active (acquiring fix) | 25-35 mA | ~3 seconds | Position acquisition & calculation |
| Sleep Mode | 0.05 mA | ~27 seconds | Minimal power state |
| Cold Start | 50+ mA | ~5-10 seconds | First acquisition (one-time) |

**Specs:**
- Operating Voltage: 3.3V
- UART Interface: 38400 baud (8N1)
- Acquisition Time: 25-35 seconds typical (cold start)
- Output: Latitude, Longitude, Altitude, Speed, Heading, Fix Type

---

### WiFi Radio (Built-in ESP32-S3)

| State | Current Draw | Duration/Cycle | Notes |
|-------|-------------|-----------------|-------|
| Connect & Handshake | 80-150 mA | ~1-1.5 seconds | WiFi association with AP |
| Active Transmission (POST) | 100-150 mA | ~1-2 seconds | HTTP POST request sending |
| Off (powered down) | 0 mA | ~27 seconds | Radio completely disabled |

**Specs:**
- Standard: 802.11 b/g/n
- Max Data Rate: 150 Mbps
- Frequency: 2.4 GHz
- Typical JSON payload: 400-600 bytes
- HTTP Timeout: 5 seconds

---

### NeoPixel Status LED (GPIO 39 - RGB WS2812B)

| State | Current Draw | Duration/Cycle | Notes |
|-------|-------------|-----------------|-------|
| Green (OK status) | 5-10 mA | Always on | Indicates both sensors initialized |
| Red (Error status) | 5-10 mA | Always on | Indicates sensor initialization failure |

**Specs:**
- Operating Voltage: 5V (with level shifter for 3.3V GPIO)
- Current per LED: 5-10 mA (at full brightness 255/255)
- Brightness Setting: 255 (full brightness)
- Colors: Full RGB capability
- Pin: GPIO 39

---

### Pulse LED (GPIO 40)

| State | Current Draw | Duration/Cycle | Notes |
|-------|-------------|-----------------|-------|
| On (pulse) | 2-10 mA | ~20 ms | WiFi TX event indicator (WIFI_EVENT_TX_DONE) |
| Off | 0 mA | ~29.98 seconds | Minimal leakage |

**Specs:**
- Operating Voltage: 3.3V (GPIO output)
- GPIO Max Current: 40 mA
- Typical indicator LED: 2-10 mA @ 3.3V
- Trigger: WiFi TX event handler callback
- Duration: LED_PULSE_DURATION = 20ms

---

### Battery Indicator LED (GPIO 38 - NeoPixel WS2812B)

| State | Current Draw | Duration/Cycle | Battery Level | Notes |
|-------|-------------|-----------------|----------------|-------|
| Green (excellent) | 5-8 mA | Always on | 75-100% (3.975-4.2V) | Updated every 5 seconds |
| Yellow (low) | 5-8 mA | Always on | 25-75% (3.225-3.975V) | Interpolated RGB |
| Red (critical) | 5-8 mA | Always on | 0-24% (3.0-3.225V) | Double-blink when <10% |
| Brightness | 200/255 | - | - | 78% of max (dimmer than status LED) |

**Specs:**
- Operating Voltage: 5V (with level shifter for 3.3V GPIO)
- GPIO Pin: 38
- Type: NeoPixel WS2812B RGB LED
- Update Frequency: Every 5 seconds (in main loop)
- Colors: Full RGB (red, yellow, green)
- Alerts: Double-blink when <10% battery (via `doubleBlink()` method)
- Implementation: `BatteryIndicatorLED` class with `updateBatteryLED()` method

**Battery Monitor Hardware (ADC on GPIO 4):**
- ADC Input: Voltage divider (R11: 220K, R12: 68K)
- Divider Ratio: 4.235:1
- Measures: 3.0V - 4.2V battery range
- Resolution: 12-bit (0-4095)
- Filtering: 100nF capacitor on ADC pin
- Update Interval: Every 5 seconds in main loop

---

## Battery Monitoring System (NEW)

### ADC Voltage Measurement

The battery voltage is continuously monitored via GPIO 4 ADC with a resistive divider and RC filter circuit:

```
VBAT (3.7V Battery)
    │
    ├─ R11 (220K) ─── [Divider Node]
    │                 │
    └─ R12 (68K) ─────┴── GND
                      │
                      ├─ R68 (1K) ─── GPIO 4 (ADC)
                      │
                      └─ C26 (100nF) ─── GND
```

**Circuit Analysis:**
- **Voltage Divider:** R11 (220K) and R12 (68K) create 4.235:1 divider ratio
- **RC Filter:** R68 (1K) + C26 (100nF) form low-pass filter (cutoff ~1.59 kHz)
- **ADC Input:** High impedance (>1MΩ) means R68 doesn't affect DC voltage measurement

**Calculation (from battery_monitor.h):**
```
ADC_Voltage = (ADC_Reading / 4095) × 3.3V
Battery_Voltage = ADC_Voltage × VOLTAGE_DIVIDER_RATIO
Battery_Voltage = ADC_Voltage × 4.235
Where: VOLTAGE_DIVIDER_RATIO = (220K + 68K) / 68K = 4.235
Note: R68 (1K) is for filtering only, not included in divider calculation
```

### Battery Percentage Calculation

```
Battery_Percentage = ((Voltage - 3.0V) / (4.2V - 3.0V)) × 100%
Percentage_Range: 0-100% (clamped)
Voltage_Range: 3.0V (0%) to 4.2V (100%)
```

### Color Scheme (GPIO 38 LED)

| Level | Voltage | Color | Meaning | Action |
|-------|---------|-------|---------|--------|
| Excellent | 3.975-4.2V | 🟢 Green | 75-100% | No action needed |
| Good | 3.6-3.975V | 🟡 Yellow | 50-75% | Monitor |
| Low | 3.225-3.6V | 🟡 Yellow | 25-50% | Plan recharge |
| Critical | 3.03-3.225V | 🔴 Red | 10-25% | Charge soon |
| Depleted | 3.0-3.03V | 🔴 Red (blink) | 0-10% | CHARGE NOW |

**Color Interpolation (Yellow Zone):**
```
At 75%:  Red=0,   Green=255 (Pure green)
At 50%:  Red=128, Green=128 (Orange/yellow)
At 25%:  Red=255, Green=0   (Pure red)

Green_Component = 255 × (Percentage - 25) / 50
Red_Component = 255 - Green_Component
```

### Battery Monitoring Features

1. **Continuous Voltage Reading**
   - Non-invasive via ADC
   - 1 reading per 5 seconds (low power impact)
   - Accurate to ±0.05V

2. **Battery Percentage Display**
   - Real-time calculation
   - Linear interpolation between min/max
   - Clamped 0-100% range

3. **Visual Indication**
   - Color-coded NeoPixel on GPIO 38
   - Separate from device status LED
   - Brightness 200/255 (78%)

4. **Critical Alerts**
   - Serial output every 5 seconds
   - Double-blink LED when <10%
   - "!!! CRITICAL BATTERY !!!" message

5. **Health Status**
   - Normal: 3.0V - 4.2V
   - Over-voltage: >4.2V (charging issue)
   - Critical-low: <3.0V (unsafe)

---

## Updated Power Consumption with Battery Monitoring

### Active Phase (~6-7 seconds) - UPDATED

```
ESP32-S3 active:        100 mA × 6.5 sec   = 650 mAs
BNO085 IMU active:      3.5 mA × 3 sec     = 10.5 mAs
NEO-M9N GPS active:     30 mA × 3 sec      = 90 mAs
WiFi (connect/TX):      115 mA × 2.5 sec   = 287.5 mAs
Device Status LED (GPIO 39): 7.5 mA × 6.5 sec = 48.75 mAs
Battery Indicator LED (GPIO 38): 7.5 mA × 6.5 sec = 48.75 mAs
Pulse LED (GPIO 40, avg): 0.5 mA × 6.5 sec = 3.25 mAs
ADC Battery Read:       1 mA × 0.01 sec    = 0.01 mAs (negligible)
                                            ──────────
                                 Subtotal:  1,139.76 mAs
```

### Idle Phase (~23-24 seconds) - UPDATED

```
ESP32-S3 idle:          15 mA × 23.5 sec   = 352.5 mAs
BNO085 IMU sleep:       0.005 mA × 23.5s   = 0.1175 mAs
NEO-M9N GPS sleep:      0.05 mA × 23.5 sec = 1.175 mAs
WiFi off:               0 mA × 23.5 sec    = 0 mAs
BLE (always on):        5 mA × 23.5 sec    = 117.5 mAs (estimated)
Device Status LED (GPIO 39): 7.5 mA × 23.5 sec = 176.25 mAs
Battery Indicator LED (GPIO 38): 7.5 mA × 23.5 sec = 176.25 mAs
Pulse LED off:          0 mA × 23.5 sec    = 0 mAs
ADC Battery Read:       1 mA × 0.01 sec    = 0.01 mAs (negligible)
                                            ──────────
                                 Subtotal:  823.8 mAs
```

### Revised Total Per 30-Second Cycle

```
Active Phase:           1,139.76 mAs
Idle Phase:             +  823.8 mAs
──────────────────────────────────
Per 30 seconds:         1,963.56 mAs (average)
Per 1 hour:             1,963.56 × 120 = 235,627 mAs = 235.6 mAh
Per 24 hours:           235.6 × 24 = 5,654.4 mAh ≈ 5.65 Ah
```

### Revised Average Current Draw

```
Average per 30-sec cycle: 1,963.56 mAs ÷ 30 sec = 65.45 mA average
Per hour:                 ~65.45 mA
Per day:                  ~1,570.8 mAh (1.57 Ah)
```

**Note:** This includes BLE always-on advertising (~5mA) and both NeoPixel LEDs always on. Actual consumption may vary based on BLE connection state and LED colors.

---

## Battery Life Calculations

**Updated with Battery Monitoring System and BLE:**
- **Previous Average Current:** 53.9 mA
- **Updated Average Current:** 65.45 mA (+11.55 mA for battery monitoring LED + BLE)
- **Impact:** ~21.4% additional power consumption
- **Components Added:**
  - Battery Indicator LED (GPIO 38): ~7.5 mA always on
  - BLE Advertising: ~5 mA always on (for WiFi credential setup)

### For 300 mAh Battery

```
Formula: Battery Capacity ÷ Average Current Draw
Calculation: 300 mAh ÷ 55.5 mA = 5.4 hours

Results:
├─ Hours:       5.4 hours
├─ Days:        0.225 days
├─ Minutes:     324 minutes
└─ Realistic:   4.6 hours (with 15% margin)

Timeline:
├─ 1 hour:      120 sensor readings transmitted
├─ 3 hours:     360 readings
└─ 5.4 hours:   972 readings (battery depleted)
```

### For 1000 mAh Battery

```
Formula: Battery Capacity ÷ Average Current Draw
Calculation: 1000 mAh ÷ 55.5 mA = 18.0 hours

Results:
├─ Hours:       18.0 hours
├─ Days:        0.75 days
├─ Minutes:     1,080 minutes
└─ Realistic:   15.3 hours (with 15% margin)

Timeline:
├─ 6 hours:     720 sensor readings
├─ 12 hours:    1,440 readings
└─ 18.0 hours:  2,160 readings (battery depleted)
```

### For 2000 mAh Battery

```
Formula: Battery Capacity ÷ Average Current Draw
Calculation: 2000 mAh ÷ 55.5 mA = 36.0 hours

Results:
├─ Hours:       36.0 hours
├─ Days:        1.5 days
├─ Minutes:     2,160 minutes
└─ Realistic:   30.6 hours (with 15% margin)

Timeline:
├─ 12 hours:    1,440 sensor readings
├─ 24 hours:    2,880 readings
└─ 36.0 hours:  4,320 readings (battery depleted)
```

### For 2200 mAh Battery (RECOMMENDED)

```
Formula: Battery Capacity ÷ Average Current Draw
Calculation: 2200 mAh ÷ 65.45 mA = 33.6 hours

Results:
├─ Hours:       33.6 hours
├─ Days:        1.4 days
├─ Minutes:     2,016 minutes
└─ Realistic:   28.6 hours (with 15% margin, ≈1.2 days)

Timeline:
├─ 12 hours:    1,440 sensor readings
├─ 24 hours:    2,880 readings
└─ 33.6 hours:  4,032 readings (battery depleted)
```

---

## Battery Comparison Table (UPDATED)

| Battery Capacity | Ideal Runtime | Realistic Runtime | Days | Sensor Readings |
|------------------|---------------|-------------------|------|-----------------|
| **300 mAh** | 4.6 hours | 3.9 hours | 0.16 days | ~552 |
| **1000 mAh** | 15.3 hours | 13.0 hours | 0.54 days | ~1,836 |
| **2000 mAh** | 30.6 hours | 26.0 hours | 1.08 days | ~3,672 |
| **2200 mAh** | 33.6 hours | 28.6 hours | 1.19 days | ~4,032 |

**Note:** Runtime reduced due to BLE always-on advertising and battery monitoring LED. To extend battery life, consider disabling BLE when not needed or implementing BLE sleep modes.

---

## Real-World Considerations

### Factors That Reduce Battery Life (10-20% impact)

1. **WiFi Connection Issues**
   - Weak signal = longer connection time = more power
   - Multiple retry attempts
   - Impact: +5-10 mA average
   - Can reduce runtime by 15-30%

2. **Battery Voltage Drop**
   - Battery voltage decreases as it drains
   - Device efficiency drops at low voltage
   - Impact: 5-10% runtime reduction
   - Noticeable in last 25% of charge

3. **Temperature Effects**
   - Cold environments: Battery capacity reduced by 20-30%
   - Hot environments: Device may throttle power
   - Impact: Variable (5-30%)
   - Battery lifespan also affected

4. **WiFi Retry Cycles (Queue Buildup)**
   - Failed transmissions (WiFi down) still consume power
   - Queued data in retry system
   - Array transmissions may take longer
   - Impact: +2-3 mA per failed cycle

5. **Battery Indicator LED Updates**
   - LED color updates every 5 seconds
   - Battery monitoring ADC sampling
   - Yellow color uses more power than green/red (mixed RGB)
   - Impact: Already included in calculations (+1.6 mA)
   - Impact: +2-3 mA for each retry

5. **Sensor Initialization Overhead**
   - GPS cold start: 50+ mA for 5-10 seconds
   - Only happens once, but on first boot
   - Impact: <1% for continuous operation

### Optimizations for Extended Battery Life

| Optimization | Current Savings | Implementation Difficulty |
|--------------|-----------------|---------------------------|
| **Disable NeoPixel LED** | ~7.5 mA (14%) | Easy - comment out |
| **Implement Deep Sleep** | ~12 mA (22%) | Medium - sleep mode setup |
| **Increase Cycle Interval** | Variable | Easy - change `READING_INTERVAL` |
| **Reduce WiFi retries** | ~2-3 mA | Medium - modify transmission logic |
| **Compress JSON data** | Minimal | Complex - adds overhead |

**With all optimizations: 25-30 mA average = 2-2.5x battery life**

---

## Recommended Battery Specifications

### For Standard Operation

```
Capacity:        2200 mAh (balanced size/runtime)
Chemistry:       Li-Po (lithium polymer) or Li-ion
Voltage:         3.7V nominal (typical single cell)
Continuous Draw: 150+ mA
Recommended:     20C discharge rating minimum
Connector:       USB-C with charging circuit
```

### Component Selection for 2200 mAh

| Battery Type | Pros | Cons | Cost |
|--------------|------|------|------|
| **Li-Po (3.7V 2200mAh)** | Small, light, ideal for wearables | Requires charge controller, less safe | $8-15 |
| **Li-ion 18650** | Standard, safe, lots of chargers available | Larger, heavier | $5-10 |
| **USB Power Bank** | Can charge on the go | Bulky, overkill capacity | $15-30 |
| **AA NiMH (4x1.2V, 2400mAh)** | Easy availability, safe | Voltage regulation needed, lower density | $20-30 |

---

## Power Budget Summary

```
┌──────────────────────────────────────────────┐
│ TOTAL SYSTEM POWER BUDGET (Per 30 sec)       │
├──────────────────────────────────────────────┤
│ Peak Current:        150+ mA (WiFi TX)       │
│ Average Current:     65.45 mA                │
│ Minimum Current:     30 mA (idle with LEDs)  │
│ Per Cycle Energy:    1,963.56 mAs = 0.545 mWh│
│ Per Day Energy:      26.2 Wh (per 3.7V)     │
├──────────────────────────────────────────────┤
│ Recommended Battery: 2200 mAh (3.7V)         │
│ Expected Runtime:    ~33.6 hours (ideal)     │
│ Expected Runtime:    ~28.6 hours (realistic) │
└──────────────────────────────────────────────┘
```

---

## Testing Recommendations

1. **Measure Actual Current Draw**
   - Use multimeter in series with battery
   - Record peaks during WiFi transmission
   - Compare against calculations

2. **Battery Discharge Test**
   - Monitor voltage drop over time
   - Track timestamp of last successful transmission
   - Validate calculation accuracy

3. **WiFi Signal Strength Impact**
   - Test with strong signal (-30 dBm)
   - Test with weak signal (-70 dBm)
   - Measure current difference

4. **Temperature Testing**
   - Test at 0°C, 25°C, 40°C
   - Record capacity variation
   - Adjust calculations for deployment environment

---

## Firmware Modifications for Better Battery Life

### Quick Wins (Easy to implement)

```cpp
// Disable NeoPixel during idle (14% power savings)
void updateStatusLED() {
  if (imuInitialized && gpsInitialized) {
    statusLED.setPixelColor(0, statusLED.Color(0, 255, 0));
  } else {
    statusLED.setPixelColor(0, statusLED.Color(255, 0, 0));
  }
  statusLED.show();
  // Add: statusLED.clear() and delay before show to pulse instead of always on
}

// Increase cycle interval (change as needed)
const unsigned long READING_INTERVAL = 60000; // 60 seconds instead of 30
```

### Advanced Optimization (Requires code changes)

```cpp
// Implement deep sleep between cycles
void enterDeepSleep(uint64_t sleepTime_us) {
  Serial.println("Entering deep sleep...");
  esp_deep_sleep(sleepTime_us);
  // Estimated power: <1mA in deep sleep
}
```

---

## Document Metadata

| Field | Value |
|-------|-------|
| **Device** | ESP32-S3 WROOM |
| **Sensors** | BNO085 IMU, NEO-M9N GPS |
| **Cycle Time** | 30 seconds |
| **Calculation Date** | December 2024 |
| **Average Current** | 65.45 mA (with BLE + battery LED) |
| **Recommended Battery** | 2200 mAh Li-Po (3.7V) |
| **Expected Runtime** | 1.2 days (realistic, 28.6 hours) |
| **BLE Status** | Always advertising (WiFi credential setup) |
| **LEDs** | Status LED (GPIO 39), Battery LED (GPIO 38), TX Pulse (GPIO 40) |

---

**Last Updated:** December 2024  
**Status:** Updated with accurate power calculations and code implementation details
