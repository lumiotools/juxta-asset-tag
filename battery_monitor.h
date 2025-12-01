#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <Arduino.h>

class BatteryMonitor {
private:
  static const int BATTERY_ADC_PIN = 0;
  static const int ADC_MAX_VALUE = 4095; // ESP32-S3 has 12-bit ADC
  static constexpr float ADC_VOLTAGE_REF = 3.3f; // ADC reference voltage
  static constexpr float VOLTAGE_DIVIDER_RATIO = 4.235f; // R11(220K) + R12(68K) / R12(68K) = 288/68 ≈ 4.235
  // Note: R68(1K) and C26(100nF) form RC filter but don't affect divider ratio due to high ADC impedance
  
  static constexpr float BATTERY_MAX_VOLTAGE = 4.2f;
  static constexpr float BATTERY_MIN_VOLTAGE = 3.0f;
  static constexpr float BATTERY_NOMINAL_VOLTAGE = 3.7f;

public:
  static void initializeADC() {
    pinMode(BATTERY_ADC_PIN, INPUT);
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
  }

  static float readBatteryVoltage() {
    int rawADC = analogRead(BATTERY_ADC_PIN);
    float adcVoltage = (rawADC / (float)ADC_MAX_VALUE) * ADC_VOLTAGE_REF;
    float batteryVoltage = adcVoltage * VOLTAGE_DIVIDER_RATIO;
    return batteryVoltage;
  }

  static int getBatteryPercentage() {
    float voltage = readBatteryVoltage();
    
    if (voltage >= BATTERY_MAX_VOLTAGE) return 100;
    if (voltage <= BATTERY_MIN_VOLTAGE) return 0;
    
    float percentage = ((voltage - BATTERY_MIN_VOLTAGE) / 
                       (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0f;
    
    return (int)percentage;
  }

  static uint32_t getBatteryColor() {
    int percentage = getBatteryPercentage();
    
    if (percentage >= 75) {
      return 0x00FF00;
    } else if (percentage >= 25) {
      int greenComponent = (int)(255 * (percentage - 25) / 50);
      int redComponent = 255 - greenComponent;
      return (redComponent << 16) | (greenComponent << 8);
    } else {
      return 0xFF0000;
    }
  }
};

#endif
