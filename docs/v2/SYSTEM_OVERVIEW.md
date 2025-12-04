# ESP32-C3 (XIAO ESP32C3) Asset Tracking System - Complete Overview (v2)

## Executive Summary

The ESP32-C3 (XIAO ESP32C3) Asset Tracking System (v2) is an autonomous IoT device designed to continuously monitor and transmit location, orientation, and motion data from mobile assets. The system combines a high-precision 6-axis inertial measurement unit (IMU) for motion tracking with a professional-grade GPS receiver for global positioning, all powered by a rechargeable 2200mAh Li-Po battery. The device operates on a power-efficient 15-minute duty cycle, automatically waking from deep sleep, collecting comprehensive motion and location data, attempting transmission via BLE (with 60-second advertising on first cycle, 4-second on normal cycles) or WiFi, and then returning to deep sleep to maximize battery life. With intelligent retry logic and data queuing stored on external flash, the system ensures reliable data transmission even during network outages, while visual LED indicators provide real-time status and battery level feedback. The system also includes Bluetooth Low Energy (BLE) functionality for wireless WiFi credential configuration, allowing easy setup without physical access to the device.

The system transmits data as CSV (Comma-Separated Values) format over HTTP (WiFi) or BLE to a remote server, with each transmission including unique device identification (hardcoded device ID), time-synchronized timestamps, battery level, and comprehensive sensor data. The device implements sophisticated power management strategies, including sensor sleep modes, WiFi power cycling, continuous battery monitoring, and BLE advertising for credential setup (only on power-on or reset button press, auto-stops after 1 minute if disconnected), resulting in approximately 1.2 days (28.6 hours) of continuous operation per charge cycle. Visual feedback is provided through four LED indicators: a status LED (GPIO 48) showing sensor initialization state, a battery level LED (GPIO 38) with color-coded charge indication, a WiFi TX pulse LED (GPIO 40) that activates during WiFi transmissions, and a BLE LED (GPIO 41) that indicates BLE activity.

### System Operation Overview

The device operates through a well-defined sequence of initialization and continuous monitoring cycles. Upon power-on, the system performs a comprehensive self-check, initializes all sensors and communication systems, and enters a single-cycle execution mode. The 15-minute operational cycle is carefully orchestrated to maximize battery life, with the device spending 99.9% of its time in deep sleep. Each cycle wakes from deep sleep, collects sensor data, attempts transmission via BLE (prioritized) or WiFi, and returns to deep sleep. During each cycle, the system collects multiple samples from both orientation and GPS sensors over a 1-second period, ensuring accurate and stable readings before powering down. The transmission system includes intelligent retry logic that automatically queues failed transmissions and batches them for retry, with alerts when persistent network failures require alternative communication methods.

### Key Operational Steps

**System Startup and Initialization:**
- Device powers on and performs hardware self-diagnostics
- External SPI Flash (W25Q128) initialized for WiFi credential persistence and data queue persistence
- Data queue initialized (loads from external flash, calculates max size on first boot based on available flash space)
- BLE (Bluetooth Low Energy) initialized only if reset reason is POWER_ON or reset button press, begins advertising as "AssetTag-Config" for WiFi credential setup
- Battery monitoring system activates and displays current charge level via GPIO 38 LED
- Visual status indicators initialize (GPIO 48 for device status, GPIO 38 for battery, GPIO 40 for WiFi TX pulse, GPIO 41 for BLE activity)
- Orientation sensor (IMU) initializes and calibrates via I2C (GPIO 8/9)
- GPS receiver initializes and begins satellite acquisition via UART (GPIO 17/18, 9600 baud)
- WiFi connection established using stored network credentials from external flash
- System time synchronized with internet time servers (NTP)
- Status indicator confirms successful sensor initialization (green = ready, red = error)
- System enters autonomous operation mode (BLE stops after 1 minute if disconnected, then enters deep sleep)

**Continuous Monitoring Cycle (Repeats every 15 minutes):**
- System automatically wakes from deep sleep (15-minute timer)
- Collects sensor data (IMU and GPS updated 50 times over 1 second)
- Comprehensive sensor data collection performed:
  - Motion data: acceleration (x, y, z), rotation rate (gyroscope x, y, z), temperature
  - Location data: GPS coordinates (latitude/longitude), altitude, speed, heading, satellite count, fix quality
  - Last known GPS location restored from NVS if GPS fix unavailable
- Battery level measured and included in data packet
- Current timestamp (time sync verified/attempted if WiFi available)
- Unique device identifier included for tracking
- Data packet prepared for transmission

**Data Transmission and Retry Logic (Priority-Based):**
- **Step 1: Try BLE Transmission**
  - BLE starts fresh each cycle (turned on)
  - First cycle: Advertises for 60 seconds, waits for connection
  - Normal cycles: Advertises for 4 seconds, waits for connection
  - If connected within timeout: Send data via BLE → Stop BLE → Skip WiFi → Deep sleep
  - If timeout reached: Stop BLE → Proceed to WiFi
- **Step 2: Try WiFi Transmission (if BLE failed)**
  - Check if WiFi credentials exist in NVS
  - If credentials exist: Turn on WiFi → Connect → Send data → Disconnect WiFi
  - If no credentials or connection fails: Proceed to queue
- **Step 3: Queue Data (if both BLE and WiFi failed)**
  - Current sensor data added to queue (stored in external SPI flash)
  - Queue persists across power cycles
  - Maximum queue capacity calculated dynamically based on available flash space (typically 3-10 items)
  - Queue automatically cleared when successful transmission occurs
- **After Transmission:**
  - Last known GPS location saved to NVS (if valid fix exists)
  - Queue state saved to flash
  - BLE and WiFi turned off
  - Deep sleep for 15 minutes

**Power Conservation Phase (Deep Sleep Preparation):**
- Last known GPS location saved to NVS (if valid fix exists)
- Queue state saved to external flash
- BLE completely stopped and deinitialized (zero power consumption)
- WiFi radio completely turned off (zero power consumption)
- LEDs dimmed based on debug mode (0% brightness if debug mode off, 10% if debug mode on)
- System enters deep sleep for exactly 15 minutes (900 seconds)
- Device spends 99.9% of time in deep sleep (~10-20 µA power consumption)
- Wake occurs automatically after 15 minutes via timer

**Cycle Execution Pattern:**
- Single-cycle execution: Loop runs once per wake, then deep sleep
- No continuous background monitoring during cycle (maximizes battery life)
- Battery level measured once per cycle and included in data packet
- Status LED shows sensor initialization state (green = operational, red = error)
- Battery LED shows charge level (updated once per cycle)
- All monitoring occurs during active cycle phase (~5-10 seconds)

**Power Management Features (Optimized for Battery Life):**
- **Deep Sleep Dominance:** Device spends 99.9% of time in deep sleep (~10-20 µA)
- **Fixed 15-Minute Cycle:** Consistent deep sleep duration regardless of transmission success
- **BLE Power Management:**
  - Starts fresh each cycle (turned on)
  - First cycle: 60-second advertising window
  - Normal cycles: 4-second advertising window (minimal power usage)
  - Completely deinitialized between cycles (zero power consumption)
- **WiFi Power Management:**
  - Turned on only when needed (after BLE fails)
  - Completely powered off between cycles (zero power consumption)
  - Only connects if credentials exist
- **Sensor Power:** Sensors remain initialized but only active during data collection (~1 second per cycle)
- **LED Power:** Dimmed to 0% (debug mode off) or 10% (debug mode on) during deep sleep
- **Average Power Consumption:** ~0.1-0.2 mA (estimated, dominated by deep sleep)
- **Estimated Runtime:** Significantly extended due to deep sleep optimization (weeks to months depending on usage)

## Table of Contents
1. [System Architecture](#system-architecture)
2. [Hardware Components](#hardware-components)
3. [Operating Cycle](#operating-cycle)
4. [Data Flow](#data-flow)
5. [Power Management](#power-management)
6. [Communication & Retry Logic](#communication--retry-logic)
7. [LED Indicators](#led-indicators)
8. [File Structure](#file-structure)

---

## System Architecture

### High-Level Block Diagram

```
┌─────────────────────────────────────────────────────────────┐
│              ESP32-C3 (XIAO ESP32C3) Microcontroller      │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │   IMU         │  │   GPS        │  │  ADC         │     │
│  │  BMI323       │  │  AT6558      │  │  Battery     │     │
│  │  6-axis       │  │  Position    │  │  Monitor     │     │
│  └──────────────┘  └──────────────┘  └──────────────┘     │
│        ↓                ↓                    ↓               │
│  ┌────────────────────────────────────────────────────┐    │
│  │     Sensor Data Collection & CSV Creation          │    │
│  │  (Every 30 seconds, ~6-7 seconds active time)     │    │
│  └────────────────────────────────────────────────────┘    │
│        ↓                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │     Transmission Handler with Retry Logic         │    │
│  │  • Queue management (dynamic max size, flash backed) │    │
│  │  • Sends as CSV format                            │    │
│  │  • Tries BLE first, then WiFi                     │    │
│  │  • Queue persists to external SPI flash (W25Q128) │    │
│  └────────────────────────────────────────────────────┘    │
│        ↓                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │              WiFi Communication                    │    │
│  │  • Read credentials from external flash (W25Q128) │    │
│  │  • HTTP POST with 5-second timeout                │    │
│  │  • Power down after transmission                  │    │
│  └────────────────────────────────────────────────────┘    │
│        ↓                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │              BLE Communication                    │    │
│  │  • Starts only on POWER_ON or reset button        │    │
│  │  • Stops after 1 minute if disconnected           │    │
│  │  • WiFi credential setup via GATT                   │    │
│  │  • Data transmission fallback (chunked)             │    │
│  │  • Auto-save credentials to external flash when received │    │
│  └────────────────────────────────────────────────────┘    │
│        ↓                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │          Status & Battery Indication              │    │
│  │  • GPIO 48: Device Status (Green/Red)             │    │
│  │  • GPIO 38: Battery Level (Red/Yellow/Green)      │    │
│  └────────────────────────────────────────────────────┘    │
│                                                               │
└─────────────────────────────────────────────────────────────┘
         ↓                              ↓
    ┌─────────┐                  ┌──────────────┐
    │ Remote  │                  │ WiFi Network │
    │ Server  │ ←──── HTTP ────→ │              │
    └─────────┘                  └──────────────┘
```

---

## Hardware Components

### Core Processor
- **ESP32-C3 (XIAO ESP32C3)**
  - Single-core RISC-V 160 MHz processor
  - 400KB SRAM, 4MB Flash (on-chip)
  - Built-in WiFi (802.11 b/g/n)
  - Built-in Bluetooth 5.0 (BLE)
  - ADC, GPIO, UART, I2C, SPI

### Sensors
- **BMI323 IMU (6-axis)**
  - Interface: I2C (GPIO 8 SDA, GPIO 9 SCL)
  - I2C Address: 0x69
  - I2C Speed: 400 kHz
  - Outputs: Acceleration (x, y, z), Gyroscope (x, y, z), Temperature
  - Sampling: ~3 seconds per cycle (20 updates × 50ms)
  - Power: Active ~3.5mA, Sleep ~0.005mA
  - Note: BMI323 is a 6-axis IMU (accelerometer + gyroscope), does not include magnetometer

- **AT6558 GPS**
  - Interface: UART Serial2 (GPIO 18 RX, GPIO 17 TX)
  - Baud Rate: 9600 (8N1) - NMEA protocol
  - Outputs: Position, Altitude, Speed, Heading, Fix Type, HDOP, Satellites
  - Acquisition: 3-30 seconds per cycle (20 updates × 50ms)
  - Power: Active ~30mA, Sleep ~0.05mA

### External Storage
- **W25Q128 SPI Flash**
  - Capacity: 16MB (128 Mbit)
  - Interface: SPI (GPIO 10 CS, GPIO 12 MISO, GPIO 13 MOSI, GPIO 14 SCK)
  - Purpose: WiFi credentials storage, data queue persistence, and GPS location persistence
  - Queue persistence: Survives power cycles, loaded on boot
  - GPS location persistence: Last known GPS location saved to NVS (accessed via external flash), restored after wake

### Power & Battery
- **3.7V 2200mAh Li-Po Battery**
  - Chemistry: Lithium Polymer (rechargeable)
  - Max Voltage: 4.2V
  - Min Voltage: 3.0V (safe cutoff)
  - Nominal: 3.7V
  - Energy: 8.14 Wh

### Voltage Measurement
- **ADC on GPIO 4**
  - Circuit: Resistive divider (R11: 220K, R12: 68K) + RC filter (R68: 1K, C26: 100nF)
  - Divider Ratio: 4.235:1 (calculated as (220K + 68K) / 68K)
  - Filter: R68 and C26 form low-pass filter (cutoff ~1.59 kHz) for noise reduction
  - Measures: 3.0V - 4.2V battery range (0-100%)
  - Resolution: 12-bit (0-4095)
  - Reference Voltage: 3.3V
  - Attenuation: ADC_11db (full 0-3.3V range)
  - Note: R68 doesn't affect divider calculation due to high ADC input impedance

### LEDs
- **GPIO 48: Device Status LED (NeoPixel)**
  - Green: Both sensors initialized
  - Red: Sensor initialization failed
  - Brightness: 100/255 (full brightness)
  - Always on for status indication

- **GPIO 38: Battery Level LED (NeoPixel)**
  - Green (75-100%): Excellent
  - Yellow (25-75%): Low
  - Red (0-24%): Critical
  - Brightness: 200/255 (78%)
  - Updates every 5 seconds

- **GPIO 40: WiFi TX Pulse LED**
  - Indicator: Pulses when WiFi transmits
  - Duration: 20ms
  - Standard GPIO output

- **GPIO 41: BLE Activity LED**
  - Indicator: Blinks when BLE receives data or transmits
  - Duration: 20ms pulses
  - Standard GPIO output

---

## Operating Cycle

### 15-Minute Main Cycle (Deep Sleep Based)

```
WAKE: 0s (from 15-minute deep sleep)
├─ Restore last known GPS location from NVS (if available)
├─ Collect sensor data (50 updates × 20ms = 1 second)
│  ├─ Read IMU: acceleration, gyroscope, temperature
│  └─ Read GPS: position, speed, satellites, etc.
│     └─ If no fix: Use last known location from NVS
├─
TIME: ~1s
├─ Create CSV payload with:
│  ├─ device_id (hardcoded)
│  ├─ battery_level (percentage from BatteryMonitor)
│  ├─ timestamp (NTP synced if available)
│  ├─ imu data (accelerometer, gyroscope, temperature)
│  └─ gps data (fix, coordinates, altitude, speed, heading, satellites)
├─
TIME: ~1.1s
├─ STEP 1: Try BLE Transmission
│  ├─ Start BLE (begin + startAdvertising)
│  ├─ Determine timeout:
│  │  ├─ First cycle: 60 seconds
│  │  └─ Normal cycles: 4 seconds
│  ├─ Wait for connection (with timeout)
│  ├─ If connected: Send data → Stop BLE → Skip WiFi → Deep sleep
│  └─ If timeout: Stop BLE → Proceed to WiFi
├─
TIME: ~2-65s (depends on BLE timeout)
├─ STEP 2: Try WiFi Transmission (if BLE failed)
│  ├─ Check if WiFi credentials exist
│  ├─ If credentials exist:
│  │  ├─ Turn on WiFi → Connect
│  │  ├─ Verify/attempt time sync (if not already synced)
│  │  ├─ Send data via WiFi
│  │  └─ Disconnect WiFi
│  └─ If no credentials or connection fails: Proceed to queue
├─
TIME: ~3-66s
├─ STEP 3: Queue Data (if both BLE and WiFi failed)
│  └─ Add current data to queue (stored in external flash)
├─
TIME: ~3-66s
├─ STEP 4: Prepare for Deep Sleep
│  ├─ Save last known GPS location to NVS (if valid fix)
│  ├─ Save queue state to flash
│  ├─ Ensure BLE is stopped
│  ├─ Ensure WiFi is off
│  ├─ Dim LEDs (based on debug mode)
│  └─ Set deep sleep timer: 15 minutes (900 seconds)
├─
TIME: ~4-67s
└─ Enter deep sleep → Sleep for 15 minutes → Wake and repeat
```

### Energy Timeline per Cycle

| Phase | Duration | Power Draw | Energy |
|-------|----------|-----------|--------|
| Data collection | 1 sec | 140 mA | 0.039 mWh |
| BLE advertising (first cycle) | 60 sec | 5 mA | 0.083 mWh |
| BLE advertising (normal cycle) | 4 sec | 5 mA | 0.006 mWh |
| WiFi connect (if needed) | 1-2 sec | 130 mA | 0.036 mWh |
| WiFi transmission (if needed) | 1-2 sec | 140 mA | 0.039 mWh |
| Deep sleep | 15 min | 0.01-0.02 mA | 0.002-0.004 mWh |
| **Total per 15min (first cycle)** | **~62 sec active** | **~0.15 mA avg** | **~0.16 mWh** |
| **Total per 15min (normal cycle)** | **~6 sec active** | **~0.02 mA avg** | **~0.08 mWh** |

---

## Data Flow

### CSV Payload Structure

**Transmission Format (CSV - Comma-Separated Values):**

Single data record:
```csv
ASSET_TAG_007,85,2024:11:15 10:30:00,0.123,-0.234,9.810,0.012,-0.034,0.001,25.50,true,3	2024:11:15 10:30:00,12,37.7749000,-122.4194000,45.30,0.50,123.45,1.20
```

**CSV Column Format:**
```
device_id,battery_level,timestamp,
imu.accelerometer.x,imu.accelerometer.y,imu.accelerometer.z,
imu.gyroscope.x,imu.gyroscope.y,imu.gyroscope.z,
imu.temperature,
gps.fix,gps.fixType,gps.satellites,
gps.latitude,gps.longitude,gps.altitude,
gps.speed,gps.heading,gps.hdop
```

**Field Details:**
- `device_id`: Hardcoded device identifier (e.g., "ASSET_TAG_007")
- `battery_level`: Battery percentage (0-100)
- `timestamp`: Formatted as "yyyy:mm:dd hh:mm:ss"
- `imu.accelerometer.x/y/z`: Acceleration in m/s² (3 decimal places)
- `imu.gyroscope.x/y/z`: Rotation rate in rad/s (3 decimal places)
- `imu.temperature`: Temperature in °C (2 decimal places)
- `gps.fix`: "true" or "false" (string)
- `gps.fixType`: Fix type (0=no fix, 2=2D, 3=3D) + timestamp (tab-separated)
- `gps.satellites`: Number of satellites (integer)
- `gps.latitude/longitude`: Coordinates in decimal degrees (7 decimal places)
- `gps.altitude`: Altitude in meters (2 decimal places)
- `gps.speed`: Speed in m/s (2 decimal places)
- `gps.heading`: Heading in degrees (2 decimal places)
- `gps.hdop`: Horizontal Dilution of Precision (2 decimal places)

**Note:** Device ID is hardcoded in code (change `DEVICE_ID` constant for each device), not MAC address.

**Note:** BMI323 IMU provides accelerometer, gyroscope, and temperature data only (no quaternion, euler angles, or magnetometer).

**Note:** Queue system may store data in different format internally, but transmission is always CSV.

### Data Queue Lifecycle

```
Cycle 1 (WiFi/BLE OK):
  Queue: EMPTY → Add CSV1 → Send CSV1 via WiFi/BLE ✓ → Queue: EMPTY

Cycle 2 (WiFi/BLE fails):
  Queue: EMPTY → Add CSV2 → Send ✗ → Queue: [CSV2] (saved to external flash)

Cycle 3 (WiFi/BLE still down):
  Queue: [CSV2] → Add CSV3 → Send ✗ → Queue: [CSV2, CSV3] (saved to external flash)

Cycle 4 (WiFi/BLE recovered):
  Queue: [CSV2, CSV3] → Add CSV4 → Send all queued data via WiFi/BLE ✓ → Queue: EMPTY

Cycle 5+ (Queue persists across power cycles):
  Queue loaded from external flash (W25Q128) on boot → Continues from where it left off
```

**Note:** Queue data persists to external SPI flash (W25Q128), so failed transmissions survive power cycles and are retried on next boot. Queue may store data in batch format internally, but individual transmissions are CSV format.

---

## Power Management

### Power States

| Component | Active | Sleep | Idle |
|-----------|--------|-------|------|
| ESP32-C3 | 100 mA | N/A | 15 mA |
| IMU (BMI323) | 3.5 mA | 0.005 mA | - |
| GPS (AT6558) | 30 mA | 0.05 mA | - |
| WiFi | 115+ mA | 0 mA | 0 mA |
| NeoPixel | 7.5 mA | - | 7.5 mA* |
| **Total** | **~150 mA** | **~23 mA** | **~23 mA** |

*NeoPixel always on for status indication

### Battery Life Estimation

**With 2200 mAh battery:**
- Average draw: 53.9 mA
- Runtime: 40.8 hours (ideal)
- Realistic: 34.7 hours (~1.5 days)
- Sensor readings: ~4,896 transmissions

---

## Communication & Retry Logic

### Cycle-Based Transmission Workflow

```
WAKE FROM DEEP SLEEP (15 minutes elapsed)
│
├─ STEP 1: Collect Sensor Data
│  └─ IMU and GPS data collected (50 updates over 1 second)
│
├─ STEP 2: Try BLE Transmission (Priority 1)
│  ├─ Start BLE advertising
│  ├─ First cycle: Wait 60 seconds for connection
│  ├─ Normal cycles: Wait 4 seconds for connection
│  ├─ If connected: Send data → Stop BLE → Deep sleep
│  └─ If timeout: Stop BLE → Proceed to WiFi
│
├─ STEP 3: Try WiFi Transmission (Priority 2, if BLE failed)
│  ├─ Check if WiFi credentials exist
│  ├─ If credentials exist:
│  │  ├─ Turn on WiFi → Connect
│  │  ├─ Verify/attempt time sync
│  │  ├─ Send data via WiFi
│  │  └─ Disconnect WiFi
│  └─ If no credentials or connection fails: Proceed to queue
│
├─ STEP 4: Queue Data (if both BLE and WiFi failed)
│  └─ Add current data to queue (stored in external flash)
│
└─ STEP 5: Prepare for Deep Sleep
   ├─ Save last known GPS location to NVS
   ├─ Save queue state to flash
   ├─ Turn off BLE and WiFi
   └─ Deep sleep for 15 minutes
```

### Transmission Handler (Internal)

The transmission handler manages queued data and transmission attempts:

```
START: New sensor data arrives

STEP 1: SEND QUEUED DATA FIRST
├─ Read queued data from external flash
├─ Attempt transmission via BLE or WiFi
├─ If success: Remove from queue
└─ If failure: Keep in queue for next cycle
│
STEP 2: SEND CURRENT DATA
├─ Attempt direct transmission (without writing to flash)
├─ If success: Skip flash write, return success
└─ If failure: Add to queue (writes to flash)
│
END: Queue persists to external flash, will retry next cycle
```

### WiFi Credentials Management

**Storage:** External SPI Flash (W25Q128 - persistent across power cycles)

**Setup Methods:**
1. **Via BLE (Recommended):** Connect to "AssetTag-Config" BLE device (only available on power-on or reset button press, stops after 1 minute if disconnected), write SSID and password to characteristics
2. **Via Code (First Time):** Uncomment credential setup lines in sketch

```cpp
// Method 1: Via BLE (automatic)
// Connect to "AssetTag-Config" BLE device (within 1 minute of power-on)
// Write SSID to characteristic UUID: 12345678-1234-1234-1234-123456789abd
// Write Password to characteristic UUID: 12345678-1234-1234-1234-123456789abe
// Credentials automatically saved to external flash (W25Q128)
// BLE stops after 1 minute if disconnected, then device enters deep sleep

// Method 2: Via code (first time only)
// Set WiFi credentials via code (implementation depends on storage handler)
// Credentials saved to external flash (W25Q128)

// Automatic retrieval on every boot
// Credentials loaded from external flash (W25Q128)
```

### BLE Behavior

- **Starts:** Every cycle (turned on fresh each wake from deep sleep)
- **First Cycle:** Advertises for 60 seconds (allows time for initial setup)
- **Normal Cycles:** Advertises for 4 seconds (minimal power usage)
- **Connection Handling:** If connected within timeout, sends data and stops immediately
- **Timeout Handling:** If no connection within timeout, stops and proceeds to WiFi
- **Power Management:** Completely deinitialized between cycles (zero power consumption)
- **Purpose:** WiFi credential setup and data transmission (prioritized over WiFi)
- **LED Indicator:** GPIO 41 blinks during BLE activity

### Time Synchronization

- **Protocol:** NTP (Network Time Protocol)
- **Servers:** pool.ntp.org
- **Timing:** Attempted when WiFi is connected (not limited to first cycle)
- **Verification:** System checks if time is already synced before attempting
- **Retry Logic:** If sync fails, will retry in next cycle when WiFi is available
- **Timestamp:** Unix epoch (seconds since 1970-01-01)
- **Status Check:** `TimeSync::isTimeSynced()` verifies if time > Jan 1, 1970
- **Update Frequency:** Device time drifts during deep sleep, NTP provides accurate sync when WiFi available

---

## LED Indicators

### Device Status LED (GPIO 48 - NeoPixel)

| State | Color | Meaning | Action |
|-------|-------|---------|--------|
| Green | 🟢 | Both IMU & GPS initialized | Normal operation |
| Red | 🔴 | IMU or GPS failed | Check connections, restart |
| Orange | 🟠 | Initialization in progress | Wait for completion |

### Battery Level LED (GPIO 38 - NeoPixel)

| Battery % | Voltage | Color | Meaning | Action |
|-----------|---------|-------|---------|--------|
| 75-100% | 3.975-4.2V | 🟢 Green | Excellent | No action needed |
| 50-75% | 3.6-3.975V | 🟡 Yellow | Good | Monitor level |
| 25-50% | 3.225-3.6V | 🟡 Yellow | Low | Plan recharge soon |
| 10-25% | 3.03-3.225V | 🔴 Red | Critical | Charge soon |
| <10% | <3.03V | 🔴 Red (blinking) | Depleted | CHARGE IMMEDIATELY |

### WiFi TX Indicator (GPIO 40 - Standard GPIO)

| State | Behavior | Meaning |
|-------|----------|---------|
| Pulse | 20ms on-pulse | WiFi transmission event detected |
| Off | No activity | No WiFi transmission |

### BLE Activity Indicator (GPIO 41 - Standard GPIO)

| State | Behavior | Meaning |
|-------|----------|---------|
| Blink | 20ms pulses | BLE data received or transmitted |
| Off | No activity | BLE idle or stopped |

---

## File Structure

### Header Files (Configuration & Libraries)

```
juxta-asset-tag-main/
├── juxta-asset-tag-main.ino  # Main Arduino sketch
├── imu_sensor.h              # BMI323 IMU sensor driver (I2C on GPIO 8/9)
├── gps_sensor.h              # AT6558 GPS sensor driver (UART on GPIO 17/18)
├── spi_flash_handler.h        # W25Q128 external flash handler (SPI)
├── customwifi.h              # WiFi connection manager with HTTP POST
├── time_sync.h               # NTP time synchronization
├── device_id.h               # Device ID management
├── data_queue.h              # Transmission queue management (stored on external flash)
├── transmission_handler.h    # Retry logic & batch transmission
├── nvs_config.h              # Configuration storage (may use external flash)
├── battery_monitor.h         # Battery voltage reading & percentage (GPIO 4 ADC)
├── battery_indicator_led.h   # Battery LED color control (GPIO 38 NeoPixel)
└── ble_config.h              # BLE configuration for WiFi credentials setup and data transmission
```

### Main Sketch

```
juxta-asset-tag-main.ino
├── setup()                   # Initialization
│  ├─ Serial begin
│  ├─ External flash (W25Q128) initialize
│  ├─ Data queue initialize (loads from external flash, calculates max size)
│  ├─ Transmission handler initialize
│  ├─ BLE initialize (only on POWER_ON or reset button)
│  ├─ Battery ADC initialize
│  ├─ Battery LED initialize
│  ├─ Status LED initialize (GPIO 48)
│  ├─ WiFi TX LED initialize (GPIO 40)
│  ├─ BLE LED initialize (GPIO 41)
│  ├─ Sensors initialize (IMU & GPS)
│  ├─ WiFi connect
│  └─ Time sync
│
├── loop()                    # Single-cycle execution (runs once per wake)
│  ├─ Collect sensor data (50 updates × 20ms)
│  ├─ Try BLE transmission
│  │  ├─ Start BLE (begin + advertising)
│  │  ├─ Wait for connection (60s first cycle, 4s normal)
│  │  ├─ If connected: Send data → Stop BLE → Deep sleep
│  │  └─ If timeout: Stop BLE → Try WiFi
│  ├─ Try WiFi transmission (if BLE failed)
│  │  ├─ Check credentials exist
│  │  ├─ Connect WiFi → Verify/attempt time sync
│  │  ├─ Send data → Disconnect WiFi
│  │  └─ If failed: Queue data
│  ├─ Queue data (if transmission failed)
│  ├─ Save last known GPS location to NVS
│  ├─ Save queue state
│  ├─ Turn off BLE and WiFi
│  └─ Deep sleep for 15 minutes
│
├── createSensorCSV()         # Format sensor data as CSV
├── sendDataWithRetryLogic()  # Use transmission handler
├── updateStatusLED()         # Show initialization status
└── wifiEventHandler()        # WiFi TX event callback
```

### Documentation Files

```
docs/
├── v1/                       # Version 1 documentation (ESP32-S3, BNO085, NEO-M9N)
│  ├── BATTERY_ANALYSIS.md
│  ├── BATTERY_MONITORING.md
│  ├── TRANSMISSION_SYSTEM_DOCS.md
│  └── SYSTEM_OVERVIEW.md
└── v2/                       # Version 2 documentation (ESP32-C3, BMI323, AT6558, W25Q128)
   ├── BATTERY_ANALYSIS.md
   ├── BATTERY_MONITORING.md
   ├── TRANSMISSION_SYSTEM_DOCS.md
   └── SYSTEM_OVERVIEW.md (this file)
```

### BLE Configuration

The system includes BLE (Bluetooth Low Energy) functionality for WiFi credential configuration and data transmission:

- **Device Name:** "AssetTag-Config"
- **Service UUID:** 12345678-1234-1234-1234-123456789abc
- **Start Condition:** Only on POWER_ON reset or reset button press (ESP_RST_EXT)
- **Stop Condition:** Automatically stops after 1 minute if disconnected (saves power)
- **Characteristics:**
  - SSID Characteristic (UUID: 12345678-1234-1234-1234-123456789abd) - Write WiFi SSID
  - Password Characteristic (UUID: 12345678-1234-1234-1234-123456789abe) - Write WiFi password
  - Status Characteristic (UUID: 12345678-1234-1234-1234-123456789abf) - Read device status
  - Data Characteristic (UUID: 12345678-1234-1234-1234-123456789ac0) - Read/Notify sensor data (chunked)
  - Current SSID Characteristic (UUID: 12345678-1234-1234-1234-123456789ac1) - Read saved WiFi SSID and device info
- **Storage:** Credentials automatically saved to external flash (W25Q128) when received via BLE
- **Data Transmission:** Can send queued sensor data via BLE if WiFi unavailable (chunked for large payloads)
- **LED Indicator:** GPIO 41 blinks during BLE activity
- **Usage:** Connect with BLE client app within 1 minute of power-on, write SSID and password, credentials saved automatically

---

## Initialization Sequence

### At Power-On

```
1. Serial begin (115200 baud)
   ↓
2. External SPI Flash (W25Q128) initialize (read WiFi credentials)
   ↓
3. Data queue initialize (loads from external flash, calculates max size on first boot)
   ↓
4. Transmission handler initialize
   ↓
5. Check reset reason (POWER_ON or reset button?)
   ↓
6. BLE initialize (only if POWER_ON or reset button, start advertising "AssetTag-Config")
   ↓
7. Battery ADC initialize (GPIO 4)
   ↓
8. Battery Indicator LED initialize (GPIO 38, brightness 200/255)
   ↓
9. Device Status LED initialize (GPIO 48, brightness 100/255, orange initially)
   ↓
10. WiFi TX LED initialize (GPIO 40)
    ↓
11. BLE LED initialize (GPIO 41)
    ↓
12. WiFi TX event handler register
    ↓
13. IMU sensor begin() → sets imuInitialized flag
    ↓
14. GPS sensor begin() → sets gpsInitialized flag
    ├─ Restores last known GPS location from NVS (if available)
    ↓
15. Update Status LED → Green if both initialized, else Red
    ↓
16. Determine cycle type (first cycle if POWER_ON/reset button, else normal cycle)
    ↓
17. Enter main loop (single-cycle execution)
    ├─ Collect sensor data
    ├─ Try BLE transmission (60s first cycle, 4s normal)
    ├─ Try WiFi transmission (if BLE failed)
    ├─ Queue data (if both failed)
    ├─ Save last known GPS location to NVS
    └─ Deep sleep for 15 minutes → Wake and repeat
```

---

## Real-World Deployment

### Prerequisites

1. **WiFi Network**
   - SSID and password (stored in external flash on first boot)
   - 2.4GHz frequency (ESP32-C3 supports 2.4GHz)

2. **Remote Server**
   - URL configured in `customwifi.h` → `SERVER_URL`
   - Accepts HTTP POST with CSV payload (Content-Type: text/csv)
   - Returns 200 OK for success

3. **Battery**
   - 3.7V 2200mAh Li-Po with charge controller
   - Connected to VBAT rail via resistor divider

4. **NTP Access**
   - Automatic via WiFi (pool.ntp.org)
   - Syncs time on startup

### Typical Deployment Steps

```
1. Edit customwifi.h → Update SERVER_URL
2. Upload sketch to ESP32-C3 (XIAO ESP32C3)
3. Uncomment credential setup lines
4. Upload again with WiFi SSID/password
5. Comment out credential setup
6. Upload final version
7. Device will auto-connect on next boot
8. Monitor serial output (115200 baud)
9. Observe LED colors for status
```

---

## Troubleshooting

### Device Status LED is Red

**Symptom:** Device Status LED shows red instead of green

**Causes:**
- IMU not responding (I2C issue)
- GPS not responding (UART issue)
- Sensor connection loose

**Solution:**
1. Check I2C (GPIO 8/9) connections
2. Check UART (GPIO 17/18) connections
3. Restart device
4. Check serial output for specific error

### Battery LED not responding

**Symptom:** Battery LED always shows same color or no color

**Causes:**
- GPIO 38 not connected
- ADC reading stuck
- NeoPixel communication issue

**Solution:**
1. Verify GPIO 38 level shifter connection
2. Run `BatteryMonitor::printBatteryInfo()`
3. Check ADC readings vs multimeter

### WiFi not connecting

**Symptom:** Device stays offline, no data transmission

**Causes:**
- External flash credentials not set
- Wrong SSID/password
- WiFi network unreachable

**Solution:**
1. Uncomment credential setup lines in sketch
2. Verify SSID and password
3. Check WiFi network availability
4. Review serial output for connection attempts

### Data not being received

**Symptom:** Server not receiving transmissions

**Causes:**
- Wrong SERVER_URL in customwifi.h
- Server not accepting requests
- Transmission failing silently

**Solution:**
1. Verify SERVER_URL is correct
2. Test server endpoint manually
3. Check `transmission_handler` queue status via serial
4. Enable debug output in transmission functions

### External Flash Issues

**Symptom:** Queue not persisting or credentials not saving

**Causes:**
- SPI flash not initialized
- Flash write/read errors
- Flash capacity issues

**Solution:**
1. Check SPI connections (GPIO 10, 12, 13, 14)
2. Verify W25Q128 flash initialization
3. Check flash capacity and available space
4. Review serial output for flash errors

---

## Performance Metrics

### Processing Speeds

| Operation | Time | Notes |
|-----------|------|-------|
| Sensor data collection | 1 second | 20 × 50ms updates |
| CSV creation | ~10-50ms | Depends on data volume |
| WiFi connect | 1-2 seconds | Depends on signal |
| HTTP POST | 1-3 seconds | Depends on server response |
| Battery read & calculate | <1ms | ADC only |
| LED update | <10ms | NeoPixel protocol |

### Memory Usage

| Component | RAM | Notes |
|-----------|-----|-------|
| IMU data | ~50 bytes | Single reading structure (BMI323 - no quaternion/euler) |
| GPS data | ~100 bytes | Single reading structure |
| CSV string | ~300-350 bytes | Formatted payload (compact CSV format) |
| Queue (max 10 items) | ~5KB | JSON strings |
| **Total** | **~6KB active** | Out of 400KB ESP32-C3 RAM |

---

## Network Communication

### HTTP POST Format

```
POST /push/juxtatetsing HTTP/1.1
Host: echo-http-requests.appspot.com
Content-Type: text/csv
Content-Length: 350

ASSET_TAG_007,85,2024:11:15 10:30:00,0.123,-0.234,9.810,0.012,-0.034,0.001,25.50,true,3	2024:11:15 10:30:00,12,37.7749000,-122.4194000,45.30,0.50,123.45,1.20
```

### Expected Response

```
HTTP/1.1 200 OK
Content-Type: text/html
Content-Length: <varies>

<server response>
```

### Timeout Behavior

- **Connection timeout:** 5 seconds
- **Read timeout:** 5 seconds
- **Total request time:** Up to 10 seconds
- **Retry wait:** 2 seconds between attempts
- **Batch send:** No retry on array failure (stays in queue)

---

## Security Considerations

### WiFi Credentials

- **Storage:** External SPI Flash (W25Q128 - secure, persistent)
- **Transmission:** HTTPS recommended for SERVER_URL
- **Reset:** Can be cleared via flash erase

### Data Privacy

- **Device ID:** Included in payload (can identify device)
- **Location:** GPS data exposed in CSV
- **Timestamps:** UTC epoch visible to anyone with server access

**Recommendations:**
- Use HTTPS for server connection
- Implement authentication token in header
- Consider data encryption at application layer

---

## GPS Location Persistence

### Last Known Location Storage

The system implements persistent storage for the last known GPS location to ensure location data is available even when GPS fix is temporarily unavailable (e.g., indoors, during deep sleep).

**Storage Mechanism:**
- **Location:** NVS (Non-Volatile Storage) via `nvs_config.h`
- **Data Saved:** Latitude, longitude, altitude, speed, heading, satellites, HDOP, fix status
- **When Saved:** Before each deep sleep cycle (if valid GPS fix exists)
- **When Restored:** After wake from deep sleep, during GPS sensor initialization

**Implementation:**
- `GPSSensor::saveLastKnownLocation()` - Saves current lastKnownData to NVS
- `GPSSensor::restoreLastKnownLocation()` - Restores lastKnownData from NVS after wake
- Automatically called in `GPS sensor begin()` to restore location
- Automatically called before deep sleep in main loop

**Benefits:**
- Provides fallback location when GPS fix is unavailable
- Survives deep sleep and power cycles
- Automatic operation (no manual intervention)
- Efficient storage (only saves when valid fix exists)

**Usage:**
- If GPS has no fix, system uses last known location from NVS
- Location marked as stale (hasValidFix = false) but coordinates preserved
- Timestamp updated to current time to indicate staleness

---

## Version Differences (v1 vs v2)

### Hardware Changes

| Component | v1 | v2 |
|-----------|----|----|
| **Microcontroller** | ESP32-S3 (Dual-core 240MHz) | ESP32-C3 (Single-core RISC-V 160MHz) |
| **IMU** | BNO085 (9-axis with quaternion/euler) | BMI323 (6-axis accelerometer + gyroscope) |
| **GPS** | NEO-M9N (UBX protocol, 38400 baud) | AT6558 (NMEA protocol, 9600 baud) |
| **Storage** | Internal NVS flash | External W25Q128 SPI flash (16MB) |
| **RAM** | 320KB | 512KB |
| **Flash** | 8MB internal | 4MB internal + 16MB external |

### Software Changes

- **IMU Data:** v2 provides accelerometer, gyroscope, and temperature only (no quaternion, euler angles, or magnetometer)
- **GPS Protocol:** v2 uses NMEA instead of UBX
- **Storage:** v2 uses external SPI flash for credentials and queue persistence instead of internal NVS
- **Data Format:** v2 uses CSV format (more compact than JSON) with reduced IMU data fields

---

