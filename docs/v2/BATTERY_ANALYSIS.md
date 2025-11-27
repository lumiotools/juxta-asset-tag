# Battery Life & Power Consumption Analysis (v2)

## Project: ESP32C6 Asset Tracking Device with IMU & GPS

---

## Executive Summary

This document provides detailed battery life calculations and component power requirements for the ESP32C6 based asset tracking system (v2). The device uses BMI323 IMU, AT6558 GPS, and W25Q128 external flash for storage. The device operates on a **30-second duty cycle** with WiFi/BLE transmission, sensor data collection, and BLE advertising for WiFi credential configuration (only on power-on, auto-stops after 1 minute if disconnected).

**Power Consumption Varies by Connection State:**
- **BLE Always Connected:** ~77.6 mA → ~24.1 hours realistic (worst case, best connectivity)
- **BLE Off, WiFi Always Connected:** ~39.6 mA → ~47.3 hours (1.97 days) realistic (best balance)
- **BLE & WiFi Disconnected:** ~2.9 mA → ~637.5 hours (26.56 days) realistic (ultra low power, no transmission)
- **Mixed (On/Off Reconnection):** ~32.5 mA → ~57.5 hours (2.40 days) realistic (realistic field deployment)
- **Average (Typical Deployment):** ~24.9 mA → ~75.1 hours (3.13 days) realistic (typical long-term usage)

**Battery:** 2200 mAh Li-Po (3.7V)  
**Typical Runtime:** ~3.13 days (realistic, average scenario)  
**Best Case Runtime:** ~1.97 days (WiFi always connected)  
**Worst Case Runtime:** ~1.00 day (BLE always connected)  
**Ultra Low Power:** ~26.56 days (disconnected, no transmission)

**Key Power Consumers:**
- **ESP32C6 Active:** ~150 mA (WiFi + Processing)
- **ESP32C6 Idle:** ~30-40 mA (Light sleep/delay loop with BLE active)
- **ESP32C6 Deep Sleep:** ~0.01 mA (After BLE stops, if WiFi connected and transmission successful: 30s sleep, else 5min sleep)
- **BLE Advertising:** ~5 mA (Only active on power-on or reset button, stops after 1 minute if disconnected)
- **Status LEDs:** ~15 mA (2x NeoPixels always on: GPIO 48 status LED, GPIO 38 battery LED)
- **Sensors:** ~33.5 mA (Active only during reading)

---

## ⚠️ CRITICAL HARDWARE WARNING

**Voltage Connection:**
You mentioned connecting a **2200mAh Li-Po battery** to the **3.3V pin** of the board.

> **DO NOT CONNECT A Li-Po BATTERY (3.7V - 4.2V) DIRECTLY TO THE 3.3V PIN!**

- The ESP32C6 and sensors typically have a maximum voltage rating of **3.6V**.
- A fully charged Li-Po battery is **4.2V**.
- Connecting 4.2V to the 3.3V rail will likely **permanently damage** the ESP32C6, IMU, and GPS.

**Correct Connection:**
- Connect the Li-Po battery to the **BAT** or **5V/VIN** pin (which feeds the onboard voltage regulator).
- OR use an external 3.3V Low Dropout Regulator (LDO) between the battery and the 3.3V pin.

*The calculations below assume the system is powered correctly (e.g., via LDO) and the current drawn from the battery is roughly equal to the system current consumption (linear regulator assumption).*

---

## Component Power Requirements

### ESP32C6 Microcontroller

| State | Current Draw | Duration/Cycle | Notes |
|-------|-------------|-----------------|-------|
| Active (WiFi Connect/TX) | 100-150 mA | ~4-5 seconds | WiFi association & HTTP POST |
| Active (Processing) | 60-80 mA | ~3-4 seconds | Sensor stabilization & reading |
| Idle (Loop delay) | 30-40 mA | ~22 seconds | `delay()` loop, BLE stack active |
| **Recommendation** | - | - | Use `esp_light_sleep_start()` instead of `delay()` |

### Sensors & Peripherals

| Component | Active Current | Sleep Current | Active Duration | Notes |
|-----------|----------------|---------------|-----------------|-------|
| **BMI323 IMU** | 3.5 mA | 0.005 mA | ~3 sec | Powered off/sleep between readings |
| **AT6558 GPS** | 30 mA | 0.05 mA | ~3 sec | Powered off/sleep between readings |
| **Status LED** | 7.5 mA | 7.5 mA | Always On | GPIO 48 (NeoPixel) |
| **Battery LED** | 7.5 mA | 7.5 mA | Always On | GPIO 38 (NeoPixel) |
| **BLE Radio** | ~5 mA | 0 mA | Conditional | Only on power-on/reset, stops after 1 min if disconnected |
| **WiFi TX LED** | 2.5 mA | 0 mA | Pulse | GPIO 40 (20ms pulses during TX) |
| **BLE Activity LED** | 2.5 mA | 0 mA | Pulse | GPIO 41 (20ms pulses during BLE activity) |

---

## Power Consumption Analysis (30-Second Cycle)

### 1. Active Phase (~8 seconds)
*Events: Wake sensors, stabilize (2s), connect WiFi (~3s), read sensors (1s), transmit (~1-2s), power down.*

```
ESP32C6 (Avg Active):  120 mA × 8 sec     = 960 mAs
Sensors (IMU+GPS):      33.5 mA × 3 sec    = 100.5 mAs
Status LEDs (x2):       15 mA × 8 sec      = 120 mAs
BLE Advertising:        5 mA × 8 sec       = 40 mAs
WiFi Pulse LED:         2.5 mA × 2 sec     = 5 mAs
                                            ──────────
                                 Subtotal:  1,225.5 mAs
```

### 2. Idle Phase (~22 seconds)
*Events: Sensors off, WiFi off, BLE advertising (if still active), LEDs on, main loop delay. After BLE stops (>1 min disconnected), device enters deep sleep.*

**Scenario A: BLE Still Active (First Minute)**
```
ESP32C6 (Idle/BLE):    35 mA × 22 sec     = 770 mAs
Sensors (Sleep):        0.1 mA × 22 sec    = 2.2 mAs
Status LEDs (x2):       15 mA × 22 sec     = 330 mAs
BLE Advertising:        Included in ESP32  = 0 mAs
                                            ──────────
                                 Subtotal:  1,102.2 mAs
```

**Scenario B: BLE Stopped, Deep Sleep (After 1 Minute)**
```
ESP32C6 (Deep Sleep):  0.01 mA × 22 sec    = 0.22 mAs
Sensors (Sleep):        0.1 mA × 22 sec    = 2.2 mAs
Status LEDs (x2):       0 mA × 22 sec      = 0 mAs (LEDs off in deep sleep)
BLE:                    0 mA × 22 sec      = 0 mAs
                                            ──────────
                                 Subtotal:  2.42 mAs (much lower!)
```

### 3. Total Per Cycle

```
Total Charge per Cycle: 1,225.5 + 1,102.2 = 2,327.7 mAs
Average Current:        2,327.7 mAs / 30 sec = 77.59 mA
```

---

## Detailed Power Consumption Scenarios

This section provides comprehensive breakdowns for different connection states and operational modes. Each scenario includes cycle-by-cycle power consumption, total energy per cycle, average current, and battery life estimates.

---

### Scenario 1: BLE Always Connected (Worst Case for Power, Best for Connectivity)

**Conditions:**
- BLE stays connected throughout entire operation (never disconnects)
- WiFi may or may not be connected (transmission attempts via BLE if WiFi fails)
- No deep sleep (BLE keeps device awake)
- Device runs continuous 30-second cycles indefinitely
- LEDs always on

**Cycle Breakdown (30 seconds per cycle):**

**Active Phase (8 seconds):**
```
Component                    Current    Duration    Energy
─────────────────────────────────────────────────────────
ESP32C6 (Active/WiFi)       120 mA     8 sec      960 mAs
IMU Sensor                   3.5 mA     3 sec      10.5 mAs
GPS Sensor                   30 mA      3 sec      90 mAs
Status LED (GPIO 48)         7.5 mA     8 sec      60 mAs
Battery LED (GPIO 38)        7.5 mA     8 sec      60 mAs
BLE Radio (Advertising)     5 mA       8 sec      40 mAs
WiFi TX LED (GPIO 40)        2.5 mA     2 sec      5 mAs
BLE Activity LED (GPIO 41)   2.5 mA     1 sec      2.5 mAs
─────────────────────────────────────────────────────────
Active Phase Subtotal:                   8 sec      1,228.0 mAs
```

**Idle Phase (22 seconds):**
```
Component                    Current    Duration    Energy
─────────────────────────────────────────────────────────
ESP32C6 (Idle/BLE active)   35 mA      22 sec     770 mAs
IMU Sensor (Sleep)           0.005 mA  22 sec     0.11 mAs
GPS Sensor (Sleep)           0.05 mA   22 sec      1.1 mAs
Status LED (GPIO 48)         7.5 mA     22 sec     165 mAs
Battery LED (GPIO 38)        7.5 mA     22 sec     165 mAs
BLE Radio (Included in ESP)  -         22 sec     0 mAs
─────────────────────────────────────────────────────────
Idle Phase Subtotal:                     22 sec     1,101.21 mAs
```

**Total Per Cycle:**
```
Total Energy per Cycle:      1,228.0 + 1,101.21 = 2,329.21 mAs
Cycle Duration:              30 seconds
Average Current:             2,329.21 mAs / 30 sec = 77.64 mA
```

**Battery Life Calculation (2200 mAh):**
```
Total Cycles:                2200 mAh / 77.64 mA = 28.34 hours
Ideal Runtime:               28.34 hours (1.18 days)
Realistic Runtime (85%):     24.09 hours (1.00 days)
Conservative Runtime (70%):  19.84 hours (0.83 days)
Sensor Readings:             ~2,890 readings per charge
```

**Key Characteristics:**
- Highest power consumption scenario
- Best connectivity (BLE always available for data transmission)
- No deep sleep (device always awake)
- Continuous operation at 30-second intervals
- Suitable for applications requiring constant connectivity

**Detailed Calculation Table (Per Cycle):**

| Component | Phase | Current (mA) | Duration (sec) | Energy (mAs) | Cycle Duration (sec) |
|-----------|-------|--------------|----------------|--------------|----------------------|
| ESP32C6 (Active/WiFi) | Active | 120.00 | 8.00 | 960.00 | 30.00 |
| IMU Sensor | Active | 3.50 | 3.00 | 10.50 | 30.00 |
| GPS Sensor | Active | 30.00 | 3.00 | 90.00 | 30.00 |
| Status LED (GPIO 48) | Active | 7.50 | 8.00 | 60.00 | 30.00 |
| Battery LED (GPIO 38) | Active | 7.50 | 8.00 | 60.00 | 30.00 |
| BLE Radio (Advertising) | Active | 5.00 | 8.00 | 40.00 | 30.00 |
| WiFi TX LED (GPIO 40) | Active | 2.50 | 2.00 | 5.00 | 30.00 |
| BLE Activity LED (GPIO 41) | Active | 2.50 | 1.00 | 2.50 | 30.00 |
| ESP32C6 (Idle/BLE active) | Idle | 35.00 | 22.00 | 770.00 | 30.00 |
| IMU Sensor (Sleep) | Idle | 0.005 | 22.00 | 0.11 | 30.00 |
| GPS Sensor (Sleep) | Idle | 0.05 | 22.00 | 1.10 | 30.00 |
| Status LED (GPIO 48) | Idle | 7.50 | 22.00 | 165.00 | 30.00 |
| Battery LED (GPIO 38) | Idle | 7.50 | 22.00 | 165.00 | 30.00 |
| **TOTAL PER CYCLE** | - | **77.64** | **30.00** | **2,329.21** | **30.00** |

---

### Scenario 2: BLE Off, WiFi Always Connected (Best Balance - Power & Connectivity)

**Conditions:**
- BLE stops after 1 minute (disconnected or not used)
- WiFi always connected and transmission always successful
- Deep sleep for 30 seconds after each cycle (after BLE stops)
- Device wakes every 30 seconds for sensor reading
- LEDs off during deep sleep

**First Minute (BLE Active - 2 cycles):**
```
Cycle 1-2: Same as Scenario 1
Total Energy: 2,329.21 mAs × 2 = 4,658.42 mAs
Duration: 60 seconds
Average Current: 77.64 mA
```

**After First Minute (BLE Stopped, WiFi Connected):**

**Active Phase (8 seconds):**
```
Component                    Current    Duration    Energy
─────────────────────────────────────────────────────────
ESP32C6 (Active/WiFi)       120 mA     8 sec      960 mAs
IMU Sensor                   3.5 mA     3 sec      10.5 mAs
GPS Sensor                   30 mA      3 sec      90 mAs
Status LED (GPIO 48)         7.5 mA     8 sec      60 mAs
Battery LED (GPIO 38)        7.5 mA     8 sec      60 mAs
WiFi TX LED (GPIO 40)        2.5 mA     2 sec      5 mAs
─────────────────────────────────────────────────────────
Active Phase Subtotal:                   8 sec      1,185.5 mAs
```

**Deep Sleep Phase (22 seconds - WiFi Connected):**
```
Component                    Current    Duration    Energy
─────────────────────────────────────────────────────────
ESP32-S3 (Deep Sleep)        0.01 mA   22 sec      0.22 mAs
IMU Sensor (Sleep)           0.005 mA  22 sec      0.11 mAs
GPS Sensor (Sleep)           0.05 mA   22 sec       1.1 mAs
Status LED (Off in sleep)    0 mA       22 sec      0 mAs
Battery LED (Off in sleep)   0 mA       22 sec      0 mAs
─────────────────────────────────────────────────────────
Deep Sleep Phase Subtotal:               22 sec     1.43 mAs
```

**Total Per Cycle (After BLE Stops):**
```
Total Energy per Cycle:      1,185.5 + 1.43 = 1,186.93 mAs
Cycle Duration:              30 seconds
Average Current:             1,186.93 mAs / 30 sec = 39.56 mA
```

**Battery Life Calculation (2200 mAh):**
```
First Minute Energy:         4,658.42 mAs (BLE active)
Remaining Energy:            (2200 mAh × 3600 sec) - 4,658.42 = 7,915,341.58 mAs
Remaining Cycles:            7,915,341.58 / 1,186.93 = 6,670 cycles
Remaining Time:              6,670 × 30 sec = 200,100 sec = 55.58 hours
Total Runtime:               1 min + 55.58 hours = 55.59 hours

Ideal Runtime:               55.59 hours (2.32 days)
Realistic Runtime (85%):     47.25 hours (1.97 days)
Conservative Runtime (70%):  38.91 hours (1.62 days)
Sensor Readings:             ~5,670 readings per charge
```

**Key Characteristics:**
- Best balance between power efficiency and connectivity
- Reliable data transmission via WiFi
- Deep sleep saves significant power (22 seconds per cycle)
- Suitable for most deployment scenarios
- WiFi connection required

**Detailed Calculation Table (Per Cycle - After BLE Stops):**

| Component | Phase | Current (mA) | Duration (sec) | Energy (mAs) | Cycle Duration (sec) |
|-----------|-------|--------------|----------------|--------------|----------------------|
| ESP32C6 (Active/WiFi) | Active | 120.00 | 8.00 | 960.00 | 30.00 |
| IMU Sensor | Active | 3.50 | 3.00 | 10.50 | 30.00 |
| GPS Sensor | Active | 30.00 | 3.00 | 90.00 | 30.00 |
| Status LED (GPIO 48) | Active | 7.50 | 8.00 | 60.00 | 30.00 |
| Battery LED (GPIO 38) | Active | 7.50 | 8.00 | 60.00 | 30.00 |
| WiFi TX LED (GPIO 40) | Active | 2.50 | 2.00 | 5.00 | 30.00 |
| ESP32C6 (Deep Sleep) | Deep Sleep | 0.01 | 22.00 | 0.22 | 30.00 |
| IMU Sensor (Sleep) | Deep Sleep | 0.005 | 22.00 | 0.11 | 30.00 |
| GPS Sensor (Sleep) | Deep Sleep | 0.05 | 22.00 | 1.10 | 30.00 |
| Status LED (Off in sleep) | Deep Sleep | 0.00 | 22.00 | 0.00 | 30.00 |
| Battery LED (Off in sleep) | Deep Sleep | 0.00 | 22.00 | 0.00 | 30.00 |
| **TOTAL PER CYCLE** | - | **39.56** | **30.00** | **1,186.93** | **30.00** |

**Note:** First minute (2 cycles) uses Scenario 1 values (BLE active).

---

### Scenario 3: BLE and WiFi Disconnected Throughout (Ultra Low Power, No Data Transmission)

**Conditions:**
- BLE stops after 1 minute (disconnected)
- WiFi never connects or always fails transmission
- Deep sleep for 5 minutes (300 seconds) after each cycle (after BLE stops)
- Device wakes every 5 minutes for sensor reading attempt
- LEDs off during deep sleep
- Data accumulates in queue (not transmitted)

**First Minute (BLE Active - 2 cycles):**
```
Cycle 1-2: Same as Scenario 1
Total Energy: 2,329.21 mAs × 2 = 4,658.42 mAs
Duration: 60 seconds
Average Current: 77.64 mA
```

**After First Minute (BLE Stopped, WiFi Disconnected):**

**Active Phase (8 seconds):**
```
Component                    Current    Duration    Energy
─────────────────────────────────────────────────────────
ESP32C6 (Active, no WiFi)   80 mA      8 sec      640 mAs
IMU Sensor                   3.5 mA     3 sec      10.5 mAs
GPS Sensor                   30 mA      3 sec      90 mAs
Status LED (GPIO 48)         7.5 mA     8 sec      60 mAs
Battery LED (GPIO 38)        7.5 mA     8 sec      60 mAs
─────────────────────────────────────────────────────────
Active Phase Subtotal:                   8 sec      860.5 mAs
```

**Deep Sleep Phase (292 seconds - 5 minutes, WiFi Failed):**
```
Component                    Current    Duration    Energy
─────────────────────────────────────────────────────────
ESP32-S3 (Deep Sleep)        0.01 mA   292 sec     2.92 mAs
IMU Sensor (Sleep)           0.005 mA  292 sec     1.46 mAs
GPS Sensor (Sleep)           0.05 mA   292 sec     14.6 mAs
Status LED (Off in sleep)    0 mA       292 sec     0 mAs
Battery LED (Off in sleep)   0 mA       292 sec     0 mAs
─────────────────────────────────────────────────────────
Deep Sleep Phase Subtotal:               292 sec    18.98 mAs
```

**Total Per Cycle (After BLE Stops, 300 seconds = 5 minutes):**
```
Total Energy per Cycle:      860.5 + 18.98 = 879.48 mAs
Cycle Duration:              300 seconds (5 minutes)
Average Current:             879.48 mAs / 300 sec = 2.93 mA
```

**Battery Life Calculation (2200 mAh):**
```
First Minute Energy:         4,658.42 mAs (BLE active)
Remaining Energy:            (2200 mAh × 3600 sec) - 4,658.42 = 7,915,341.58 mAs
Remaining Cycles:            7,915,341.58 / 879.48 = 9,000 cycles
Remaining Time:              9,000 × 300 sec = 2,700,000 sec = 750 hours
Total Runtime:               1 min + 750 hours = 750.02 hours

Ideal Runtime:               750.02 hours (31.25 days)
Realistic Runtime (85%):     637.52 hours (26.56 days)
Conservative Runtime (70%):  525.01 hours (21.88 days)
Sensor Readings:             ~9,000 readings per charge (but not transmitted)
```

**Key Characteristics:**
- Lowest power consumption scenario
- No data transmission (data queued but not sent)
- Very long battery life (3+ weeks)
- Suitable for data logging without transmission
- Queue fills up over time (limited by external flash space)

**Detailed Calculation Table (Per Cycle - After BLE Stops):**

| Component | Phase | Current (mA) | Duration (sec) | Energy (mAs) | Cycle Duration (sec) |
|-----------|-------|--------------|----------------|--------------|----------------------|
| ESP32-S3 (Active, no WiFi) | Active | 80.00 | 8.00 | 640.00 | 300.00 |
| IMU Sensor | Active | 3.50 | 3.00 | 10.50 | 300.00 |
| GPS Sensor | Active | 30.00 | 3.00 | 90.00 | 300.00 |
| Status LED (GPIO 48) | Active | 7.50 | 8.00 | 60.00 | 300.00 |
| Battery LED (GPIO 38) | Active | 7.50 | 8.00 | 60.00 | 300.00 |
| ESP32-S3 (Deep Sleep) | Deep Sleep | 0.01 | 292.00 | 2.92 | 300.00 |
| IMU Sensor (Sleep) | Deep Sleep | 0.005 | 292.00 | 1.46 | 300.00 |
| GPS Sensor (Sleep) | Deep Sleep | 0.05 | 292.00 | 14.60 | 300.00 |
| Status LED (Off in sleep) | Deep Sleep | 0.00 | 292.00 | 0.00 | 300.00 |
| Battery LED (Off in sleep) | Deep Sleep | 0.00 | 292.00 | 0.00 | 300.00 |
| **TOTAL PER CYCLE** | - | **2.93** | **300.00** | **879.48** | **300.00** |

**Note:** First minute (2 cycles) uses Scenario 1 values (BLE active).

---

### Scenario 4: Mixed - BLE and WiFi On/Off with Reconnection (Realistic Field Deployment)

**Conditions:**
- BLE connects/disconnects intermittently
- WiFi connects/disconnects intermittently
- Transmission success rate: ~70%
- Mix of connection states over extended period
- Deep sleep duration varies (30s if WiFi OK, 5min if WiFi failed)
- LEDs on during active, off during deep sleep

**Assumed Distribution Over 24 Hours:**
- **20% of cycles:** BLE connected (no deep sleep, 30s cycle)
- **30% of cycles:** BLE off, WiFi connected (30s deep sleep, 30s cycle)
- **30% of cycles:** BLE off, WiFi disconnected (5min deep sleep, 5min cycle)
- **20% of cycles:** BLE off, WiFi intermittent (mix of 30s/5min sleep)

**Detailed Breakdown:**

**Period 1: BLE Connected (20% of cycles)**
```
Cycle Duration:              30 seconds
Energy per Cycle:            2,329.21 mAs (from Scenario 1)
Average Current:             77.64 mA
Weight:                       20%
Contribution to Average:     77.64 × 0.20 = 15.53 mA
```

**Period 2: BLE Off, WiFi Connected (30% of cycles)**
```
Cycle Duration:              30 seconds
Energy per Cycle:            1,186.93 mAs (from Scenario 2)
Average Current:             39.56 mA
Weight:                       30%
Contribution to Average:     39.56 × 0.30 = 11.87 mA
```

**Period 3: BLE Off, WiFi Disconnected (30% of cycles)**
```
Cycle Duration:              300 seconds (5 minutes)
Energy per Cycle:            879.48 mAs (from Scenario 3)
Average Current:             2.93 mA
Weight:                       30%
Contribution to Average:     2.93 × 0.30 = 0.88 mA
```

**Period 4: BLE Off, WiFi Intermittent (20% of cycles)**
```
Assumed Mix:
- 50% WiFi connected (30s cycle): 39.56 mA
- 50% WiFi disconnected (5min cycle): 2.93 mA
Average: (39.56 + 2.93) / 2 = 21.25 mA
Weight: 20%
Contribution to Average: 21.25 × 0.20 = 4.25 mA
```

**Weighted Average Current:**
```
Total Average Current:       15.53 + 11.87 + 0.88 + 4.25 = 32.53 mA
```

**Battery Life Calculation (2200 mAh):**
```
Ideal Runtime:               2200 mAh / 32.53 mA = 67.63 hours (2.82 days)
Realistic Runtime (85%):     57.49 hours (2.40 days)
Conservative Runtime (70%):  47.34 hours (1.97 days)
Sensor Readings:             ~6,800 readings per charge (mixed transmission success)
```

**Key Characteristics:**
- Most realistic field deployment scenario
- Accounts for network variability
- Moderate power consumption
- Good balance of connectivity and battery life
- Represents typical real-world usage

**Detailed Calculation Table (Weighted Average Per Cycle):**

| Period | Component Mix | Cycle Duration (sec) | Energy per Cycle (mAs) | Average Current (mA) | Weight | Weighted Contribution (mA) |
|--------|---------------|---------------------|------------------------|---------------------|--------|----------------------------|
| Period 1 | BLE Connected (Scenario 1) | 30.00 | 2,329.21 | 77.64 | 20% | 15.53 |
| Period 2 | BLE Off, WiFi Connected (Scenario 2) | 30.00 | 1,186.93 | 39.56 | 30% | 11.87 |
| Period 3 | BLE Off, WiFi Disconnected (Scenario 3) | 300.00 | 879.48 | 2.93 | 30% | 0.88 |
| Period 4 | BLE Off, WiFi Intermittent (50/50 mix) | 165.00 | 1,033.21 | 21.25 | 20% | 4.25 |
| **TOTAL WEIGHTED AVERAGE** | - | - | - | **32.53** | **100%** | **32.53** |

**Component Breakdown (Period 1 - BLE Connected):**
| Component | Phase | Current (mA) | Duration (sec) | Energy (mAs) |
|-----------|-------|--------------|----------------|--------------|
| ESP32-S3 (Active/WiFi) | Active | 120.00 | 8.00 | 960.00 |
| IMU Sensor | Active | 3.50 | 3.00 | 10.50 |
| GPS Sensor | Active | 30.00 | 3.00 | 90.00 |
| Status LED (GPIO 48) | Active | 7.50 | 8.00 | 60.00 |
| Battery LED (GPIO 38) | Active | 7.50 | 8.00 | 60.00 |
| BLE Radio | Active | 5.00 | 8.00 | 40.00 |
| WiFi TX LED | Active | 2.50 | 2.00 | 5.00 |
| BLE Activity LED | Active | 2.50 | 1.00 | 2.50 |
| ESP32-S3 (Idle/BLE) | Idle | 35.00 | 22.00 | 770.00 |
| IMU Sensor (Sleep) | Idle | 0.005 | 22.00 | 0.11 |
| GPS Sensor (Sleep) | Idle | 0.05 | 22.00 | 1.10 |
| Status LED | Idle | 7.50 | 22.00 | 165.00 |
| Battery LED | Idle | 7.50 | 22.00 | 165.00 |

**Component Breakdown (Period 2 - WiFi Connected):**
| Component | Phase | Current (mA) | Duration (sec) | Energy (mAs) |
|-----------|-------|--------------|----------------|--------------|
| ESP32-S3 (Active/WiFi) | Active | 120.00 | 8.00 | 960.00 |
| IMU Sensor | Active | 3.50 | 3.00 | 10.50 |
| GPS Sensor | Active | 30.00 | 3.00 | 90.00 |
| Status LED | Active | 7.50 | 8.00 | 60.00 |
| Battery LED | Active | 7.50 | 8.00 | 60.00 |
| WiFi TX LED | Active | 2.50 | 2.00 | 5.00 |
| ESP32-S3 (Deep Sleep) | Deep Sleep | 0.01 | 22.00 | 0.22 |
| IMU Sensor (Sleep) | Deep Sleep | 0.005 | 22.00 | 0.11 |
| GPS Sensor (Sleep) | Deep Sleep | 0.05 | 22.00 | 1.10 |

**Component Breakdown (Period 3 - WiFi Disconnected):**
| Component | Phase | Current (mA) | Duration (sec) | Energy (mAs) |
|-----------|-------|--------------|----------------|--------------|
| ESP32-S3 (Active, no WiFi) | Active | 80.00 | 8.00 | 640.00 |
| IMU Sensor | Active | 3.50 | 3.00 | 10.50 |
| GPS Sensor | Active | 30.00 | 3.00 | 90.00 |
| Status LED | Active | 7.50 | 8.00 | 60.00 |
| Battery LED | Active | 7.50 | 8.00 | 60.00 |
| ESP32-S3 (Deep Sleep) | Deep Sleep | 0.01 | 292.00 | 2.92 |
| IMU Sensor (Sleep) | Deep Sleep | 0.005 | 292.00 | 1.46 |
| GPS Sensor (Sleep) | Deep Sleep | 0.05 | 292.00 | 14.60 |

---

### Scenario 5: Average Scenario (Typical Deployment Over Extended Period)

**Conditions:**
- Represents typical real-world usage over extended period (weeks/months)
- BLE active for first minute only (then stops)
- WiFi connection success rate: ~60% after BLE stops
- Mix of connection states: 60% WiFi connected, 40% WiFi disconnected
- Weighted average of scenarios 2 and 3 (after BLE stops)
- Accounts for initial BLE period

**First Minute (BLE Active - 2 cycles):**
```
Energy:                      4,658.42 mAs
Duration:                    60 seconds
Average Current:             77.64 mA
```

**After First Minute (BLE Stopped):**

**WiFi Connected (60% of time):**
```
Energy per Cycle:            1,186.93 mAs (from Scenario 2)
Cycle Duration:              30 seconds
Average Current:             39.56 mA
Time Weight:                 60%
Energy Contribution:         39.56 mA × 0.60 = 23.74 mA (time-weighted)
```

**WiFi Disconnected (40% of time):**
```
Energy per Cycle:            879.48 mAs (from Scenario 3)
Cycle Duration:              300 seconds (5 minutes)
Average Current:             2.93 mA
Time Weight:                 40%
Energy Contribution:         2.93 mA × 0.40 = 1.17 mA (time-weighted)
```

**Weighted Average (After BLE Stops, Time-Weighted):**
```
Average Current:             23.74 + 1.17 = 24.91 mA
```

**Overall Average (Accounting for First Minute):**
```
For a typical 24-hour period:
First minute (BLE active):   77.64 mA × 60 sec = 4,658.4 mAs
Remaining 1,439 minutes:     24.91 mA × 1,439 min × 60 sec = 2,149,229.4 mAs
Total Energy:                2,153,887.8 mAs
Average Current:              2,153,887.8 / (24 × 3600) = 24.93 mA
```

**Note:** The weighted average accounts for different cycle durations (30s vs 5min) by weighting by time, not by number of cycles. This gives a more accurate representation of power consumption over extended periods.

**Battery Life Calculation (2200 mAh):**
```
Ideal Runtime:               2200 mAh / 24.91 mA = 88.32 hours (3.68 days)
Realistic Runtime (85%):      75.07 hours (3.13 days)
Conservative Runtime (70%):   61.82 hours (2.58 days)
Sensor Readings:             ~9,000 readings per charge (60% transmission success)
```

**Key Characteristics:**
- Represents typical long-term deployment
- Accounts for network variability over time
- Good balance of all factors
- Most representative of real-world usage
- Suitable for planning and estimation

**Detailed Calculation Table (Time-Weighted Average Per Cycle):**

| Period | Connection State | Cycle Duration (sec) | Energy per Cycle (mAs) | Average Current (mA) | Time Weight | Weighted Contribution (mA) |
|--------|-----------------|---------------------|------------------------|---------------------|-------------|---------------------------|
| First Minute | BLE Active (Scenario 1) | 30.00 | 2,329.21 | 77.64 | 0.07% | 0.05 |
| After BLE Stops | WiFi Connected (60%) | 30.00 | 1,186.93 | 39.56 | 60% | 23.74 |
| After BLE Stops | WiFi Disconnected (40%) | 300.00 | 879.48 | 2.93 | 40% | 1.17 |
| **TOTAL WEIGHTED AVERAGE** | - | - | - | **24.91** | **100%** | **24.91** |

**Component Breakdown (WiFi Connected - 60% of time):**
| Component | Phase | Current (mA) | Duration (sec) | Energy (mAs) |
|-----------|-------|--------------|----------------|--------------|
| ESP32-S3 (Active/WiFi) | Active | 120.00 | 8.00 | 960.00 |
| IMU Sensor | Active | 3.50 | 3.00 | 10.50 |
| GPS Sensor | Active | 30.00 | 3.00 | 90.00 |
| Status LED (GPIO 48) | Active | 7.50 | 8.00 | 60.00 |
| Battery LED (GPIO 38) | Active | 7.50 | 8.00 | 60.00 |
| WiFi TX LED (GPIO 40) | Active | 2.50 | 2.00 | 5.00 |
| ESP32-S3 (Deep Sleep) | Deep Sleep | 0.01 | 22.00 | 0.22 |
| IMU Sensor (Sleep) | Deep Sleep | 0.005 | 22.00 | 0.11 |
| GPS Sensor (Sleep) | Deep Sleep | 0.05 | 22.00 | 1.10 |

**Component Breakdown (WiFi Disconnected - 40% of time):**
| Component | Phase | Current (mA) | Duration (sec) | Energy (mAs) |
|-----------|-------|--------------|----------------|--------------|
| ESP32-S3 (Active, no WiFi) | Active | 80.00 | 8.00 | 640.00 |
| IMU Sensor | Active | 3.50 | 3.00 | 10.50 |
| GPS Sensor | Active | 30.00 | 3.00 | 90.00 |
| Status LED (GPIO 48) | Active | 7.50 | 8.00 | 60.00 |
| Battery LED (GPIO 38) | Active | 7.50 | 8.00 | 60.00 |
| ESP32-S3 (Deep Sleep) | Deep Sleep | 0.01 | 292.00 | 2.92 |
| IMU Sensor (Sleep) | Deep Sleep | 0.005 | 292.00 | 1.46 |
| GPS Sensor (Sleep) | Deep Sleep | 0.05 | 292.00 | 14.60 |

---

## Battery Life Summary Table

| Scenario | Average Current | Ideal Runtime | Realistic Runtime | Conservative Runtime | Sensor Readings |
|----------|----------------|---------------|-------------------|----------------------|----------------|
| **1. BLE Always Connected** | 77.64 mA | 28.3 hours (1.18 days) | 24.1 hours (1.00 days) | 19.8 hours (0.83 days) | ~2,890 |
| **2. BLE Off, WiFi Always Connected** | 39.56 mA* | 55.6 hours (2.32 days) | 47.3 hours (1.97 days) | 38.9 hours (1.62 days) | ~5,670 |
| **3. BLE & WiFi Disconnected** | 2.93 mA* | 750 hours (31.25 days) | 637.5 hours (26.56 days) | 525 hours (21.88 days) | ~9,000** |
| **4. Mixed (On/Off Reconnection)** | 32.53 mA | 67.6 hours (2.82 days) | 57.5 hours (2.40 days) | 47.3 hours (1.97 days) | ~6,800 |
| **5. Average (Typical Deployment)** | 24.91 mA | 88.3 hours (3.68 days) | 75.1 hours (3.13 days) | 61.8 hours (2.58 days) | ~9,000 |

*After first minute (BLE active period)
**Data not transmitted, stored in queue

**Key Insights:**
- **Best Power Efficiency:** Scenario 3 (disconnected) - 26.56 days, but no data transmission
- **Best Balance:** Scenario 2 (WiFi always connected) - 1.97 days with reliable transmission
- **Worst Power Efficiency:** Scenario 1 (BLE always connected) - 1.00 day, but best connectivity
- **Realistic Deployment:** Scenario 5 (average) - 3.13 days with typical connection patterns
- **Field Deployment:** Scenario 4 (mixed) - 2.40 days with network variability

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

2.  **Deep Sleep (Already Implemented):**
    *   Device automatically enters deep sleep after BLE stops (>1 minute disconnected).
    *   ESP32C6 current drops from ~35mA to ~0.01mA during sleep.
    *   **Saving:** ~35mA * 22s = 770mAs per cycle (after BLE stops).
    *   **Current Behavior:** Deep sleep for 30 seconds if WiFi connected and transmission successful, 5 minutes if WiFi failed.
    *   **Note:** Deep sleep is already implemented and active after BLE auto-stops.

3.  **Increase Reading Interval:**
    *   Change `READING_INTERVAL` from 30s to 60s or 5 minutes.
    *   **At 60s:** Runtime doubles (~48 hours).
    *   **At 5 min:** Runtime ~4-5 days.

## Conclusion

With the current firmware (LEDs always on, deep sleep after BLE stops, BLE only active on power-on/reset and stops after 1 minute if disconnected) and a **2200mAh battery**, runtime varies significantly based on connection state:

**Runtime by Scenario (Realistic 85% Efficiency):**
- **BLE Always Connected:** ~24.1 hours (1.00 day) - Worst case, best connectivity
- **BLE Off, WiFi Always Connected:** ~47.3 hours (1.97 days) - Best balance
- **BLE & WiFi Disconnected:** ~637.5 hours (26.56 days) - Ultra low power, no data transmission
- **Mixed (On/Off Reconnection):** ~57.5 hours (2.40 days) - Realistic field deployment
- **Average (Typical Deployment):** ~75.1 hours (3.13 days) - Typical real-world usage

**Current Power Management Features:**
- ✅ Deep sleep implemented (after BLE stops, 30s if WiFi OK, 5min if WiFi failed)
- ✅ BLE auto-stops after 1 minute if disconnected (saves ~5mA)
- ✅ Sensors powered off between readings
- ✅ WiFi powered off between cycles
- ⚠️ LEDs always on (Status LED GPIO 48, Battery LED GPIO 38)

**To Extend Runtime Further:**
- Turn off LEDs during idle (saves ~15mA, extends runtime by ~30-40%)
- Increase reading interval beyond 30 seconds (doubles runtime at 60s, 4-5 days at 5min)
- Use light sleep instead of delay() in main loop (when BLE active, saves ~35mA during idle)
- Optimize WiFi connection time (reduce active phase duration)

**Recommendations:**
- For **maximum connectivity:** Use Scenario 1 (BLE always connected) - 1.00 day runtime
- For **best balance:** Use Scenario 2 (WiFi always connected) - 1.97 days runtime
- For **maximum battery life:** Use Scenario 3 (disconnected) - 26.56 days runtime (data queued)
- For **realistic planning:** Use Scenario 5 (average) - 3.13 days runtime

Ensure you connect the battery to the correct power input (BAT/VIN) to avoid damaging the 3.3V components.
