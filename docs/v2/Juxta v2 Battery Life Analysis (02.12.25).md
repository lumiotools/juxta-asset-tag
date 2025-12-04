# Battery Life & Power Consumption Analysis

## 15-Minute Duty Cycle Configuration

**Project:** ESP32C3 Asset Tracking Device with IMU & GPS  
**Device Version:** v2.0  
**Date: 12/02/2025**  
**Battery Capacity:** 3,000 mAh Li-Po (3.7V)

---

This document provides a battery life analysis for the ESP32C3-based asset tracking system operating on a 15-minute duty cycle with a focus on the Test Deployment. The device integrates BMI323 IMU, AT6558 GPS, and W25Q128 external flash for persistent data storage. 

---

### Battery Life by Scenario

Scenario Descriptions:

1) **BLE Connected:** The tag connects via Bluetooth Low Energy for each transmission cycle. The device enters deep sleep between cycles (15 minutes), similar to the WiFi scenario. Data is sent via BLE during the active phase. No local storage is performed in this scenario. 
2) **WiFi Connected:** The tag connects to a Wi‑Fi network for every transmission but does not rely on BLE. It can enter deep sleep between cycles. No local storage is performed in this scenario.

3) **Storage Only (No WiFi):** The tag attempts to establish a WiFi connection in each cycle but is unable to find a connection (perhaps because it’s not in WiFi range). So, the device skips transmission and stores the data locally in flash memory. After writing, it goes into deep sleep. 

Our analysis considers each of the three cases individually (it assumes that the same case applies for the duration of a single-charge battery life). Calculations can be found at the end of this document.

| Scenario | Average Current | Ideal Runtime | Realistic Runtime (85%) | Conservative Runtime (70%) | Data Transmission |
| :---- | :---- | :---- | :---- | :---- | :---- |
| A) BLE Connected | 1.24 mA | 100.7 days | **85.6 days** | 70.5 days | Every cycle (BLE) |
| B) WiFi Connected | 1.63 mA | 76.7 days | **65.2 days** | 53.7 days | Every cycle |
| C) Storage Only (No WiFi) | 1.80 mA | 69.5 days | **59.1 days** | 48.7 days | Queued only |

---

### Computing Battery Life in Mixed Use Scenario:

Since actual usage will likely consist of a mix of these three cases, and you can compute the expected ideal battery life by making assumptions on what % of the life of the tag will be spent in each of the three states using the below formula:

**Ideal Battery Life (in hours) \= 3,000mAh / \[(A% x 1.24mA) \+ (B% x 1.63mA) \+ (C% x 1.80mA)\]**  
*Where A% is the % time in scenario A, B% is the % time in B and C% is the % in C*

Example: Assuming 5% of usage in scenario A (Setup), 85% in B (Normal), and 10% in C (Out of range) Average Current \= (0.05 \* 1.24) \+ (0.85 \* 1.63) \+ (0.10 \* 1.80) \= 1.63 mA

Battery Life \= 3000 / 1.63 \= 1,843 Hours (\~76.8 Days)

---

### Battery Optimization Strategies (Implemented):

| Feature | Description | Power Savings |
| :---- | :---- | :---- |
| **Deep Sleep** | ESP32 enters sleep between cycles | **Active:** Reduces MCU current to 0.02 mA |
| **Sensor Power** | Sensors remain in standby during sleep | **Active:** Sensors draw \~0.11 mA continuously (IMU: 0.01 mA, GPS: 0.05 mA, Flash: 0.05 mA) |
| **LED Management** | LED Off during sleep | **Active:** Reduces sleep current contribution to 0 mA |
| **WiFi Power Mgmt** | WiFi disabled during deep sleep | **Active:** Saves \~200mA+ during sleep |

---

### Advanced Battery Optimization Strategies (To Do): 

1) **Reducing GPS Time-to-Fix:** The AT6558 GPS takes 30-35s of active time to establish a connection with its GPS satellites. In scenario A & B we are trying to use the BLE / WiFi connection to download current satellite fixing data from the internet so establishing a connection only takes 1-3s of GPS active time therefore saving considerable battery. This is a simple implementation for a premium GPS chip (like the one from u-blox that we are using in our v1 design) but it's far trickier for the AT6558 GPS since they have their own proprietary satellite data format which we are trying to reverse engineer. We have assumed the reduced active time figures (2 seconds) for Scenario A & B calculations in this document.

2) **Motion Triggered Cycles:** Instead of sending data on a fixed cycle (such as every 15 minutes) we can leverage the IMU sensor to detect motion (therefore a change of location) and only then take a GPS reading and perform a data transmission. During periods of no motion being detected it can be assumed that the device is at the same location as the most recent transmission. This could extend the battery life substantially during periods when the device is stationary (such as at nighttime or during storage of the asset). This still requires periodic IMU processing and the battery consumption impact of this needs to be evaluated. 

---

### Factors that Impact Battery Efficiency 

| Factor | Impact | Notes |
| :---- | :---- | :---- |
| **Voltage Regulator Losses** | \~10-15% | Linear regulator efficiency |
| **Battery Self-Discharge** | \~2-5% per month | Li-Po chemistry dependent |
| **Battery Aging** | Variable | Capacity decreases over time |
| **Temperature Effects** | Variable | Cold reduces capacity, heat reduces lifespan |
| **Realistic Efficiency** | **85%** | Recommended for estimation |

---

### Notes on Data Storage 

| Parameter | Value |
| :---- | :---- |
| **Flash Capacity** | W25Q128 (16 MB \= 16,777,216 bytes) |
| **Estimated Data per Reading** | \~150-200 bytes (CSV format) |
| **Maximum Storage Capacity** | \~80,000 \- 100,000 readings |
| **Storage Duration** | \~2.5 \- 3.1 years at 4 readings/hour |
| **Data Queue** | Persistent storage in flash, survives power cycles |
| **Retry Logic** | Automatic retry on next cycle if transmission fails |

## ---

## System Overview

### Hardware Components

| Component | Model | Active Current | Sleep Current | Notes |
| :---- | :---- | :---- | :---- | :---- |
| **Microcontroller** | ESP32C3 | 40-250 mA | 0.02 mA | WiFi active during TX |
| **IMU Sensor** | BMI323 | 0.79 mA | **0.01 mA** | **Always On (Standby)** |
| **GPS Sensor** | AT6558 | 25 mA | **0.05 mA** | **Always On (Standby)** |
| **Flash Storage** | W25Q128 | 20 mA | **0.05 mA** | **Always On (Standby)** |
| **Status LED** | Single Color | 20 mA | **0 mA** | **Off during Deep Sleep** |
| **Battery LED** | Single Color | 20 mA | **0 mA** | **Off during Deep Sleep** |

### 

### Operational Cycle

**Cycle Duration:** 15 minutes (900 seconds)   
**Deep Sleep Phase:** \~890 seconds (ESP32 sleeps; Sensors Idle; LED Off)  
**Active Phase:** \~10 seconds (Sensor reading \+ TX \+ LED On)

---

## Battery Life Calculations

### Scenario 1: BLE Connected

**Cycle Energy Consumption:**

**Active Phase (7 seconds):** Includes Wake, Stabilization, Read, and BLE Connection/Transmission.

| Component | Current | Duration | Energy (mAs) |
| :---- | :---- | :---- | :---- |
| ESP32 (Wake/Stab/Read) | 40 mA | 3.5 sec | 140.0 |
| ESP32 (BLE Connect/TX) | 150 mA | 3.5 sec | 525.0 |
| IMU (Always On) | 0.79 mA | 7.0 sec | 5.5 |
| GPS (Active Read) | 25 mA | 2.0 sec | 50.0 |
| GPS (Standby) | 0.05 mA | 5.0 sec | 0.2 |
| Status LED (On) | 20 mA | 7.0 sec | 140.0 |
| Battery LED (On) | 20 mA | 7.0 sec | 140.0 |
| ACTIVE PHASE TOTAL: |  | 7.0 sec | 1,000.7 mAs |

**Deep Sleep Phase (893 seconds):**

**ESP32 sleeps, sensors powered, LED Off.**

| Component | Current | Duration | Energy (mAs) |
| :---- | :---- | :---- | :---- |
| ESP32C3 (Deep Sleep) | 0.02 mA | 893 sec | 17.9 |
| IMU Sensor (Always On) | 0.01 mA | 893 sec | 8.93 |
| GPS Sensor (Standby) | 0.05 mA | 893 sec | 44.6 |
| W25Q128 Flash (Standby) | 0.05 mA | 893 sec | 44.6 |
| Status LED (Off) | 0 mA | 893 sec | 0.0 |
| Battery LED (Off) | 0 mA | 893 sec | 0.0 |
| DEEP SLEEP TOTAL: |  | 893 sec | 116 mAs |

**Total Per Cycle:**

* **Total Energy:** 1,000.7 \+ 116 \= **1,116.7 mAs**  
* **Cycle Duration:** 900 seconds (15 minutes)

**Battery Life Calculation (3000 mAh):**

**Simplified Calculation Method:**

1. **Energy per hour:** 1,116.7 mAs × 4 cycles/hour \= 4,466.8 mAs/hour
2. **Convert to mAh:** 4,466.8 mAs ÷ 3,600 \= **1.24 mAh/hour** (Average current: **1.24 mA**)
3. **Battery life:** 3,000 mAh ÷ 1.24 mAh/hour \= **2,418 hours** \= **100.7 days**
4. **Total cycles:** 2,418 hours × 4 cycles/hour \= **9,671 cycles**

| Parameter | Value |
| :---- | :---- |
| Energy per Cycle | 1,116.7 mAs |
| Energy per Hour | 1.24 mAh (4 cycles/hour) |
| Average Current | 1.24 mA |
| Battery Life | **2,418 hours (100.7 days)** |
| Total Cycles | 9,671 cycles |

**Note:** The calculated values shown above represent ideal operating conditions. The summary table shows these ideal values along with realistic estimates that account for 85% efficiency factor (voltage regulator losses, temperature effects, battery aging, etc.) and real-world operating conditions. The realistic estimate of **85.6 days** (85% of ideal) should be used for practical battery life planning.

---

### Scenario 2: WiFi Connected

**Cycle Energy Consumption:**

**Active Phase (7 seconds):** Includes Wake, Stabilization, Read, and WiFi Transmission.

| Component | Current | Duration | Energy (mAs) |
| :---- | :---- | :---- | :---- |
| ESP32 (Wake/Stab/Read) | 40 mA | 3.5 sec | 140.0 |
| ESP32 (WiFi TX) | 250 mA | 3.5 sec | 875.0 |
| IMU (Always On) | 0.79 mA | 7.0 sec | 5.5 |
| GPS (Active Read) | 25 mA | 2.0 sec | 50.0 |
| GPS (Standby) | 0.05 mA | 5.0 sec | 0.2 |
| Status LED (On) | 20 mA | 7.0 sec | 140.0 |
| Battery LED (On) | 20 mA | 7.0 sec | 140.0 |
| ACTIVE PHASE TOTAL: |  | 7.0 sec | 1,350.7 mAs |

**Deep Sleep Phase (893 seconds):**

**ESP32 sleeps, sensors powered, LED Off.**

| Component | Current | Duration | Energy (mAs) |
| :---- | :---- | :---- | :---- |
| ESP32C3 (Deep Sleep) | 0.02 mA | 893 sec | 17.9 |
| IMU Sensor (Always On) | 0.01 mA | 893 sec | 8.93 |
| GPS Sensor (Standby) | 0.05 mA | 893 sec | 44.6 |
| W25Q128 Flash (Standby) | 0.05 mA | 893 sec | 44.6 |
| Status LED (Off) | 0 mA | 893 sec | 0.0 |
| Battery LED (Off) | 0 mA | 893 sec | 0.0 |
| DEEP SLEEP TOTAL: |  | 893 sec | 116 mAs |

**Total Per Cycle:**

* **Total Energy:** 1,350.7 \+ 116 \= **1,466.7 mAs**  
* **Cycle Duration:** 900 seconds (15 minutes)

**Battery Life Calculation (3000 mAh):**

**Simplified Calculation Method:**

1. **Energy per hour:** 1,466.7 mAs × 4 cycles/hour \= 5,866.8 mAs/hour
2. **Convert to mAh:** 5,866.8 mAs ÷ 3,600 \= **1.63 mAh/hour** (Average current: **1.63 mA**)
3. **Battery life:** 3,000 mAh ÷ 1.63 mAh/hour \= **1,841 hours** \= **76.7 days**
4. **Total cycles:** 1,841 hours × 4 cycles/hour \= **7,363 cycles**

| Parameter | Value |
| :---- | :---- |
| Energy per Cycle | 1,466.7 mAs |
| Energy per Hour | 1.63 mAh (4 cycles/hour) |
| Average Current | 1.63 mA |
| Battery Life | **1,841 hours (76.7 days)** |
| Total Cycles | 7,363 cycles |

**Note:** The calculated values shown above represent ideal operating conditions. The summary table shows these ideal values along with realistic estimates that account for 85% efficiency factor (voltage regulator losses, temperature effects, battery aging, etc.) and real-world operating conditions. The realistic estimate of **65.2 days** (85% of ideal) should be used for practical battery life planning.

---

### Scenario 3: Storage Only (No WiFi or BLE Connection)

**Cycle Energy Consumption:**

**Note:** On every cycle, the device attempts to establish a WiFi connection. If no WiFi network is available or connection fails, the device stores the data locally in flash memory and enters deep sleep. This WiFi connection attempt occurs on each wake cycle, which is why WiFi power consumption (250 mA during connection attempt) is included in this scenario. The WiFi attempt duration varies: if no credentials are configured, it fails immediately (~0.2 seconds); if credentials exist but no network is available, it attempts for up to 10 seconds (20 attempts × 500ms) plus a 1-second stabilization delay, for a maximum of ~11 seconds. The calculation uses 3.5 seconds as a reasonable average, but actual consumption may be higher if credentials exist but no network is in range.

**Active Phase (8.5 seconds):** Wake, Read, WiFi Attempt (Fail), Flash Write, Sleep.

| Component | Current | Duration | Energy (mAs) |
| :---- | :---- | :---- | :---- |
| ESP32 (Wake/Read) | 40 mA | 3.5 sec | 140.0 |
| ESP32 (WiFi Attempt) | 250 mA | 3.5 sec | 875.0 |
| ESP32 (Flash Write) | 40 mA | 1.5 sec | 60.0 |
| Flash Memory (Write) | 20 mA | 1.5 sec | 30.0 |
| Sensors (IMU/GPS) | 0.79 mA | 8.5 sec | 6.7 |
| GPS Active Adder | 25 mA | 2.0 sec | 50.0 |
| Status LED (On) | 20 mA | 8.5 sec | 170.0 |
| Battery LED (On) | 20 mA | 8.5 sec | 170.0 |
| **ACTIVE PHASE TOTAL:** |  | **8.5 sec** | **1,501.7 mAs** |

**Deep Sleep Phase (891.5 seconds):**

**ESP32 sleeps, sensors powered, LED Off.**

| Component | Current | Duration | Energy (mAs) |
| :---- | :---- | :---- | :---- |
| ESP32C3 (Deep Sleep) | 0.02 mA | 891.5 sec | 17.8 |
| IMU Sensor (Always On) | 0.01 mA | 891.5 sec | 8.9 |
| GPS Sensor (Standby) | 0.05 mA | 891.5 sec | 44.6 |
| W25Q128 Flash (Standby) | 0.05 mA | 891.5 sec | 44.6 |
| Status LED (Off) | 0 mA | 891.5 sec | 0.0 |
| Battery LED (Off) | 0 mA | 891.5 sec | 0.0 |
| **DEEP SLEEP TOTAL:** |  | **891.5 sec** | **115.9 mAs** |

**Total Per Cycle:**

* **Total Energy:** 1,501.7 \+ 115.9 \= **1,617.6 mAs**  
* **Cycle Duration:** 900 seconds (15 minutes)

**Battery Life Calculation (3000 mAh):**

**Simplified Calculation Method:**

1. **Energy per hour:** 1,617.6 mAs × 4 cycles/hour \= 6,470.4 mAs/hour
2. **Convert to mAh:** 6,470.4 mAs ÷ 3,600 \= **1.80 mAh/hour** (Average current: **1.80 mA**)
3. **Battery life:** 3,000 mAh ÷ 1.80 mAh/hour \= **1,667 hours** \= **69.5 days**
4. **Total cycles:** 1,667 hours × 4 cycles/hour \= **6,668 cycles**

| Parameter | Value |
| :---- | :---- |
| Energy per Cycle | 1,617.6 mAs |
| Energy per Hour | 1.80 mAh (4 cycles/hour) |
| Average Current | 1.80 mA |
| Battery Life | **1,667 hours (69.5 days)** |
| Total Cycles | 6,668 cycles |

**Note:** The calculated values shown above represent ideal operating conditions. The summary table shows these ideal values along with realistic estimates that account for 85% efficiency factor (voltage regulator losses, temperature effects, battery aging, etc.) and real-world operating conditions. The realistic estimate of **59.1 days** (85% of ideal) should be used for practical battery life planning.

