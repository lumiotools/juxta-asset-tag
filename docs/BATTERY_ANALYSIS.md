# Battery Life & Power Consumption Analysis

## Project: ESP32-S3 Asset Tracking Device with IMU & GPS

---

## Executive Summary

This document provides detailed battery life calculations and component power requirements for the ESP32-S3 based asset tracking system. The device operates on a **30-second duty cycle** with WiFi transmission, sensor data collection, and BLE advertising for WiFi credential configuration.

**Average Power Consumption: ~77.9 mA** (including battery monitoring LED, BLE advertising, and status LEDs)

**Battery:** 2200 mAh Li-Po (3.7V)  
**Expected Runtime:** ~28.2 hours (ideal) / ~24.0 hours (realistic with 15% margin)  
**Sensor Readings:** ~2,880 per battery charge  
**Deployment Duration:** ~1.0 day per charge cycle

**Key Power Consumers:**
- **ESP32-S3 Active:** ~150 mA (WiFi + Processing)
- **ESP32-S3 Idle:** ~30-40 mA (Light sleep/delay loop with BLE active)
- **BLE Advertising:** ~5 mA (Always on background)
- **Status LEDs:** ~15 mA (2x NeoPixels always on)
- **Sensors:** ~33.5 mA (Active only during reading)

---

## ⚠️ CRITICAL HARDWARE WARNING

**Voltage Connection:**
You mentioned connecting a **2200mAh Li-Po battery** to the **3.3V pin** of the board.

> **DO NOT CONNECT A Li-Po BATTERY (3.7V - 4.2V) DIRECTLY TO THE 3.3V PIN!**

- The ESP32-S3 and sensors typically have a maximum voltage rating of **3.6V**.
- A fully charged Li-Po battery is **4.2V**.
- Connecting 4.2V to the 3.3V rail will likely **permanently damage** the ESP32-S3, IMU, and GPS.

**Correct Connection:**
- Connect the Li-Po battery to the **BAT** or **5V/VIN** pin (which feeds the onboard voltage regulator).
- OR use an external 3.3V Low Dropout Regulator (LDO) between the battery and the 3.3V pin.

*The calculations below assume the system is powered correctly (e.g., via LDO) and the current drawn from the battery is roughly equal to the system current consumption (linear regulator assumption).*

---

## Component Power Requirements

### ESP32-S3 Microcontroller

| State | Current Draw | Duration/Cycle | Notes |
|-------|-------------|-----------------|-------|
| Active (WiFi Connect/TX) | 100-150 mA | ~4-5 seconds | WiFi association & HTTP POST |
| Active (Processing) | 60-80 mA | ~3-4 seconds | Sensor stabilization & reading |
| Idle (Loop delay) | 30-40 mA | ~22 seconds | `delay()` loop, BLE stack active |
| **Recommendation** | - | - | Use `esp_light_sleep_start()` instead of `delay()` |

### Sensors & Peripherals

| Component | Active Current | Sleep Current | Active Duration | Notes |
|-----------|----------------|---------------|-----------------|-------|
| **BNO085 IMU** | 3.5 mA | 0.005 mA | ~3 sec | Powered off/sleep between readings |
| **NEO-M9N GPS** | 30 mA | 0.05 mA | ~3 sec | Powered off/sleep between readings |
| **Status LED** | 7.5 mA | 7.5 mA | Always On | GPIO 48 (NeoPixel) |
| **Battery LED** | 7.5 mA | 7.5 mA | Always On | Battery Indicator |
| **BLE Radio** | ~5 mA | ~5 mA | Always On | Background advertising |

---

## Power Consumption Analysis (30-Second Cycle)

### 1. Active Phase (~8 seconds)
*Events: Wake sensors, stabilize (2s), connect WiFi (~3s), read sensors (1s), transmit (~1-2s), power down.*

```
ESP32-S3 (Avg Active):  120 mA × 8 sec     = 960 mAs
Sensors (IMU+GPS):      33.5 mA × 3 sec    = 100.5 mAs
Status LEDs (x2):       15 mA × 8 sec      = 120 mAs
BLE Advertising:        5 mA × 8 sec       = 40 mAs
WiFi Pulse LED:         2.5 mA × 2 sec     = 5 mAs
                                            ──────────
                                 Subtotal:  1,225.5 mAs
```

### 2. Idle Phase (~22 seconds)
*Events: Sensors off, WiFi off, BLE advertising, LEDs on, main loop delay.*

```
ESP32-S3 (Idle/BLE):    35 mA × 22 sec     = 770 mAs
Sensors (Sleep):        0.1 mA × 22 sec    = 2.2 mAs
Status LEDs (x2):       15 mA × 22 sec     = 330 mAs
BLE Advertising:        Included in ESP32  = 0 mAs
                                            ──────────
                                 Subtotal:  1,102.2 mAs
```

### 3. Total Per Cycle

```
Total Charge per Cycle: 1,225.5 + 1,102.2 = 2,327.7 mAs
Average Current:        2,327.7 mAs / 30 sec = 77.59 mA
```

---

## Battery Life Calculation

**Battery:** 2200 mAh Li-Po
**Average Current:** ~77.6 mA

### Runtime Estimates

| Scenario | Runtime | Days | Sensor Readings |
|----------|---------|------|-----------------|
| **Ideal** (100% Capacity) | **28.3 Hours** | 1.18 Days | ~3,400 |
| **Realistic** (85% Efficiency) | **24.1 Hours** | 1.00 Days | ~2,890 |
| **Conservative** (70% Efficiency) | **19.8 Hours** | 0.82 Days | ~2,380 |

*Note: Efficiency losses account for voltage regulator heat, battery self-discharge, and aging.*

---

## Optimization Recommendations

To extend battery life beyond 24 hours, consider the following changes:

1.  **Turn off LEDs in Idle:**
    *   Currently, Status and Battery LEDs consume ~15mA continuously.
    *   **Saving:** 15mA * 22s = 330mAs per cycle.
    *   **New Avg Current:** ~66 mA.
    *   **New Runtime:** ~28 hours (Realistic).

2.  **Use Deep Sleep:**
    *   Replace `delay()` in loop with `esp_deep_sleep()`.
    *   ESP32 current drops from ~35mA to ~0.01mA.
    *   **Saving:** ~35mA * 22s = 770mAs per cycle.
    *   **New Avg Current:** ~40 mA.
    *   **New Runtime:** ~46 hours (Realistic).

3.  **Increase Reading Interval:**
    *   Change `READING_INTERVAL` from 30s to 60s or 5 minutes.
    *   **At 60s:** Runtime doubles (~48 hours).
    *   **At 5 min:** Runtime ~4-5 days.

## Conclusion

With the current firmware (LEDs always on, no deep sleep, BLE active) and a **2200mAh battery**, you can expect the device to run for approximately **24 hours**.

Ensure you connect the battery to the correct power input (BAT/VIN) to avoid damaging the 3.3V components.
