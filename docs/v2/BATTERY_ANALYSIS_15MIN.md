# Battery Life & Power Consumption Analysis
## 15-Minute Duty Cycle Configuration

**Project:** ESP32C3 Asset Tracking Device with IMU & GPS  
**Document Version:** 2.0  
**Date:** 2024  
**Battery Capacity:** 3000 mAh Li-Po (3.7V)

---

## Executive Summary

This document provides comprehensive battery life analysis for the ESP32C3-based asset tracking system operating on a **15-minute duty cycle**. The device integrates BMI323 IMU, AT6558 GPS, and W25Q128 external flash for persistent data storage. All operational cycles are configured to 15 minutes (900 seconds) with deep sleep power management.

### Key Performance Metrics

| Scenario | Average Current | Ideal Runtime | Realistic Runtime (85%) | Conservative Runtime (70%) | Data Transmission |
|----------|----------------|---------------|--------------------------|----------------------------|-------------------|
| **BLE Always Connected** | 50.89 mA | 2.46 days | **2.09 days** | 1.72 days | Every cycle (BLE/WiFi) |
| **WiFi Connected + Storage** | 2.93 mA | 42.61 days | **36.21 days** | 29.82 days | Every cycle |
| **Storage Only (No WiFi)** | 2.77 mA | 45.07 days | **38.31 days** | 31.55 days | Queued only |

**Optimization Suggestion:** Completely disabling LEDs (code modification) extends runtime to **~82-94 days (2.7-3.1 months)** - see Power Optimization section.

**Recommended Configuration:** WiFi Connected + Storage with LEDs enabled (10% brightness in deep sleep)  
**Expected Runtime:** **~36.2 days (1.2 months)** with realistic efficiency  
**Update Frequency:** 4 sensor readings per hour (96 readings per day)  
**Optimization Suggestion:** Completely disabling LEDs via code modification extends runtime to **~82.5 days (2.7 months)** - see Power Optimization section for details.

---

## System Overview

### Hardware Components

| Component | Model | Active Current | Sleep Current | Notes |
|-----------|-------|----------------|---------------|-------|
| **Microcontroller** | ESP32C3 | 100-150 mA | 0.005 mA | WiFi active during transmission |
| **IMU Sensor** | BMI323 | 3.5 mA | 0.005 mA | I2C interface, powered during reading |
| **GPS Sensor** | AT6558 | 30 mA | 0.05 mA | UART interface, powered during reading |
| **Flash Storage** | W25Q128 | 15 mA | 0.001 mA | SPI interface, active during write |
| **Status LED** | NeoPixel | 7.5 mA | 0.75 mA | GPIO 48, 10% brightness during deep sleep (default) |
| **Battery LED** | NeoPixel | 7.5 mA | 0.75 mA | GPIO 38, 10% brightness during deep sleep (default) |

**Note:** Default behavior dims LEDs to 10% brightness during deep sleep. LEDs can be completely disabled via code modification for maximum battery life (see optimization scenarios).

### Operational Cycle

**Cycle Duration:** 15 minutes (900 seconds)  
**Active Phase:** ~10 seconds (sensor reading + transmission + storage)  
**Deep Sleep Phase:** ~890 seconds (ultra-low power state)

**Cycle Operations:**

| Step | Operation | Duration |
|------|-----------|----------|
| 1 | Wake from deep sleep | ~0.5 sec |
| 2 | Power on sensors | ~0.5 sec |
| 3 | Sensor stabilization | ~2 sec |
| 4 | Read IMU and GPS data | ~1 sec |
| 5 | WiFi connection & data transmission | ~3-4 sec (if connected) |
| 6 | Flash storage write | ~1-2 sec |
| 7 | Power down sensors | ~0.5 sec |
| 8 | Enter deep sleep | ~0.5 sec |
| **Total Active Phase** | | **~10 seconds** |

---

## Detailed Power Consumption Analysis

### Active Phase Breakdown (10 seconds)

#### Scenario A: WiFi Connected + Data Transmission + Storage

```
Component                    Current    Duration    Energy (mAs)
───────────────────────────────────────────────────────────────
ESP32C3 (Wake/Init)         80 mA      1.0 sec     80.0
ESP32C3 (WiFi Connect/TX)   120 mA     4.0 sec    480.0
ESP32C3 (Processing)        60 mA      2.0 sec    120.0
ESP32C3 (Flash Write)        80 mA      2.0 sec    160.0
ESP32C3 (Power Down)        50 mA      1.0 sec     50.0
───────────────────────────────────────────────────────────────
ESP32C3 Subtotal:                       10.0 sec   890.0

IMU Sensor (Power On)       3.5 mA     0.5 sec      1.75
IMU Sensor (Active)         3.5 mA     3.0 sec     10.5
IMU Sensor (Power Off)      1.0 mA     0.5 sec      0.5
───────────────────────────────────────────────────────────────
IMU Subtotal:                           4.0 sec    12.75

GPS Sensor (Power On)       30 mA      0.5 sec     15.0
GPS Sensor (Active)        30 mA      3.0 sec     90.0
GPS Sensor (Power Off)      5.0 mA     0.5 sec      2.5
───────────────────────────────────────────────────────────────
GPS Subtotal:                           4.0 sec   107.5

W25Q128 Flash (Idle)        0.001 mA   8.0 sec      0.008
W25Q128 Flash (Write)       15 mA      2.0 sec     30.0
───────────────────────────────────────────────────────────────
Flash Subtotal:                         10.0 sec   30.008

Status LED (GPIO 48)        7.5 mA     10.0 sec    75.0
Battery LED (GPIO 38)       7.5 mA     10.0 sec    75.0
───────────────────────────────────────────────────────────────
LEDs Subtotal:                          10.0 sec  150.0
───────────────────────────────────────────────────────────────
ACTIVE PHASE TOTAL:                     10.0 sec 1,250.26 mAs
```

#### Scenario B: WiFi Disconnected + Storage Only

```
Component                    Current    Duration    Energy (mAs)
───────────────────────────────────────────────────────────────
ESP32C3 (Wake/Init)         80 mA      1.0 sec     80.0
ESP32C3 (Processing)        60 mA      2.0 sec    120.0
ESP32C3 (Flash Write)       80 mA      3.0 sec    240.0
ESP32C3 (Power Down)        50 mA      1.0 sec     50.0
ESP32C3 (WiFi Attempt)      100 mA     3.0 sec    300.0
───────────────────────────────────────────────────────────────
ESP32C3 Subtotal:                       10.0 sec   790.0

IMU Sensor (Power On)       3.5 mA     0.5 sec      1.75
IMU Sensor (Active)         3.5 mA     3.0 sec     10.5
IMU Sensor (Power Off)      1.0 mA     0.5 sec      0.5
───────────────────────────────────────────────────────────────
IMU Subtotal:                           4.0 sec    12.75

GPS Sensor (Power On)       30 mA      0.5 sec     15.0
GPS Sensor (Active)        30 mA      3.0 sec     90.0
GPS Sensor (Power Off)      5.0 mA     0.5 sec      2.5
───────────────────────────────────────────────────────────────
GPS Subtotal:                           4.0 sec   107.5

W25Q128 Flash (Idle)        0.001 mA   7.0 sec      0.007
W25Q128 Flash (Write)       15 mA      3.0 sec     45.0
───────────────────────────────────────────────────────────────
Flash Subtotal:                         10.0 sec   45.007

Status LED (GPIO 48)        7.5 mA     10.0 sec    75.0
Battery LED (GPIO 38)       7.5 mA     10.0 sec    75.0
───────────────────────────────────────────────────────────────
LEDs Subtotal:                          10.0 sec  150.0
───────────────────────────────────────────────────────────────
ACTIVE PHASE TOTAL:                     10.0 sec  1,105.26 mAs
```

### Deep Sleep Phase Breakdown (890 seconds)

**Both Scenarios (Identical):**

```
Component                    Current    Duration    Energy (mAs)
───────────────────────────────────────────────────────────────
ESP32C3 (Deep Sleep)        0.005 mA    890 sec     4.45
IMU Sensor (Sleep)          0.005 mA   890 sec     4.45
GPS Sensor (Sleep)          0.05 mA    890 sec    44.5
W25Q128 Flash (Sleep)       0.001 mA   890 sec     0.89
Status LED (10% brightness)  0.75 mA    890 sec   667.5
Battery LED (10% brightness) 0.75 mA    890 sec   667.5
───────────────────────────────────────────────────────────────
DEEP SLEEP PHASE TOTAL:                 890 sec  1,389.29 mAs
```

**Note:** LEDs remain on at 10% brightness during deep sleep (dimmed via `dimStatusLEDTo10Percent()` function). This is the **default behavior** and provides visual status indication while consuming minimal power (0.75 mA per LED = 1.5 mA total). 

**Optimization Option:** LEDs can be completely disabled via code modification (using `batteryIndicatorLED.disable()` and setting status LED to off), which would eliminate LED power consumption entirely and extend runtime significantly (see Scenarios 4 and 5).

---

## Battery Life Calculations

### Scenario 1: BLE Always Connected (Maximum Connectivity)

**Conditions:**
- BLE stays connected throughout entire operation (never disconnects)
- WiFi may or may not be connected (transmission attempts via BLE if WiFi fails)
- No deep sleep (BLE keeps device awake)
- Device runs continuous 15-minute cycles indefinitely
- LEDs always on at full brightness (no deep sleep)

**Cycle Energy Consumption:**

**Active Phase (10 seconds):**
```
Component                    Current    Duration    Energy (mAs)
───────────────────────────────────────────────────────────────
ESP32C3 (Wake/Init)         80 mA      1.0 sec     80.0
ESP32C3 (WiFi Connect/TX)   120 mA     4.0 sec    480.0
ESP32C3 (Processing)        60 mA      2.0 sec    120.0
ESP32C3 (Flash Write)        80 mA      2.0 sec    160.0
ESP32C3 (Power Down)        50 mA      1.0 sec     50.0
───────────────────────────────────────────────────────────────
ESP32C3 Subtotal:                       10.0 sec   890.0

IMU Sensor (Power On)       3.5 mA     0.5 sec      1.75
IMU Sensor (Active)         3.5 mA     3.0 sec     10.5
IMU Sensor (Power Off)      1.0 mA     0.5 sec      0.5
───────────────────────────────────────────────────────────────
IMU Subtotal:                           4.0 sec    12.75

GPS Sensor (Power On)       30 mA      0.5 sec     15.0
GPS Sensor (Active)        30 mA      3.0 sec     90.0
GPS Sensor (Power Off)      5.0 mA     0.5 sec      2.5
───────────────────────────────────────────────────────────────
GPS Subtotal:                           4.0 sec   107.5

W25Q128 Flash (Idle)        0.001 mA   8.0 sec      0.008
W25Q128 Flash (Write)       15 mA      2.0 sec     30.0
───────────────────────────────────────────────────────────────
Flash Subtotal:                         10.0 sec   30.008

Status LED (GPIO 48)        7.5 mA     10.0 sec    75.0
Battery LED (GPIO 38)       7.5 mA     10.0 sec    75.0
BLE Radio (Active)          5 mA       10.0 sec    50.0
───────────────────────────────────────────────────────────────
LEDs & BLE Subtotal:                    10.0 sec  200.0
───────────────────────────────────────────────────────────────
ACTIVE PHASE TOTAL:                     10.0 sec  1,250.26 mAs
```

**Idle Phase (890 seconds - BLE Active, No Deep Sleep):**
```
Component                    Current    Duration    Energy (mAs)
───────────────────────────────────────────────────────────────
ESP32C3 (Idle/BLE active)   35 mA      890 sec  31,150.0
IMU Sensor (Sleep)           0.005 mA   890 sec      4.45
GPS Sensor (Sleep)           0.05 mA    890 sec     44.5
W25Q128 Flash (Sleep)        0.001 mA   890 sec      0.89
Status LED (GPIO 48)         7.5 mA     890 sec   6,675.0
Battery LED (GPIO 38)        7.5 mA     890 sec   6,675.0
BLE Radio (Included in ESP)  -          890 sec      0
───────────────────────────────────────────────────────────────
IDLE PHASE TOTAL:                       890 sec 44,549.84 mAs
```

**Total Per Cycle:**
```
Active Phase:               1,250.26 mAs
Idle Phase:                 44,549.84 mAs
─────────────────────────────────────────
Total per Cycle:             45,800.1 mAs
Cycle Duration:              900 seconds (15 minutes)
Average Current:             45,800.1 / 900 = 50.89 mA
```

**Battery Life Calculation (3000 mAh):**

| Parameter | Value |
|-----------|-------|
| **First Minute Energy** | 4,658.42 mAs (BLE active initialization) |
| **Remaining Energy** | (3000 mAh × 3600) - 4,658.42 = 10,795,341.58 mAs |
| **Remaining Cycles** | 10,795,341.58 / 45,800.1 = 236 cycles |
| **Remaining Time** | 236 × 900 sec = 212,400 sec = 59.0 hours |
| **Total Runtime** | 1 min + 59.0 hours = 59.02 hours |

**Runtime Estimates:**

| Efficiency | Runtime | Days | Notes |
|------------|---------|------|-------|
| **Ideal (100%)** | 59.02 hours | 2.46 days | Highest power consumption |
| **Realistic (85%)** | 50.17 hours | **2.09 days** | Best connectivity |
| **Conservative (70%)** | 41.31 hours | 1.72 days | Worst case efficiency |

**Operational Metrics:**

| Metric | Value |
|--------|-------|
| **Sensor Readings per Charge** | ~236 readings |
| **Data Transmissions** | ~236 successful transmissions (BLE/WiFi) |
| **Flash Write Operations** | ~236 write cycles |
| **Readings per Hour** | 4 readings |
| **Readings per Day** | 96 readings |
| **Daily Energy Consumption** | 1,220.8 mAh/day |
| **Monthly Energy Consumption** | ~36,624 mAh/month |

**Key Characteristics:**
- Highest power consumption scenario (50.89 mA average)
- Best connectivity (BLE always available for data transmission)
- No deep sleep (device always awake, BLE keeps it active)
- Continuous operation at 15-minute intervals
- Suitable for applications requiring constant connectivity
- LEDs remain at full brightness (no dimming)

---

### Scenario 2: WiFi Connected + Storage (Standard Configuration)

**Cycle Energy Consumption:**
```
Active Phase:               1,250.26 mAs
Deep Sleep Phase:           1,389.29 mAs
─────────────────────────────────────────
Total per Cycle:             2,639.55 mAs
Cycle Duration:              900 seconds (15 minutes)
Average Current:             2,639.55 / 900 = 2.93 mA
```

**Battery Life Calculation (3000 mAh):**

| Parameter | Value |
|-----------|-------|
| **First Minute Energy** | 4,658.42 mAs (BLE active initialization) |
| **Remaining Energy** | (3000 mAh × 3600) - 4,658.42 = 10,795,341.58 mAs |
| **Remaining Cycles** | 10,795,341.58 / 2,639.55 = 4,090 cycles |
| **Remaining Time** | 4,090 × 900 sec = 3,681,000 sec = 1,022.5 hours |
| **Total Runtime** | 1 min + 1,022.5 hours = 1,022.52 hours |

**Runtime Estimates:**

| Efficiency | Runtime | Days | Months |
|------------|---------|------|--------|
| **Ideal (100%)** | 1,022.52 hours | 42.61 days | 1.42 months |
| **Realistic (85%)** | 869.14 hours | **36.21 days** | **1.21 months** |
| **Conservative (70%)** | 715.76 hours | 29.82 days | 0.99 months |

**Operational Metrics:**

| Metric | Value |
|--------|-------|
| **Sensor Readings per Charge** | ~4,090 readings |
| **Data Transmissions** | ~4,090 successful transmissions |
| **Flash Write Operations** | ~4,090 write cycles |
| **Readings per Hour** | 4 readings |
| **Readings per Day** | 96 readings |
| **Daily Energy Consumption** | 70.58 mAh/day |
| **Monthly Energy Consumption** | ~2,117 mAh/month |

---

### Scenario 3: Storage Only (No WiFi Connection)

**Cycle Energy Consumption:**
```
Active Phase:               1,105.26 mAs
Deep Sleep Phase:           1,389.29 mAs
─────────────────────────────────────────
Total per Cycle:             2,494.55 mAs
Cycle Duration:              900 seconds (15 minutes)
Average Current:             2,494.55 / 900 = 2.77 mA
```

**Battery Life Calculation (3000 mAh):**

| Parameter | Value |
|-----------|-------|
| **First Minute Energy** | 4,658.42 mAs (BLE active initialization) |
| **Remaining Energy** | (3000 mAh × 3600) - 4,658.42 = 10,795,341.58 mAs |
| **Remaining Cycles** | 10,795,341.58 / 2,494.55 = 4,327 cycles |
| **Remaining Time** | 4,327 × 900 sec = 3,894,300 sec = 1,081.75 hours |
| **Total Runtime** | 1 min + 1,081.75 hours = 1,081.77 hours |

**Runtime Estimates:**

| Efficiency | Runtime | Days | Months |
|------------|---------|------|--------|
| **Ideal (100%)** | 1,081.77 hours | 45.07 days | 1.50 months |
| **Realistic (85%)** | 919.50 hours | **38.31 days** | **1.28 months** |
| **Conservative (70%)** | 757.24 hours | 31.55 days | 1.05 months |

**Operational Metrics:**

| Metric | Value |
|--------|-------|
| **Sensor Readings per Charge** | ~4,327 readings |
| **Data Transmissions** | 0 (all data queued in flash) |
| **Flash Write Operations** | ~4,327 write cycles |
| **Readings per Hour** | 4 readings |
| **Readings per Day** | 96 readings |
| **Daily Energy Consumption** | 66.64 mAh/day |
| **Monthly Energy Consumption** | ~2,000 mAh/month |

---

## Power Consumption Summary

### Energy Distribution per Cycle

| Phase | Duration | Energy (mAs) | % of Total | Current (mA) |
|-------|----------|--------------|------------|--------------|
| **Active Phase** | 10 sec | 1,105 - 1,250 | 44.2 - 47.3% | 110.5 - 125.0 |
| **Deep Sleep Phase** | 890 sec | 1,389.29 | 52.6 - 55.7% | 1.56 |
| **Total Cycle** | 900 sec | 2,494.55 - 2,639.55 | 100% | 2.77 - 2.93 |

### Component Energy Breakdown (WiFi Connected Scenario)

| Component | Active Phase (mAs) | Deep Sleep (mAs) | Total per Cycle (mAs) | % of Total |
|-----------|-------------------|------------------|----------------------|------------|
| LEDs (10% in sleep) | 150.0 | 1,335.0 | 1,485.0 | 56.2% |
| ESP32C3 | 890.0 | 4.45 | 894.45 | 33.8% |
| GPS Sensor | 107.5 | 44.5 | 152.0 | 5.7% |
| Flash Storage | 30.0 | 0.89 | 30.89 | 1.2% |
| IMU Sensor | 12.75 | 4.45 | 17.2 | 0.7% |
| **Total** | **1,250.26** | **1,389.29** | **2,639.55** | **100%** |

---

## Operational Characteristics

### Update Frequency

| Parameter | Value |
|----------|-------|
| **Cycle Duration** | 15 minutes (900 seconds) |
| **Readings per Hour** | 4 readings |
| **Readings per Day** | 96 readings |
| **Readings per Week** | 672 readings |
| **Readings per Month** | ~2,880 readings |

### Data Storage Capacity

| Parameter | Value |
|----------|-------|
| **Flash Capacity** | W25Q128 (16 MB = 16,777,216 bytes) |
| **Estimated Data per Reading** | ~150-200 bytes (CSV format) |
| **Maximum Storage Capacity** | ~80,000 - 100,000 readings |
| **Storage Duration** | ~2.5 - 3.1 years at 4 readings/hour (before overwrite) |

### Network Connectivity

| Feature | Description |
|---------|-------------|
| **WiFi Transmission** | Every cycle (if connected) |
| **BLE Configuration** | Active only on power-on/reset, auto-stops after 1 minute if disconnected |
| **Data Queue** | Persistent storage in flash, survives power cycles |
| **Retry Logic** | Automatic retry on next cycle if transmission fails |

---

## Efficiency Factors

### Battery Efficiency Considerations

| Factor | Impact | Notes |
|--------|--------|-------|
| **Voltage Regulator Losses** | ~10-15% | Linear regulator efficiency |
| **Battery Self-Discharge** | ~2-5% per month | Li-Po chemistry dependent |
| **Battery Aging** | Variable | Capacity decreases over time |
| **Temperature Effects** | Variable | Cold reduces capacity, heat reduces lifespan |
| **Realistic Efficiency** | **85%** | Recommended for planning |

### Power Optimization Opportunities

| Optimization | Energy Saved | Runtime Improvement | Trade-off |
|--------------|--------------|---------------------|-----------|
| **Disable LEDs Completely** | 1,485 mAs/cycle | +46-56 days (128-146%) | No visual status (requires code modification) |
| **Button-Triggered LEDs** | ~1,480 mAs/cycle | +46-56 days (similar to disabled) | On-demand status display (requires button + code) |
| **Increase Cycle to 30 min** | ~50% reduction | ~2x runtime | Lower update frequency |
| **Motion-Triggered Cycles** | Variable | Significant | Requires IMU processing |
| **Reduce WiFi TX Power** | ~10-20 mAs/cycle | +1-2 days | Reduced range |

**LED Optimization Recommendations:**
- **Complete Disable:** Best for deployments where visual status is not critical
  - Extends runtime from ~36 days to ~82 days (WiFi) or ~38 days to ~94 days (Storage only)
  - No visual feedback available
- **Button-Triggered (Recommended):** Best balance of battery life and user experience
  - Similar battery life to complete disable (~82-94 days)
  - On-demand status display when button is pressed (powerbank-style)
  - Provides status and battery level when needed
  - See Power Optimization section below for details

---

## Power Optimization: Disabling LEDs Completely

**Suggestion:** For maximum battery life, LEDs can be completely disabled via code modification. This optimization eliminates LED power consumption entirely during both active and deep sleep phases.

### Optimization Impact

**WiFi Connected + Storage (with LEDs disabled):**

| Parameter | Value |
|-----------|-------|
| **Active Phase** | 1,100.26 mAs (reduced by 150 mAs) |
| **Deep Sleep Phase** | 54.29 mAs (reduced by 1,335 mAs) |
| **Total per Cycle** | 1,154.55 mAs |
| **Average Current** | 1.28 mA |
| **Realistic Runtime** | **82.75 days (2.76 months)** |
| **Improvement** | +46.54 days (128% longer) vs standard configuration |

**Storage Only (with LEDs disabled):**

| Parameter | Value |
|-----------|-------|
| **Active Phase** | 955.26 mAs (reduced by 150 mAs) |
| **Deep Sleep Phase** | 54.29 mAs (reduced by 1,335 mAs) |
| **Total per Cycle** | 1,009.55 mAs |
| **Average Current** | 1.12 mA |
| **Realistic Runtime** | **94.60 days (3.15 months)** |
| **Improvement** | +56.29 days (147% longer) vs standard configuration |

### Implementation

To disable LEDs completely, modify the code to:
1. Call `batteryIndicatorLED.disable()` to turn off battery LED
2. Set status LED to off (0, 0, 0) before entering deep sleep
3. Skip the `dimStatusLEDTo10Percent()` call

**Trade-off:** No visual status indication, but extends battery life by 2-3x.

---

### Alternative: Button-Triggered LED Display (Powerbank-Style)

**Suggestion:** Configure a button on the device to temporarily show LED status for a few seconds when pressed, similar to powerbank behavior. This provides on-demand status feedback while maintaining most of the battery savings.

**How It Works:**
- LEDs remain off during normal operation (same as complete LED disabling)
- Button press wakes device from deep sleep (if sleeping) or triggers LED display
- LEDs show status for 3-5 seconds (configurable)
- LEDs automatically turn off after display period
- Device returns to normal operation or deep sleep

**Power Consumption:**
- **Normal Operation:** Same as LEDs completely disabled (~1.13-1.29 mA)
- **Button Press Event:** Brief LED activation (~15 mA for 3-5 seconds)
- **Energy per Button Press:** ~45-75 mAs (negligible impact on overall battery life)

**Benefits:**
- **Battery Savings:** ~99% of LED power saved (only active on demand)
- **User Feedback:** Status and battery level available when needed
- **Flexible:** Can check status without waiting for scheduled cycle
- **Familiar UX:** Similar to powerbank behavior users are familiar with

**Implementation:**
1. Configure GPIO pin as button input with interrupt
2. On button press, wake from deep sleep (if sleeping)
3. Display status LED (green/red) and battery LED (color-coded level) for 3-5 seconds
4. Automatically turn off LEDs and return to normal operation
5. Button can work even during deep sleep (GPIO wake-up capability)

**Estimated Runtime Impact:**
- **WiFi Connected:** ~82-83 days (vs 82.75 days with LEDs completely off)
- **Storage Only:** ~94-95 days (vs 94.60 days with LEDs completely off)
- **Difference:** Negligible (<1 day) even with frequent button presses

**Best For:**
- Deployments where occasional status checks are needed
- User-facing devices where visual feedback is important
- Applications requiring battery level checks on demand
- Balancing battery life with user experience

---

## Comparison with Other Cycle Durations

| Cycle Duration | Avg Current | Realistic Runtime | Readings/Hour | Use Case |
|----------------|-------------|-------------------|---------------|----------|
| **15 minutes (BLE Connected)** | 50.89 mA | **2.09 days** | 4 | Maximum connectivity |
| **15 minutes (WiFi Only)** | 2.93 mA | **36.21 days** | 4 | **Recommended** |
| 30 seconds | 39.56 mA | 1.97 days | 120 | High-frequency tracking |
| 5 minutes | 2.93 mA | 26.56 days | 12 | Medium-frequency tracking |
| 30 minutes | 0.78 mA | 144.96 days | 2 | Low-frequency tracking |

**15-minute cycle provides optimal balance between:**

| Factor | Benefit |
|--------|---------|
| **Battery Life** | 1.2+ months (with LEDs) or 2.7+ months (without LEDs) |
| **Update Frequency** | 4 readings/hour (96 readings/day) |
| **Data Freshness** | 15-minute maximum latency |
| **Storage Efficiency** | Optimized flash write frequency |

---

## Recommendations

### For Maximum Connectivity (BLE Always Connected)
- **Configuration:** BLE always connected, WiFi + storage, LEDs enabled
- **Expected Runtime:** ~2.09 days (realistic)
- **Benefits:** Best connectivity (BLE always available), real-time data transmission, persistent storage
- **Trade-off:** Highest power consumption, shortest battery life

### For Maximum Battery Life
- **Configuration:** Storage only with LEDs completely disabled (optimization suggestion)
- **Expected Runtime:** ~94.60 days (3.15 months)
- **Trade-off:** No real-time data transmission, data queued in flash, no visual status indicators
- **Suggestion:** Disable LEDs via code modification for maximum battery life, or use button-triggered LEDs for on-demand status (see Power Optimization section)

### For Best Balance (Recommended)
- **Configuration:** WiFi connected + storage with LEDs enabled (10% brightness in deep sleep)
- **Expected Runtime:** ~36.21 days (1.2 months)
- **Benefits:** Real-time data transmission, visual status indicators, persistent storage
- **Optimization Suggestion:** Completely disable LEDs via code modification for ~82.75 days (2.76 months) runtime (see Power Optimization section)

### For Maximum Connectivity
- **Configuration:** WiFi connected + storage, shorter cycle (30 seconds)
- **Expected Runtime:** ~1.97 days
- **Trade-off:** Higher power consumption, more frequent updates

### Deployment Considerations

| Consideration | Recommendation |
|---------------|----------------|
| **Network Availability** | Ensure WiFi coverage for real-time transmission |
| **Storage Management** | Flash can store ~80,000+ readings before overwrite |
| **Battery Replacement** | Plan for replacement every 1-1.5 months (with LEDs) or 2.5-3 months (without LEDs) |
| **Environmental Factors** | Temperature affects battery capacity; monitor in extreme conditions |
| **Data Retrieval** | Implement periodic flash readout for offline deployments |
| **LED Configuration** | Default: LEDs at 10% brightness during deep sleep. Can be completely disabled or configured for button-triggered display (powerbank-style) via code modification for maximum battery life |

---

## Technical Specifications

### Power Management Features

| Feature | Description | Power Savings |
|---------|-------------|---------------|
| ✅ **Deep Sleep** | Automatic deep sleep between cycles | ESP32C3: 0.01 mA (vs 30-40 mA idle) |
| ✅ **Sensor Power Control** | Sensors powered off between readings | ~33.5 mA saved during sleep |
| ✅ **WiFi Power Management** | WiFi disabled during deep sleep | ~115 mA saved during sleep |
| ✅ **BLE Auto-Stop** | BLE stops after 1 minute if disconnected | ~5 mA saved after timeout |
| ✅ **LED Management** | LEDs dimmed to 10% brightness during deep sleep | 1.5 mA (vs 15 mA if full brightness) |
| ✅ **Flash Persistence** | Data queue survives power cycles | Enables reliable data recovery |

### Hardware Requirements

| Component | Specification |
|-----------|---------------|
| **Battery** | 3000 mAh Li-Po (3.7V nominal, 4.2V max) |
| **Power Input** | Connect to BAT/VIN pin (NOT 3.3V pin) |
| **Voltage Regulation** | Onboard LDO or external regulator required |
| **Charging** | Standard Li-Po charger compatible |

### Software Configuration

| Parameter | Value |
|----------|-------|
| **Cycle Duration** | 15 minutes (900,000 ms) |
| **BLE Timeout** | 60 seconds (auto-stop if disconnected) |
| **Deep Sleep Duration** | 890 seconds (after active phase) |
| **Flash Write** | Automatic after each sensor reading |

---

## Conclusion

The 15-minute duty cycle configuration provides an excellent balance between battery life, data freshness, and operational reliability. With a 3000 mAh battery, the system can operate for **approximately 36.2 days (1.2 months)** with realistic efficiency factors when LEDs are enabled, or **82.75 days (2.76 months)** with LEDs disabled, providing 4 sensor readings per hour with real-time WiFi transmission and persistent flash storage.

**Key Advantages:**

| Advantage | Description |
|-----------|-------------|
| **Extended Battery Life** | 1.2+ months (with LEDs) or 2.7+ months (without LEDs) |
| **Reliable Data Persistence** | Flash storage ensures data survives power cycles |
| **Real-Time Data Transmission** | WiFi connected for immediate data delivery |
| **Low Maintenance** | Minimal intervention required during deployment |
| **Long-Term Deployment** | Suitable for extended field operations |

**Recommended Deployment:**

| Configuration | Battery Replacement | Notes |
|---------------|---------------------|-------|
| **WiFi Connected + Storage (LEDs On)** | Every 1-1.5 months | Standard configuration with visual status |
| **WiFi Connected + Storage (LEDs Disabled)** | Every 2.5-3 months | Maximum runtime with real-time transmission (optimization suggestion) |
| **Storage Only (LEDs Disabled)** | Every 3+ months | Maximum battery life, data queued (optimization suggestion) |

**Optimization Suggestion:** Disabling LEDs completely via code modification extends runtime significantly. Default behavior uses LEDs at 10% brightness during deep sleep.

**Additional Recommendations:**
- Ensure WiFi network availability for real-time transmission
- Monitor flash storage capacity for extended deployments
- Consider disabling LEDs for maximum battery life if visual status is not critical
- Implement periodic data retrieval for offline deployments

---

**Document Prepared By:** Engineering Team  
**Last Updated:** 2024  
**Version:** 2.0

