#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <Arduino.h>

// Battery reading states for non-blocking operation
enum BatteryReadingState {
  BATTERY_IDLE,     // Not currently reading
  BATTERY_READING,  // Taking readings over time
  BATTERY_READY     // New reading available
};

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
  
  // Non-blocking reading state variables
  static BatteryReadingState readingState;
  static unsigned long lastReadingTime;
  static int sampleCount;
  static uint32_t accumulatedVoltage;
  static float latestVoltage;

public:
  static void initializeADC() {
    pinMode(BATTERY_ADC_PIN, INPUT);
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);  // 0-3.3V range
  }

  // Non-blocking battery voltage reading - call this repeatedly in main loop
  static void updateBatteryReading() {
    unsigned long currentTime = millis();
    
    switch (readingState) {
      case BATTERY_IDLE:
        // Start new reading cycle
        readingState = BATTERY_READING;
        lastReadingTime = currentTime;
        sampleCount = 0;
        accumulatedVoltage = 0;
        // Take first sample immediately
        accumulatedVoltage += analogReadMilliVolts(BATTERY_ADC_PIN);
        sampleCount++;
        break;
        
      case BATTERY_READING:
        // Check if 500 milliseconds has passed since last reading
        if (currentTime - lastReadingTime >= 500) {
          accumulatedVoltage += analogReadMilliVolts(BATTERY_ADC_PIN);
          sampleCount++;
          lastReadingTime = currentTime;
          
          // Check if we have all 5 samples
          if (sampleCount >= 5) {
            // Convert to actual battery voltage using divider ratio
            latestVoltage = (accumulatedVoltage / 5.0f / 1000.0f) * VOLTAGE_DIVIDER_RATIO;
            readingState = BATTERY_READY;
          }
        }
        break;
        
      case BATTERY_READY:
        // Reading complete, wait for next request
        break;
    }
  }
  
  // Check if a new battery reading is available
  static bool isNewReadingAvailable() {
    return readingState == BATTERY_READY;
  }
  
  // Get the latest battery voltage and start next reading cycle
  static float readBatteryVoltage() {
    if (readingState == BATTERY_READY) {
      readingState = BATTERY_IDLE; // Start next cycle
    }
    return latestVoltage;
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

// Static variable definitions
BatteryReadingState BatteryMonitor::readingState = BATTERY_IDLE;
unsigned long BatteryMonitor::lastReadingTime = 0;
int BatteryMonitor::sampleCount = 0;
uint32_t BatteryMonitor::accumulatedVoltage = 0;
float BatteryMonitor::latestVoltage = 3.7f;  // Default safe value

#endif
