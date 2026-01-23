#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <Arduino.h>

class BatteryMonitor {
private:
  // Hardware Configuration (v2.5 - ESP32-C6)
  static const int BATTERY_ADC_PIN = 3;  // GPIO3 (IO3) connected to voltage divider
  static constexpr float BATTERY_MAX_VOLTAGE = 4.2f;  // Fully charged Li-Po
  static constexpr float BATTERY_MIN_VOLTAGE = 3.2f;  // Safe discharge limit
  
  // Voltage Divider Configuration
  // VBAT → R11 (220K) → [ADC Node] → R12 (220K) → GND
  // Divider Ratio = (R11 + R12) / R12 = (220K + 220K) / 220K = 2.0
  // Battery Voltage = ADC_Voltage × 2.0
  // 
  // Note: If hardware uses different resistor values, update this constant:
  //   - R11=220K, R12=100K → ratio = 3.2
  //   - R11=220K, R12=68K  → ratio = 4.235 (older v2 hardware)
  static constexpr float VOLTAGE_DIVIDER_RATIO = 2.0f;

public:
  static void initializeADC() {
    pinMode(BATTERY_ADC_PIN, INPUT);
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);  // 0-3.3V range
  }

  static float readBatteryVoltage() {
    uint32_t Vbatt = 0;
    
    // Take 16 samples and average for stability
    for(int i = 0; i < 16; i++) {
      Vbatt = Vbatt + analogReadMilliVolts(BATTERY_ADC_PIN);
    }
    
    // Convert to actual battery voltage using divider ratio
    // Average the samples (÷16), convert mV to V (÷1000), apply divider ratio
    float Vbattf = (Vbatt / 16.0f / 1000.0f) * VOLTAGE_DIVIDER_RATIO;
    return Vbattf;
  }

  static int getBatteryPercentage() {
    float voltage = readBatteryVoltage();
    
    if (voltage >= BATTERY_MAX_VOLTAGE) return 100;
    if (voltage <= BATTERY_MIN_VOLTAGE) return 0;
    
    float percentage = ((voltage - BATTERY_MIN_VOLTAGE) / 
                       (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0f;
    
    return (int)percentage;
  }

  static int getBatteryPercentageV(float voltage) {
    if (voltage >= BATTERY_MAX_VOLTAGE) return 100;
    if (voltage <= BATTERY_MIN_VOLTAGE) return 0;
    
    float percentage = ((voltage - BATTERY_MIN_VOLTAGE) / 
                       (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0f;
    
    return (int)percentage;
  }

  static uint32_t getBatteryColor() {
    int percentage = getBatteryPercentage();
    
    if (percentage <= 20) {
      return 0xFF0000;
    }
    else{
      return 0x000000;
    }
  }
};

#endif
