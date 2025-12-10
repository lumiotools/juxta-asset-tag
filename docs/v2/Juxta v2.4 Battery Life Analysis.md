# Battery Life & Power Consumption Analysis

## 15-Minute Duty Cycle Configuration

**Project:** ESP32-S3 Asset Tracking Device with IMU & GPS  
**Device Version:** v2.4  
**Date:** 12/02/2025  
**Battery Capacity:** 3,000 mAh Li-Po (3.7V)

---

This document provides a battery life analysis for the ESP32-S3-based asset tracking system (v2.4) operating on a 15-minute duty cycle. The device integrates BMI323 IMU, AT6558 GPS, and W25Q128 external flash for persistent data storage. The analysis is based on the actual codebase implementation.

---

### Battery Life by Scenario

Scenario Descriptions:

1) **BLE Connected:** The tag connects via Bluetooth Low Energy for each transmission cycle. The device enters light sleep between cycles (15 minutes), waking every 10ms for IMU sampling. Data is sent via BLE during the active phase. No local storage is performed in this scenario.

2) **WiFi Connected:** The tag connects to a Wi‑Fi network for every transmission but does not rely on BLE. It enters light sleep between cycles, waking every 10ms for IMU sampling. No local storage is performed in this scenario.

3) **Storage Only (No WiFi):** The tag attempts to establish a WiFi connection in each cycle but is unable to find a connection (perhaps because it's not in WiFi range). So, the device skips transmission and stores the data locally in flash memory. After writing, it goes into light sleep, waking every 10ms for IMU sampling.

Our analysis considers each of the three cases individually (it assumes that the same case applies for the duration of a single-charge battery life). Calculations can be found at the end of this document.

| Scenario | Average Current | Ideal Runtime | Realistic Runtime (85%) | Conservative Runtime (70%) | Data Transmission |
| :---- | :---- | :---- | :---- | :---- | :---- |
| A) BLE Connected | 19.95 mA | 6.3 days | **5.4 days** | 4.4 days | Every cycle (BLE) |
| B) WiFi Connected | 21.21 mA | 5.9 days | **5.0 days** | 4.1 days | Every cycle |
| C) Storage Only (No WiFi) | 20.65 mA | 6.1 days | **5.2 days** | 4.3 days | Queued only |

---

### Computing Battery Life in Mixed Use Scenario:

Since actual usage will likely consist of a mix of these three cases, and you can compute the expected ideal battery life by making assumptions on what % of the life of the tag will be spent in each of the three states using the below formula:

**Ideal Battery Life (in hours) = 3,000mAh / [(A% × X.XXmA) + (B% × X.XXmA) + (C% × X.XXmA)]**  
*Where A% is the % time in scenario A, B% is the % time in B and C% is the % in C*

Example: Assuming 5% of usage in scenario A (Setup), 85% in B (Normal), and 10% in C (Out of range)  
Average Current = (0.05 × 19.95) + (0.85 × 21.21) + (0.10 × 20.65) = 21.05 mA

Battery Life = 3000 / 21.05 = 142.5 Hours (~5.9 Days)

---

### Battery Optimization Strategies (Implemented):

| Feature | Description | Power Savings |
| :---- | :---- | :---- |
| **Light Sleep** | ESP32 enters light sleep between cycles, waking every 10ms for IMU sampling | **Active:** Reduces MCU current during sleep periods |
| **Sensor Power** | Sensors remain powered during sleep for continuous IMU sampling | **Active:** IMU: ~0.79 mA continuous, GPS: ~0.05 mA standby, Flash: ~0.05 mA standby |
| **LED Management** | LEDs controlled via NeoPixel brightness (70/255 = ~27%) | **Active:** Reduced LED current consumption |
| **WiFi Power Mgmt** | WiFi disabled during light sleep | **Active:** Saves ~200mA+ during sleep |

---

### Advanced Battery Optimization Strategies (To Do): 

1) **Reducing GPS Time-to-Fix:** The AT6558 GPS takes 30-35s of active time to establish a connection with its GPS satellites. In scenario A & B we are trying to use the BLE / WiFi connection to download current satellite fixing data from the internet so establishing a connection only takes 1-3s of GPS active time therefore saving considerable battery. This is a simple implementation for a premium GPS chip (like the one from u-blox that we are using in our v1 design) but it's far trickier for the AT6558 GPS since they have their own proprietary satellite data format which we are trying to reverse engineer. We have assumed the reduced active time figures (1 second) for Scenario A & B calculations in this document.

2) **Motion Triggered Cycles:** Instead of sending data on a fixed cycle (such as every 15 minutes) we can leverage the IMU sensor to detect motion (therefore a change of location) and only then take a GPS reading and perform a data transmission. During periods of no motion being detected it can be assumed that the device is at the same location as the most recent transmission. This could extend the battery life substantially during periods when the device is stationary (such as at nighttime or during storage of the asset). This still requires periodic IMU processing and the battery consumption impact of this needs to be evaluated.

---

### Factors that Impact Battery Efficiency 

| Factor | Impact | Notes |
| :---- | :---- | :---- |
| **Voltage Regulator Losses** | ~10-15% | Linear regulator efficiency |
| **Battery Self-Discharge** | ~2-5% per month | Li-Po chemistry dependent |
| **Battery Aging** | Variable | Capacity decreases over time |
| **Temperature Effects** | Variable | Cold reduces capacity, heat reduces lifespan |
| **Realistic Efficiency** | **85%** | Recommended for estimation |

---

### Notes on Data Storage 

| Parameter | Value |
| :---- | :---- |
| **Flash Capacity** | W25Q128 (16 MB = 16,777,216 bytes) |
| **Estimated Data per Reading** | ~150-200 bytes (CSV format) |
| **Maximum Storage Capacity** | ~80,000 - 100,000 readings |
| **Storage Duration** | ~2.5 - 3.1 years at 4 readings/hour |
| **Data Queue** | Persistent storage in flash, survives power cycles |
| **Retry Logic** | Automatic retry on next cycle if transmission fails |

## ---

## System Overview

### Hardware Components

| Component | Model | Active Current | Sleep Current | Notes |
| :---- | :---- | :---- | :---- | :---- |
| **Microcontroller** | ESP32-S3 | 40-250 mA | ~5-10 mA | Light sleep (wakes every 10ms), WiFi active during TX |
| **IMU Sensor** | BMI323 | 0.79 mA | **0.79 mA** | **Always On (Continuous 100Hz Sampling)** |
| **GPS Sensor** | AT6558 | 25 mA | **0.05 mA** | **Standby (Active 1s per cycle when enabled)** |
| **Flash Storage** | W25Q128 | 20 mA | **0.05 mA** | **Standby (Active during write)** |
| **Status LED** | NeoPixel (2 pixels) | ~15-20 mA | **~4-5 mA** | **Brightness 70/255 (~27%), Always On** |
| **Battery LED** | NeoPixel (shared) | Included above | Included above | **Brightness 70/255 (~27%), Always On** |

### Operational Cycle

**Cycle Duration:** 15 minutes (900 seconds) - Configurable via BLE/NVS  
**Light Sleep Phase:** ~890 seconds (ESP32 in light sleep, wakes every 10ms for IMU ticker; Sensors powered; LEDs On at 27% brightness)  
**Active Phase:** ~10 seconds (Sensor reading + TX + LED On)

---

## Battery Life Calculations

### Scenario 1: BLE Connected

**Cycle Energy Consumption:**

**Active Phase (10 seconds):** Includes Wake, GPS Read, BLE Connection/Transmission, and Processing.

| Component | Current | Duration | Energy (mAs) |
| :---- | :---- | :---- | :---- |
| ESP32-S3 (Wake/Processing) | 40 mA | 2.0 sec | 80.0 |
| ESP32-S3 (BLE Connect/TX) | 150 mA | 5.5 sec | 825.0 |
| ESP32-S3 (Processing/CSV) | 40 mA | 2.5 sec | 100.0 |
| IMU (Always On) | 0.79 mA | 10.0 sec | 7.9 |
| GPS (Active Read) | 25 mA | 1.0 sec | 25.0 |
| GPS (Standby) | 0.05 mA | 9.0 sec | 0.5 |
| Flash (Standby) | 0.05 mA | 10.0 sec | 0.5 |
| Status LED (On, 27% brightness) | 5.0 mA | 10.0 sec | 50.0 |
| Battery LED (On, 27% brightness) | 5.0 mA | 10.0 sec | 50.0 |
| **ACTIVE PHASE TOTAL:** |  | **10.0 sec** | **1,138.9 mAs** |

**Light Sleep Phase (890 seconds):**

**ESP32 in light sleep (wakes every 10ms for IMU ticker), sensors powered, LEDs On at 27% brightness.**

| Component | Current | Duration | Energy (mAs) |
| :---- | :---- | :---- | :---- |
| ESP32-S3 (Light Sleep) | 8.0 mA | 890 sec | 7,120.0 |
| IMU Sensor (Always On) | 0.79 mA | 890 sec | 703.1 |
| GPS Sensor (Standby) | 0.05 mA | 890 sec | 44.5 |
| W25Q128 Flash (Standby) | 0.05 mA | 890 sec | 44.5 |
| Status LED (On, 27% brightness) | 5.0 mA | 890 sec | 4,450.0 |
| Battery LED (On, 27% brightness) | 5.0 mA | 890 sec | 4,450.0 |
| **LIGHT SLEEP TOTAL:** |  | **890 sec** | **16,812.1 mAs** |

**Total Per Cycle:**

* **Total Energy:** 1,138.9 + 16,812.1 = **17,951.0 mAs**  
* **Cycle Duration:** 900 seconds (15 minutes)

**Battery Life Calculation (3000 mAh):**

**Simplified Calculation Method:**

1. **Energy per hour:** 17,951.0 mAs × 4 cycles/hour = 71,804.0 mAs/hour
2. **Convert to mAh:** 71,804.0 mAs ÷ 3,600 = **19.95 mAh/hour** (Average current: **19.95 mA**)
3. **Battery life:** 3,000 mAh ÷ 19.95 mAh/hour = **150.4 hours** = **6.3 days**
4. **Total cycles:** 150.4 hours × 4 cycles/hour = **601 cycles**

| Parameter | Value |
| :---- | :---- |
| Energy per Cycle | 17,951.0 mAs |
| Energy per Hour | 19.95 mAh (4 cycles/hour) |
| Average Current | 19.95 mA |
| Battery Life | **150.4 hours (6.3 days)** |
| Total Cycles | 601 cycles |

**Note:** The calculated values shown above represent ideal operating conditions. The summary table shows these ideal values along with realistic estimates that account for 85% efficiency factor (voltage regulator losses, temperature effects, battery aging, etc.) and real-world operating conditions. The realistic estimate of **5.4 days** (85% of ideal) should be used for practical battery life planning.

---

### Scenario 2: WiFi Connected

**Cycle Energy Consumption:**

**Active Phase (12 seconds):** Includes Wake, GPS Read, WiFi Connection/Transmission, and Processing.

| Component | Current | Duration | Energy (mAs) |
| :---- | :---- | :---- | :---- |
| ESP32-S3 (Wake/Processing) | 40 mA | 2.0 sec | 80.0 |
| ESP32-S3 (WiFi Connect) | 250 mA | 5.0 sec | 1,250.0 |
| ESP32-S3 (WiFi TX) | 250 mA | 3.0 sec | 750.0 |
| ESP32-S3 (Processing/CSV) | 40 mA | 2.0 sec | 80.0 |
| IMU (Always On) | 0.79 mA | 12.0 sec | 9.5 |
| GPS (Active Read) | 25 mA | 1.0 sec | 25.0 |
| GPS (Standby) | 0.05 mA | 11.0 sec | 0.6 |
| Flash (Standby) | 0.05 mA | 12.0 sec | 0.6 |
| Status LED (On, 27% brightness) | 5.0 mA | 12.0 sec | 60.0 |
| Battery LED (On, 27% brightness) | 5.0 mA | 12.0 sec | 60.0 |
| **ACTIVE PHASE TOTAL:** |  | **12.0 sec** | **2,315.7 mAs** |

**Light Sleep Phase (888 seconds):**

**ESP32 in light sleep (wakes every 10ms for IMU ticker), sensors powered, LEDs On at 27% brightness.**

| Component | Current | Duration | Energy (mAs) |
| :---- | :---- | :---- | :---- |
| ESP32-S3 (Light Sleep) | 8.0 mA | 888 sec | 7,104.0 |
| IMU Sensor (Always On) | 0.79 mA | 888 sec | 701.5 |
| GPS Sensor (Standby) | 0.05 mA | 888 sec | 44.4 |
| W25Q128 Flash (Standby) | 0.05 mA | 888 sec | 44.4 |
| Status LED (On, 27% brightness) | 5.0 mA | 888 sec | 4,440.0 |
| Battery LED (On, 27% brightness) | 5.0 mA | 888 sec | 4,440.0 |
| **LIGHT SLEEP TOTAL:** |  | **888 sec** | **16,774.3 mAs** |

**Total Per Cycle:**

* **Total Energy:** 2,315.7 + 16,774.3 = **19,090.0 mAs**  
* **Cycle Duration:** 900 seconds (15 minutes)

**Battery Life Calculation (3000 mAh):**

**Simplified Calculation Method:**

1. **Energy per hour:** 19,090.0 mAs × 4 cycles/hour = 76,360.0 mAs/hour
2. **Convert to mAh:** 76,360.0 mAs ÷ 3,600 = **21.21 mAh/hour** (Average current: **21.21 mA**)
3. **Battery life:** 3,000 mAh ÷ 21.21 mAh/hour = **141.4 hours** = **5.9 days**
4. **Total cycles:** 141.4 hours × 4 cycles/hour = **566 cycles**

| Parameter | Value |
| :---- | :---- |
| Energy per Cycle | 19,090.0 mAs |
| Energy per Hour | 21.21 mAh (4 cycles/hour) |
| Average Current | 21.21 mA |
| Battery Life | **141.4 hours (5.9 days)** |
| Total Cycles | 566 cycles |

**Note:** The calculated values shown above represent ideal operating conditions. The summary table shows these ideal values along with realistic estimates that account for 85% efficiency factor (voltage regulator losses, temperature effects, battery aging, etc.) and real-world operating conditions. The realistic estimate of **5.0 days** (85% of ideal) should be used for practical battery life planning.

---

### Scenario 3: Storage Only (No WiFi or BLE Connection)

**Cycle Energy Consumption:**

**Note:** On every cycle, the device attempts to establish a WiFi connection. If no WiFi network is available or connection fails, the device stores the data locally in flash memory and enters light sleep. This WiFi connection attempt occurs on each wake cycle, which is why WiFi power consumption (250 mA during connection attempt) is included in this scenario. The WiFi attempt duration varies: if no credentials are configured, it fails immediately (~0.2 seconds); if credentials exist but no network is available, it attempts for up to 10 seconds (20 attempts × 500ms) plus a 1-second stabilization delay, for a maximum of ~11 seconds. The calculation uses 5.0 seconds as a reasonable average, but actual consumption may be higher if credentials exist but no network is in range.

**Active Phase (13 seconds):** Wake, Read, WiFi Attempt (Fail), Flash Write, Sleep.

| Component | Current | Duration | Energy (mAs) |
| :---- | :---- | :---- | :---- |
| ESP32-S3 (Wake/Read) | 40 mA | 2.0 sec | 80.0 |
| ESP32-S3 (WiFi Attempt) | 250 mA | 5.0 sec | 1,250.0 |
| ESP32-S3 (Flash Write) | 40 mA | 2.0 sec | 80.0 |
| ESP32-S3 (Processing/CSV) | 40 mA | 4.0 sec | 160.0 |
| Flash Memory (Write) | 20 mA | 2.0 sec | 40.0 |
| IMU (Always On) | 0.79 mA | 13.0 sec | 10.3 |
| GPS (Active Read) | 25 mA | 1.0 sec | 25.0 |
| GPS (Standby) | 0.05 mA | 12.0 sec | 0.6 |
| Status LED (On, 27% brightness) | 5.0 mA | 13.0 sec | 65.0 |
| Battery LED (On, 27% brightness) | 5.0 mA | 13.0 sec | 65.0 |
| **ACTIVE PHASE TOTAL:** |  | **13.0 sec** | **1,775.9 mAs** |

**Light Sleep Phase (887 seconds):**

**ESP32 in light sleep (wakes every 10ms for IMU ticker), sensors powered, LEDs On at 27% brightness.**

| Component | Current | Duration | Energy (mAs) |
| :---- | :---- | :---- | :---- |
| ESP32-S3 (Light Sleep) | 8.0 mA | 887 sec | 7,096.0 |
| IMU Sensor (Always On) | 0.79 mA | 887 sec | 700.7 |
| GPS Sensor (Standby) | 0.05 mA | 887 sec | 44.4 |
| W25Q128 Flash (Standby) | 0.05 mA | 887 sec | 44.4 |
| Status LED (On, 27% brightness) | 5.0 mA | 887 sec | 4,435.0 |
| Battery LED (On, 27% brightness) | 5.0 mA | 887 sec | 4,435.0 |
| **LIGHT SLEEP TOTAL:** |  | **887 sec** | **16,811.5 mAs** |

**Total Per Cycle:**

* **Total Energy:** 1,775.9 + 16,811.5 = **18,587.4 mAs**  
* **Cycle Duration:** 900 seconds (15 minutes)

**Battery Life Calculation (3000 mAh):**

**Simplified Calculation Method:**

1. **Energy per hour:** 18,587.4 mAs × 4 cycles/hour = 74,349.6 mAs/hour
2. **Convert to mAh:** 74,349.6 mAs ÷ 3,600 = **20.65 mAh/hour** (Average current: **20.65 mA**)
3. **Battery life:** 3,000 mAh ÷ 20.65 mAh/hour = **145.3 hours** = **6.1 days**
4. **Total cycles:** 145.3 hours × 4 cycles/hour = **581 cycles**

| Parameter | Value |
| :---- | :---- |
| Energy per Cycle | 18,587.4 mAs |
| Energy per Hour | 20.65 mAh (4 cycles/hour) |
| Average Current | 20.65 mA |
| Battery Life | **145.3 hours (6.1 days)** |
| Total Cycles | 581 cycles |

**Note:** The calculated values shown above represent ideal operating conditions. The summary table shows these ideal values along with realistic estimates that account for 85% efficiency factor (voltage regulator losses, temperature effects, battery aging, etc.) and real-world operating conditions. The realistic estimate of **5.2 days** (85% of ideal) should be used for practical battery life planning.

