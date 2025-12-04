#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <Arduino.h>

class BatteryMonitor {
private:
  static const int BATTERY_ADC_PIN = 0;
  static constexpr float BATTERY_MAX_VOLTAGE = 4.2f;
  static constexpr float BATTERY_MIN_VOLTAGE = 3.0f;

public:
  static void initializeADC() {
    pinMode(BATTERY_ADC_PIN, INPUT);
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
  }

  static float readBatteryVoltage() {
    uint32_t Vbatt = 0;
    
    for(int i = 0; i < 16; i++) {
      Vbatt = Vbatt + analogReadMilliVolts(A0); // ADC with correction
    }
    
    float Vbattf = 2 * Vbatt / 16 / 1000.0; // attenuation ratio 1/2, mV --> V
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
