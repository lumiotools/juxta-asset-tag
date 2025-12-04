# ESP32C6 Asset Tracking System - Complete Overview (v2)

## Executive Summary

The ESP32C6 Asset Tracking System (v2) is an autonomous IoT device designed to continuously monitor and transmit location, orientation, and motion data from mobile assets. The system combines a high-precision 6-axis inertial measurement unit (IMU) for motion tracking with a professional-grade GPS receiver for global positioning, all powered by a rechargeable 2200mAh Li-Po battery. The device operates on a power-efficient 30-second duty cycle, automatically waking up sensors, collecting comprehensive motion and location data, transmitting via WiFi, and then powering down to conserve energy. With intelligent retry logic and data queuing stored on external flash, the system ensures reliable data transmission even during network outages, while visual LED indicators provide real-time status and battery level feedback. The system also includes Bluetooth Low Energy (BLE) functionality for wireless WiFi credential configuration, allowing easy setup without physical access to the device.

The system transmits data as structured JSON payloads over HTTP (WiFi) or BLE to a remote server, with each transmission including unique device identification (hardcoded device ID), time-synchronized timestamps, battery level, and comprehensive sensor data. The device implements sophisticated power management strategies, including sensor sleep modes, WiFi power cycling, continuous battery monitoring, and BLE advertising for credential setup (only on power-on or reset button press, auto-stops after 1 minute if disconnected), resulting in approximately 1.2 days (28.6 hours) of continuous operation per charge cycle. Visual feedback is provided through four LED indicators: a status LED (GPIO 48) showing sensor initialization state, a battery level LED (GPIO 38) with color-coded charge indication, a WiFi TX pulse LED (GPIO 40) that activates during WiFi transmissions, and a BLE LED (GPIO 41) that indicates BLE activity.

### System Operation Overview

The device operates through a well-defined sequence of initialization and continuous monitoring cycles. Upon power-on, the system performs a comprehensive self-check, initializes all sensors and communication systems, establishes network connectivity, synchronizes time with internet time servers, and enters a continuous autonomous operation mode. The 30-second operational cycle is carefully orchestrated to balance data freshness with power efficiency, with sensors active for only 6-7 seconds per cycle. During each cycle, the system collects multiple samples from both orientation and GPS sensors over a 1-second period, ensuring accurate and stable readings before powering down. The transmission system includes intelligent retry logic that automatically queues failed transmissions and batches them for retry, with alerts when persistent network failures require alternative communication methods.

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

**Continuous Monitoring Cycle (Repeats every 30 seconds):**
- System automatically wakes from low-power idle state
- Orientation and GPS sensors powered on and allowed to stabilize
- WiFi connection re-established for data transmission
- Comprehensive sensor data collection performed:
  - Motion data: acceleration (x, y, z), rotation rate (gyroscope x, y, z), temperature
  - Location data: GPS coordinates (latitude/longitude), altitude, speed, heading, satellite count, fix quality
- Battery level measured and included in data packet
- Current timestamp synchronized with network time
- Unique device identifier included for tracking
- Data packet prepared for transmission

**Data Transmission and Retry Logic:**
- Current sensor data is always added to queue first
- System attempts to transmit all queued data as JSON array (single item or multiple items) via WiFi or BLE
- Transmission handler tries BLE first if connected, then WiFi if BLE not available
- If transmission succeeds: queue cleared, cycle continues normally
- If transmission fails: data remains in queue for next cycle retry
- Queue persists to external SPI flash (W25Q128), survives power cycles
- Maximum queue capacity calculated dynamically based on available flash space (typically 3-10 items)
- Queue automatically clears when successful transmission occurs

**Power Conservation Phase:**
- Sensors immediately powered down after data collection
- WiFi radio completely disabled to conserve energy
- If BLE was stopped (disconnected for >1 minute): System enters deep sleep (30 seconds if WiFi connected and transmission successful, 5 minutes if WiFi failed)
- If BLE still active: System enters low-power idle state
- Battery monitoring continues at reduced frequency (every 5 seconds)
- Visual indicators remain active for status feedback
- System remains in idle for approximately 23 seconds until next cycle (or deep sleep if BLE stopped)

**Continuous Background Monitoring:**
- Battery level indicator updates every 5 seconds with color-coded status
  - Green: Excellent charge (75-100%)
  - Yellow: Low charge (25-75%)
  - Red: Critical charge (0-24%)
- Critical battery warning activates when charge drops below 10% (visual alert)
- Transmission indicator briefly flashes during each WiFi data transmission
- Status indicator continuously displays sensor health (green = operational, red = error)
- System continuously monitors for next cycle trigger

**Power Management Features:**
- Sensors automatically enter sleep mode during idle periods (reduces power by ~33mA)
- WiFi radio completely powered off between cycles (reduces power by ~115mA)
- BLE only active on power-on or reset button press, auto-stops after 1 minute if disconnected (saves ~5mA)
- After BLE stops, system enters deep sleep mode (reduces power to ~0.01mA during sleep)
- Battery monitoring operates at minimal frequency (every 5 seconds) to reduce overhead
- Visual indicators optimized for power efficiency (battery LED at 78% brightness, status LED at 100%)
- Average power consumption: ~77.6mA (including BLE when active and battery monitoring LED)
- Estimated runtime: ~24 hours (realistic) per full charge cycle with 2200mAh battery

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
│                    ESP32C6 Microcontroller                 │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │   IMU         │  │   GPS        │  │  ADC         │     │
│  │  BMI323       │  │  AT6558      │  │  Battery     │     │
│  │  6-axis       │  │  Position    │  │  Monitor     │     │
│  └──────────────┘  └──────────────┘  └──────────────┘     │
│        ↓                ↓                    ↓               │
│  ┌────────────────────────────────────────────────────┐    │
│  │     Sensor Data Collection & JSON Creation        │    │
│  │  (Every 30 seconds, ~6-7 seconds active time)     │    │
│  └────────────────────────────────────────────────────┘    │
│        ↓                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │     Transmission Handler with Retry Logic         │    │
│  │  • Queue management (dynamic max size, flash backed) │    │
│  │  • Always sends as JSON array (1+ items)          │    │
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
- **ESP32C6**
  - Single-core RISC-V 160 MHz processor
  - 512KB SRAM, 4MB Flash (on-chip)
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
  - Purpose: WiFi credentials storage and data queue persistence
  - Queue persistence: Survives power cycles, loaded on boot

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

### 30-Second Main Cycle

```
TIME: 0s
├─ Wake from idle
├─ Power on IMU and GPS
├─ Wait 2 seconds (sensor stabilization)
├─
TIME: 2s
├─ Reconnect WiFi
├─ Wait 1 second (WiFi stabilization)
├─
TIME: 3s
├─ Collect sensor data (20 updates × 50ms)
│  ├─ Read IMU: acceleration, gyroscope, temperature
│  └─ Read GPS: position, speed, satellites, etc.
├─
TIME: 4s
├─ Create JSON payload with:
│  ├─ device_id (hardcoded)
│  ├─ battery_level (percentage from BatteryMonitor)
│  ├─ timestamp (NTP synced Unix epoch)
│  ├─ imu data (accelerometer, gyroscope, temperature)
│  └─ gps data (fix, coordinates, altitude, speed, heading, satellites)
├─
TIME: 4.1s
├─ Add current data to queue
├─ Create JSON array from all queued items (always array format)
├─ Attempt transmission via BLE (if connected) or WiFi (if connected)
│  ├─ If success: Clear queue, continue
│  └─ If failure: Data remains in queue for next cycle
├─
TIME: 6-7s (WiFi dependent)
├─ Power off IMU
├─ Power off GPS
├─ Disconnect and power off WiFi
├─
TIME: 7s onwards
├─ If BLE stopped (>1 min disconnected): Enter deep sleep
│  ├─ 30 seconds if WiFi connected and transmission successful
│  └─ 5 minutes if WiFi failed or not connected
├─ If BLE still active: Idle until next 30-second mark
├─ LED updates (every 5 seconds)
└─ Loop continues
```

### Energy Timeline per Cycle

| Phase | Duration | Power Draw | Energy |
|-------|----------|-----------|--------|
| Sensor warmup | 2 sec | 110 mA | 0.061 mWh |
| WiFi connect | 1-2 sec | 130 mA | 0.072 mWh |
| Data collection | 1 sec | 140 mA | 0.051 mWh |
| WiFi transmission | 1-2 sec | 140 mA | 0.077 mWh |
| Idle/sleep | 23-24 sec | 15 mA | 0.097 mWh |
| **Total per 30s** | **30 sec** | **~54 mA avg** | **0.358 mWh** |

---

## Data Flow

### JSON Payload Structure

**Transmission Format (Always JSON Array):**

Single item (queue had only current data):
```json
[
  {
    "device_id": "ASSET_TAG_001",
    "battery_level": 85,
    "timestamp": "2024-11-15T10:30:00Z",
    "imu": {
      "accelerometer": { "x": 0.123, "y": -0.234, "z": 9.81 },
      "gyroscope": { "x": 0.012, "y": -0.034, "z": 0.001 },
      "temperature": 25.5
    },
    "gps": {
      "fix": true,
      "fixType": 3,
      "satellites": 12,
      "latitude": 37.7749,
      "longitude": -122.4194,
      "altitude": 45.3,
      "speed": 0.5,
      "heading": 123.45,
      "hdop": 1.2
    }
  }
]
```

Multiple items (queue had previous failed data):
```json
[
  { "device_id": "ASSET_TAG_001", "battery_level": 85, "timestamp": "2024-11-15T10:29:30Z", "imu": {...}, "gps": {...} },
  { "device_id": "ASSET_TAG_001", "battery_level": 84, "timestamp": "2024-11-15T10:30:00Z", "imu": {...}, "gps": {...} }
]
```

**Note:** Device ID is hardcoded as "ASSET_TAG_001" (change in code for each device), not MAC address.

**Note:** BMI323 IMU provides accelerometer, gyroscope, and temperature data only (no quaternion, euler angles, or magnetometer).

### Data Queue Lifecycle

```
Cycle 1 (WiFi/BLE OK):
  Queue: EMPTY → Add JSON1 → Create [JSON1] → Send via WiFi/BLE ✓ → Queue: EMPTY

Cycle 2 (WiFi/BLE fails):
  Queue: EMPTY → Add JSON2 → Create [JSON2] → Send ✗ → Queue: [JSON2] (saved to external flash)

Cycle 3 (WiFi/BLE still down):
  Queue: [JSON2] → Add JSON3 → Create [JSON2, JSON3] → Send ✗ → Queue: [JSON2, JSON3] (saved to external flash)

Cycle 4 (WiFi/BLE recovered):
  Queue: [JSON2, JSON3] → Add JSON4 → Create [JSON2, JSON3, JSON4] → Send via WiFi/BLE ✓ → Queue: EMPTY

Cycle 5+ (Queue persists across power cycles):
  Queue loaded from external flash (W25Q128) on boot → Continues from where it left off
```

**Note:** Queue data persists to external SPI flash (W25Q128), so failed transmissions survive power cycles and are retried on next boot.

---

## Power Management

### Power States

| Component | Active | Sleep | Idle |
|-----------|--------|-------|------|
| ESP32C6 | 100 mA | N/A | 15 mA |
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

### Transmission Handler Workflow

```
START: New sensor data arrives

STEP 1: ADD TO QUEUE
├─ Always add current data to queue first
├─ Queue persists to external flash (W25Q128) after each change
│
STEP 2: CHECK CONNECTIONS
├─ WiFi connected? → Can try WiFi transmission
├─ BLE connected? → Can try BLE transmission
├─ Neither connected? → Save to queue, return (no transmission attempt)
│
STEP 3: CREATE JSON ARRAY
├─ Convert entire queue to JSON array format
├─ Array contains 1 or more items (always array, never single object)
│
STEP 4: ATTEMPT TRANSMISSION
├─ Try BLE first (if connected)
│  ├─ Success? → Clear queue, return true
│  └─ Failure? → Continue to WiFi
├─ Try WiFi (if connected and BLE failed)
│  ├─ Success? → Clear queue, return true
│  └─ Failure? → Keep data in queue, return false
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

- **Starts:** Only on POWER_ON reset or reset button press (ESP_RST_EXT)
- **Stops:** Automatically after 1 minute if disconnected (saves power)
- **After Stop:** Device enters deep sleep (30 seconds if WiFi OK, 5 minutes if WiFi failed)
- **Purpose:** WiFi credential setup and data transmission fallback
- **LED Indicator:** GPIO 41 blinks during BLE activity

### Time Synchronization

- **Protocol:** NTP (Network Time Protocol)
- **Servers:** pool.ntp.org, time.nist.gov
- **Timing:** Once at startup, synced via WiFi
- **Timestamp:** Unix epoch (seconds since 1970-01-01)
- **Update Frequency:** Device time drifts, NTP provides accurate sync

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
├── loop()                    # Main 30-second cycle
│  ├─ BLE update (if enabled, handles connections)
│  ├─ BLE auto-stop check (after 1 minute if disconnected)
│  ├─ Battery update (every 5s)
│  ├─ Interval check (every 30s)
│  │  ├─ Power on sensors
│  │  ├─ Connect WiFi (if not connected)
│  │  ├─ Collect data (50 updates × 20ms)
│  │  ├─ Power down sensors
│  │  ├─ Create JSON
│  │  ├─ Send with retry logic (always array format)
│  │  ├─ Deep sleep check (if BLE stopped)
│  │  └─ Disconnect WiFi
│  └─ Delay 100ms
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
└── v2/                       # Version 2 documentation (ESP32C6, BMI323, AT6558, W25Q128)
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
    ↓
15. Update Status LED → Green if both initialized, else Red
    ↓
16. WiFi connect (read credentials from external flash)
    ↓
17. NTP time sync (sets system clock)
    ↓
18. Enter main loop (30-second cycles begin, BLE stops after 1 min if disconnected)
```

---

## Real-World Deployment

### Prerequisites

1. **WiFi Network**
   - SSID and password (stored in external flash on first boot)
   - 2.4GHz frequency (ESP32C6 supports 2.4GHz)

2. **Remote Server**
   - URL configured in `customwifi.h` → `SERVER_URL`
   - Accepts HTTP POST with JSON payload
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
2. Upload sketch to ESP32C6
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
| JSON creation | ~50-100ms | Depends on data volume |
| WiFi connect | 1-2 seconds | Depends on signal |
| HTTP POST | 1-3 seconds | Depends on server response |
| Battery read & calculate | <1ms | ADC only |
| LED update | <10ms | NeoPixel protocol |

### Memory Usage

| Component | RAM | Notes |
|-----------|-----|-------|
| IMU data | ~50 bytes | Single reading structure (BMI323 - no quaternion/euler) |
| GPS data | ~100 bytes | Single reading structure |
| JSON string | 300-500 bytes | Formatted payload (smaller than v1 due to no quaternion/euler) |
| Queue (max 10 items) | ~5KB | JSON strings |
| **Total** | **~6KB active** | Out of 512KB ESP32C6 RAM |

---

## Network Communication

### HTTP POST Format

```
POST /api/sensor-data HTTP/1.1
Host: your-server.com
Content-Type: application/json
Content-Length: 450

{...JSON payload...}
```

### Expected Response

```
HTTP/1.1 200 OK
Content-Type: application/json
Content-Length: 20

{"status":"received"}
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
- **Location:** GPS data exposed in JSON
- **Timestamps:** UTC epoch visible to anyone with server access

**Recommendations:**
- Use HTTPS for server connection
- Implement authentication token in header
- Consider data encryption at application layer

---

## Version Differences (v1 vs v2)

### Hardware Changes

| Component | v1 | v2 |
|-----------|----|----|
| **Microcontroller** | ESP32-S3 (Dual-core 240MHz) | ESP32C6 (Single-core RISC-V 160MHz) |
| **IMU** | BNO085 (9-axis with quaternion/euler) | BMI323 (6-axis accelerometer + gyroscope) |
| **GPS** | NEO-M9N (UBX protocol, 38400 baud) | AT6558 (NMEA protocol, 9600 baud) |
| **Storage** | Internal NVS flash | External W25Q128 SPI flash (16MB) |
| **RAM** | 320KB | 512KB |
| **Flash** | 8MB internal | 4MB internal + 16MB external |

### Software Changes

- **IMU Data:** v2 provides accelerometer, gyroscope, and temperature only (no quaternion, euler angles, or magnetometer)
- **GPS Protocol:** v2 uses NMEA instead of UBX
- **Storage:** v2 uses external SPI flash for credentials and queue persistence instead of internal NVS
- **JSON Payload:** v2 payload is smaller due to reduced IMU data fields

---

