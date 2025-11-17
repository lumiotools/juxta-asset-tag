# ESP32-S3 Asset Tracking System - Complete Overview

## Executive Summary

The ESP32-S3 Asset Tracking System is an autonomous IoT device designed to continuously monitor and transmit location, orientation, and motion data from mobile assets. The system combines a high-precision 9-axis inertial measurement unit (IMU) for orientation tracking with a professional-grade GPS receiver for global positioning, all powered by a rechargeable 2200mAh Li-Po battery. The device operates on a power-efficient 30-second duty cycle, automatically waking up sensors, collecting comprehensive motion and location data, transmitting via WiFi, and then powering down to conserve energy. With intelligent retry logic and data queuing, the system ensures reliable data transmission even during network outages, while visual LED indicators provide real-time status and battery level feedback. The system also includes Bluetooth Low Energy (BLE) functionality for wireless WiFi credential configuration, allowing easy setup without physical access to the device.

The system transmits data as structured JSON payloads over HTTP to a remote server, with each transmission including unique device identification, time-synchronized timestamps, battery level, and comprehensive sensor data. The device implements sophisticated power management strategies, including sensor sleep modes, WiFi power cycling, continuous battery monitoring, and BLE advertising for credential setup, resulting in approximately 1.2 days (28.6 hours) of continuous operation per charge cycle. Visual feedback is provided through three LED indicators: a status LED (GPIO 39) showing sensor initialization state, a battery level LED (GPIO 38) with color-coded charge indication, and a pulse LED (GPIO 40) that activates during WiFi transmissions.

### System Operation Overview

The device operates through a well-defined sequence of initialization and continuous monitoring cycles. Upon power-on, the system performs a comprehensive self-check, initializes all sensors and communication systems, establishes network connectivity, synchronizes time with internet time servers, and enters a continuous autonomous operation mode. The 30-second operational cycle is carefully orchestrated to balance data freshness with power efficiency, with sensors active for only 6-7 seconds per cycle. During each cycle, the system collects multiple samples from both orientation and GPS sensors over a 1-second period, ensuring accurate and stable readings before powering down. The transmission system includes intelligent retry logic that automatically queues failed transmissions and batches them for retry, with alerts when persistent network failures require alternative communication methods.

### Key Operational Steps

**System Startup and Initialization:**
- Device powers on and performs hardware self-diagnostics
- NVS (Non-Volatile Storage) initialized for WiFi credential persistence
- BLE (Bluetooth Low Energy) initialized and begins advertising as "AssetTag-Config" for WiFi credential setup
- Battery monitoring system activates and displays current charge level via GPIO 38 LED
- Visual status indicators initialize (GPIO 39 for device status, GPIO 38 for battery, GPIO 40 for TX pulse)
- Orientation sensor (IMU) initializes and calibrates via I2C (GPIO 8/9)
- GPS receiver initializes and begins satellite acquisition via UART (GPIO 17/18, 38400 baud)
- WiFi connection established using stored network credentials from NVS
- System time synchronized with internet time servers (NTP)
- Status indicator confirms successful sensor initialization (green = ready, red = error)
- System enters autonomous operation mode with BLE continuously advertising

**Continuous Monitoring Cycle (Repeats every 30 seconds):**
- System automatically wakes from low-power idle state
- Orientation and GPS sensors powered on and allowed to stabilize
- WiFi connection re-established for data transmission
- Comprehensive sensor data collection performed:
  - Orientation data: device rotation (quaternion), tilt angles (Euler), acceleration, rotation rate, magnetic field
  - Location data: GPS coordinates (latitude/longitude), altitude, speed, heading, satellite count, fix quality
- Battery level measured and included in data packet
- Current timestamp synchronized with network time
- Unique device identifier included for tracking
- Data packet prepared for transmission

**Data Transmission and Retry Logic:**
- System attempts to transmit data packet to remote server
- If transmission succeeds: cycle completes normally
- If transmission fails: data automatically queued for retry
- When retrying: system combines queued data with new data for batch transmission
- If multiple failures occur: system alerts for alternative communication method (BLE backup)
- Maximum queue capacity prevents data loss during extended outages
- Queue automatically clears when successful transmission occurs

**Power Conservation Phase:**
- Sensors immediately powered down after data collection
- WiFi radio completely disabled to conserve energy
- System enters low-power idle state
- Battery monitoring continues at reduced frequency
- Visual indicators remain active for status feedback
- System remains in idle for approximately 23 seconds until next cycle

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
- BLE continuously advertising for WiFi credential setup (~5mA always on)
- Battery monitoring operates at minimal frequency (every 5 seconds) to reduce overhead
- Visual indicators optimized for power efficiency (battery LED at 78% brightness)
- Average power consumption: 65.45mA (including BLE and battery monitoring LED)
- Estimated runtime: 1.2 days (28.6 hours realistic) per full charge cycle with 2200mAh battery

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
│                    ESP32-S3 Microcontroller                 │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │   IMU         │  │   GPS        │  │  ADC         │     │
│  │  BNO085       │  │  NEO-M9N     │  │  Battery     │     │
│  │  9-axis       │  │  Position    │  │  Monitor     │     │
│  └──────────────┘  └──────────────┘  └──────────────┘     │
│        ↓                ↓                    ↓               │
│  ┌────────────────────────────────────────────────────┐    │
│  │     Sensor Data Collection & JSON Creation        │    │
│  │  (Every 30 seconds, ~6-7 seconds active time)     │    │
│  └────────────────────────────────────────────────────┘    │
│        ↓                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │     Transmission Handler with Retry Logic         │    │
│  │  • Queue management (max 10 failed transmissions) │    │
│  │  • Batch transmission (JSON array)                │    │
│  │  • BLE backup trigger (2+ queued items)           │    │
│  └────────────────────────────────────────────────────┘    │
│        ↓                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │              WiFi Communication                    │    │
│  │  • Read credentials from NVS (persistent storage) │    │
│  │  • HTTP POST with 5-second timeout                │    │
│  │  • Power down after transmission                  │    │
│  └────────────────────────────────────────────────────┘    │
│        ↓                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │              BLE Communication                    │    │
│  │  • Always advertising for WiFi credential setup  │    │
│  │  • GATT characteristics for SSID/Password        │    │
│  │  • Auto-save to NVS when received                │    │
│  └────────────────────────────────────────────────────┘    │
│        ↓                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │          Status & Battery Indication              │    │
│  │  • GPIO 39: Device Status (Green/Red)             │    │
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
- **ESP32-S3 WROOM**
  - Dual-core 240 MHz processor
  - 8MB PSRAM, 8MB Flash
  - Built-in WiFi (802.11 b/g/n)
  - ADC, GPIO, UART, I2C, SPI

### Sensors
- **BNO085 IMU (9-axis)**
  - Interface: I2C (GPIO 8 SDA, GPIO 9 SCL)
  - I2C Speed: 400 kHz
  - Reset Pin: GPIO 13
  - Interrupt Pin: GPIO 12 (optional)
  - Outputs: Quaternion, Euler angles, Acceleration, Gyroscope, Magnetometer
  - Sampling: ~3 seconds per cycle (20 updates × 50ms)
  - Power: Active 3.5mA, Sleep 0.005mA

- **NEO-M9N GPS**
  - Interface: UART Serial2 (GPIO 18 RX, GPIO 17 TX)
  - Reset Pin: GPIO 15
  - Interrupt Pin: GPIO 16
  - Baud Rate: 38400 (8N1)
  - Protocol: UBX only (configured in code)
  - Outputs: Position, Altitude, Speed, Heading, Fix Type, HDOP, Satellites
  - Acquisition: 3-30 seconds per cycle (20 updates × 50ms)
  - Power: Active 30mA, Sleep 0.05mA

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
- **GPIO 39: Device Status LED (NeoPixel)**
  - Green: Both sensors initialized
  - Red: Sensor initialization failed

- **GPIO 38: Battery Level LED (NeoPixel)**
  - Green (75-100%): Excellent
  - Yellow (25-75%): Low
  - Red (0-24%): Critical
  - Brightness: 200/255 (78%)
  - Updates every 5 seconds

- **GPIO 40: WiFi TX Pulse LED**
  - Indicator: Pulses when WiFi transmits
  - Duration: 20ms

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
│  ├─ Read IMU: quaternion, acceleration, etc.
│  └─ Read GPS: position, speed, satellites, etc.
├─
TIME: 4s
├─ Create JSON payload with:
│  ├─ device_id (MAC address)
│  ├─ battery_level (percentage from BatteryMonitor)
│  ├─ timestamp (NTP synced Unix epoch)
│  ├─ imu data (quaternion, euler, accelerometer, gyroscope, magnetometer)
│  └─ gps data (fix, coordinates, altitude, speed, heading, satellites)
├─
TIME: 4.1s
├─ Check Transmission Queue:
│  ├─ If empty: Send single JSON
│  ├─ If 1 item: Create array [prev, current] and send
│  └─ If 2+ items: Alert "NEED BLE", queue current
├─
TIME: 6-7s (WiFi dependent)
├─ Power off IMU
├─ Power off GPS
├─ Disconnect and power off WiFi
├─
TIME: 7s onwards
├─ Idle/Sleep until next 30-second mark
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

**Single Transmission (Queue Empty):**
```json
{
  "device_id": "A1:B2:C3:D4:E5:F6",
  "battery_level": 85,
  "timestamp": 1731657600,
  "imu": {
    "quaternion": {
      "i": -0.1234,
      "j": 0.5678,
      "k": -0.0123,
      "real": 0.8156,
      "accuracy": 0.99
    },
    "euler": {
      "roll": 12.34,
      "pitch": -5.67,
      "yaw": 45.89
    },
    "accelerometer": { "x": 0.123, "y": -0.234, "z": 9.81 },
    "gyroscope": { "x": 0.012, "y": -0.034, "z": 0.001 },
    "magnetometer": { "x": 12.3, "y": 34.5, "z": -45.6 },
    "timestamp": 1731657600000
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
    "hdop": 1.2,
    "timestamp": 1731657600000
  }
}
```

**Batch Transmission (Queue has 2+ items):**
```json
[
  { "device_id": "...", "battery_level": 85, "timestamp": 1731657570, "imu": {...}, "gps": {...} },
  { "device_id": "...", "battery_level": 84, "timestamp": 1731657600, "imu": {...}, "gps": {...} }
]
```

### Data Queue Lifecycle

```
Cycle 1 (WiFi OK):
  Queue: EMPTY → Send JSON1 ✓ → Queue: EMPTY

Cycle 2 (WiFi fails):
  Queue: EMPTY → Send JSON2 ✗ → Queue: [JSON2]

Cycle 3 (WiFi still down):
  Queue: [JSON2] → Create [JSON2, JSON3] → Send ✗ → Queue: [JSON2, JSON3]

Cycle 4 (WiFi still down):
  Queue: [JSON2, JSON3] → Alert "NEED BLE" → Queue: [JSON2, JSON3, JSON4]

Cycle 5+ (Queue full or WiFi down):
  Queue: [JSON2, JSON3, JSON4...] → Waiting for BLE or WiFi recovery
```

---

## Power Management

### Power States

| Component | Active | Sleep | Idle |
|-----------|--------|-------|------|
| ESP32-S3 | 100 mA | N/A | 15 mA |
| IMU | 3.5 mA | 0.005 mA | - |
| GPS | 30 mA | 0.05 mA | - |
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

STEP 1: CHECK QUEUE
├─ Queue Empty?
│  ├─ YES → Go to STEP 2
│  └─ NO → Go to STEP 3
│
STEP 2: SEND SINGLE JSON
├─ Attempt HTTP POST with 5s timeout
├─ Success?
│  ├─ YES → Return (cycle continues)
│  └─ NO → Add to queue, return (will retry next cycle)
│
STEP 3: QUEUE HAS 1 ITEM
├─ Append current data (queue now: 2 items)
├─ Create JSON array [prev, current]
├─ Attempt HTTP POST with 5s timeout
├─ Success?
│  ├─ YES → Clear queue, return
│  └─ NO → Keep both in queue, return
│
STEP 4: QUEUE HAS 2+ ITEMS
├─ Serial print: "!!! NEED BLE HERE !!!"
├─ Attempt to add current data if space
├─ Return (waiting for BLE or WiFi recovery)
│
END
```

### WiFi Credentials Management

**Storage:** NVS (Non-Volatile Storage - persistent across power cycles)

**Setup Methods:**
1. **Via BLE (Recommended):** Connect to "AssetTag-Config" BLE device, write SSID and password to characteristics
2. **Via Code (First Time):** Uncomment NVS setup lines in sketch

```cpp
// Method 1: Via BLE (automatic)
// Connect to "AssetTag-Config" BLE device
// Write SSID to characteristic UUID: 12345678-1234-1234-1234-123456789abd
// Write Password to characteristic UUID: 12345678-1234-1234-1234-123456789abe
// Credentials automatically saved to NVS

// Method 2: Via code (first time only)
NVSConfig::setWiFiSSID("YourSSID");
NVSConfig::setWiFiPassword("YourPassword");

// Automatic retrieval on every boot
String ssid = NVSConfig::getWiFiSSID();
String password = NVSConfig::getWiFiPassword();
```

### Time Synchronization

- **Protocol:** NTP (Network Time Protocol)
- **Servers:** pool.ntp.org, time.nist.gov
- **Timing:** Once at startup, synced via WiFi
- **Timestamp:** Unix epoch (seconds since 1970-01-01)
- **Update Frequency:** Device time drifts, NTP provides accurate sync

---

## LED Indicators

### Device Status LED (GPIO 39)

| State | Color | Meaning | Action |
|-------|-------|---------|--------|
| Green | 🟢 | Both IMU & GPS initialized | Normal operation |
| Red | 🔴 | IMU or GPS failed | Check connections, restart |

### Battery Level LED (GPIO 38)

| Battery % | Voltage | Color | Meaning | Action |
|-----------|---------|-------|---------|--------|
| 75-100% | 3.975-4.2V | 🟢 Green | Excellent | No action needed |
| 50-75% | 3.6-3.975V | 🟡 Yellow | Good | Monitor level |
| 25-50% | 3.225-3.6V | 🟡 Yellow | Low | Plan recharge soon |
| 10-25% | 3.03-3.225V | 🔴 Red | Critical | Charge soon |
| <10% | <3.03V | 🔴 Red (blinking) | Depleted | CHARGE IMMEDIATELY |

### WiFi TX Indicator (GPIO 40)

| State | Behavior | Meaning |
|-------|----------|---------|
| Pulse | 20ms on-pulse | WiFi transmission event detected |
| Off | No activity | No WiFi transmission |

---

## File Structure

### Header Files (Configuration & Libraries)

```
juxta-asset-tag-main/
├── juxta-asset-tag-main.ino  # Main Arduino sketch
├── imu_sensor.h              # BNO085 IMU sensor driver (I2C on GPIO 8/9)
├── gps_sensor.h              # NEO-M9N GPS sensor driver (UART on GPIO 17/18)
├── customwifi.h              # WiFi connection manager with HTTP POST
├── time_sync.h               # NTP time synchronization
├── device_id.h               # MAC address retrieval
├── data_queue.h              # Transmission queue management (max 10 items)
├── transmission_handler.h    # Retry logic & batch transmission
├── nvs_config.h              # NVS storage for WiFi credentials
├── battery_monitor.h         # Battery voltage reading & percentage (GPIO 4 ADC)
├── battery_indicator_led.h   # Battery LED color control (GPIO 38 NeoPixel)
└── ble_config.h              # BLE configuration for WiFi credentials setup
```

### Main Sketch

```
juxta-asset-tag-main.ino
├── setup()                   # Initialization
│  ├─ Serial begin
│  ├─ NVS initialize
│  ├─ BLE initialize (always advertising)
│  ├─ Battery ADC initialize
│  ├─ Battery LED initialize
│  ├─ Status LED initialize
│  ├─ Pulse LED initialize
│  ├─ Sensors initialize (IMU & GPS)
│  ├─ WiFi connect
│  └─ Time sync
│
├── loop()                    # Main 30-second cycle
│  ├─ BLE update (handles connections)
│  ├─ BLE advertising check (every 5s)
│  ├─ LED pulse handler (WiFi TX events)
│  ├─ Battery update (every 5s)
│  ├─ Interval check (every 30s)
│  │  ├─ Power on sensors
│  │  ├─ Connect WiFi
│  │  ├─ Collect data (20 updates)
│  │  ├─ Create JSON
│  │  ├─ Send with retry logic
│  │  ├─ Power down sensors
│  │  └─ Disconnect WiFi
│  └─ Delay 100ms
│
├── createSensorJSON()        # Format sensor data as JSON
├── sendDataWithRetryLogic()  # Use transmission handler
├── updateStatusLED()         # Show initialization status
└── wifiEventHandler()        # WiFi TX event callback
```

### Documentation Files

```
├── BATTERY_ANALYSIS.md       # Battery calculations & specs
├── BATTERY_MONITORING.md     # Battery monitoring system details
├── TRANSMISSION_SYSTEM_DOCS.h # Retry logic documentation
└── SYSTEM_OVERVIEW.md        # This file
```

### BLE Configuration

The system includes BLE (Bluetooth Low Energy) functionality for WiFi credential configuration:

- **Device Name:** "AssetTag-Config"
- **Service UUID:** 12345678-1234-1234-1234-123456789abc
- **Always Advertising:** BLE continuously advertises for WiFi credential setup
- **Characteristics:**
  - SSID Characteristic (UUID: 12345678-1234-1234-1234-123456789abd)
  - Password Characteristic (UUID: 12345678-1234-1234-1234-123456789abe)
  - Status Characteristic (UUID: 12345678-1234-1234-1234-123456789abf)
- **Storage:** Credentials automatically saved to NVS when received via BLE
- **Usage:** Connect with BLE client app, write SSID and password, credentials saved automatically

---

## Initialization Sequence

### At Power-On

```
1. Serial begin (115200 baud)
   ↓
2. NVS initialize (read WiFi credentials)
   ↓
3. BLE initialize (start advertising "AssetTag-Config")
   ↓
4. Battery ADC initialize (GPIO 4)
   ↓
5. Battery Indicator LED initialize (GPIO 38, brightness 200/255)
   ↓
6. Device Status LED initialize (GPIO 39, brightness 255/255, red initially)
   ↓
7. Pulse LED initialize (GPIO 40)
   ↓
8. WiFi TX event handler register
   ↓
9. IMU sensor begin() → sets imuInitialized flag
   ↓
10. GPS sensor begin() → sets gpsInitialized flag
    ↓
11. Update Status LED → Green if both initialized, else Red
    ↓
12. WiFi connect (read credentials from NVS)
    ↓
13. NTP time sync (sets system clock)
    ↓
14. Enter main loop (30-second cycles begin, BLE always active)
```

---

## Real-World Deployment

### Prerequisites

1. **WiFi Network**
   - SSID and password (stored in NVS on first boot)
   - 2.4GHz frequency (ESP32-S3 only supports 2.4GHz)

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
2. Upload sketch to ESP32-S3
3. Uncomment NVS credential setup lines
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
1. Check I2C (GPIO 21/22) connections
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
- NVS credentials not set
- Wrong SSID/password
- WiFi network unreachable

**Solution:**
1. Uncomment NVS setup lines in sketch
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
| IMU data | ~100 bytes | Single reading structure |
| GPS data | ~100 bytes | Single reading structure |
| JSON string | 400-600 bytes | Formatted payload |
| Queue (max 10 items) | ~6KB | JSON strings |
| **Total** | **~8KB active** | Out of 320KB ESP32-S3 RAM |

---

## Network Communication

### HTTP POST Format

```
POST /api/sensor-data HTTP/1.1
Host: your-server.com
Content-Type: application/json
Content-Length: 584

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

- **Storage:** NVS (secure, persistent, encrypted by default)
- **Transmission:** HTTPS recommended for SERVER_URL
- **Reset:** Can be cleared via `NVSConfig::clearWiFiCredentials()`

### Data Privacy

- **MAC Address:** Included in payload (can identify device)
- **Location:** GPS data exposed in JSON
- **Timestamps:** UTC epoch visible to anyone with server access

**Recommendations:**
- Use HTTPS for server connection
- Implement authentication token in header
- Consider data encryption at application layer

---

