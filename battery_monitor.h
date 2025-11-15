#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <Arduino.h>

class BatteryMonitor {
private:
  static const int BATTERY_ADC_PIN = 4;
  static const int ADC_MAX_VALUE = 4095; // ESP32-S3 has 12-bit ADC
  static const float ADC_VOLTAGE_REF = 3.3f; // ADC reference voltage
  static const float VOLTAGE_DIVIDER_RATIO = 4.235f; // R11(220K) + R12(68K) / R12(68K) = 288/68 ≈ 4.235
  // Note: R68(1K) and C26(100nF) form RC filter but don't affect divider ratio due to high ADC impedance
  
  // Battery specifications for 3.7V 2200mAh Li-Po
  static const float BATTERY_MAX_VOLTAGE = 4.2f; // Fully charged
  static const float BATTERY_MIN_VOLTAGE = 3.0f; // Cut-off voltage (safe minimum)
  static const float BATTERY_NOMINAL_VOLTAGE = 3.7f; // Nominal voltage

public:
  // Initialize ADC for battery monitoring
  static void initializeADC() {
    pinMode(BATTERY_ADC_PIN, INPUT);
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db); // Full 0-3.3V range
    Serial.println("Battery ADC initialized on GPIO " + String(BATTERY_ADC_PIN));
  }

  // Read raw ADC value (0-4095)
  static int readRawADC() {
    return analogRead(BATTERY_ADC_PIN);
  }

  // Convert ADC reading to actual battery voltage
  static float readBatteryVoltage() {
    int rawADC = readRawADC();
    // Convert ADC value to voltage at ADC pin
    float adcVoltage = (rawADC / (float)ADC_MAX_VALUE) * ADC_VOLTAGE_REF;
    // Convert to actual battery voltage using voltage divider formula
    // V_battery = V_adc * (R11 + R12) / R12
    float batteryVoltage = adcVoltage * VOLTAGE_DIVIDER_RATIO;
    return batteryVoltage;
  }

  // Calculate battery percentage (0-100%)
  static int getBatteryPercentage() {
    float voltage = readBatteryVoltage();
    
    // Clamp voltage to safe range
    if (voltage >= BATTERY_MAX_VOLTAGE) {
      return 100;
    }
    if (voltage <= BATTERY_MIN_VOLTAGE) {
      return 0;
    }
    
    // Linear interpolation between min and max voltage
    float percentage = ((voltage - BATTERY_MIN_VOLTAGE) / 
                       (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0f;
    
    // Ensure percentage is between 0-100
    if (percentage > 100) percentage = 100;
    if (percentage < 0) percentage = 0;
    
    return (int)percentage;
  }

  // Get battery status string
  static String getBatteryStatus() {
    float voltage = readBatteryVoltage();
    int percentage = getBatteryPercentage();
    
    String status = "Battery: ";
    status += String(voltage, 2) + "V | ";
    status += String(percentage) + "% | ";
    
    if (percentage >= 75) {
      status += "Excellent";
    } else if (percentage >= 50) {
      status += "Good";
    } else if (percentage >= 25) {
      status += "Low";
    } else if (percentage > 0) {
      status += "Critical";
    } else {
      status += "Empty";
    }
    
    return status;
  }

  // Get RGB color based on battery percentage
  // Returns: 32-bit value with R, G, B components (0x00RRGGBB)
  static uint32_t getBatteryColor() {
    int percentage = getBatteryPercentage();
    
    // Green: 100-75%
    if (percentage >= 75) {
      return (0xFF << 8) | 0x00; // Green (0x00FF00)
    }
    // Yellow: 74-25% (mix of Red and Green)
    else if (percentage >= 25) {
      // Interpolate between green and red
      // At 75%: full green
      // At 25%: full red
      int greenComponent = (int)(255 * (percentage - 25) / 50);
      int redComponent = 255 - greenComponent;
      return (redComponent << 16) | (greenComponent << 8);
    }
    // Red: 24-0%
    else {
      return 0xFF0000; // Red (0xFF0000)
    }
  }

  // Get battery health status
  static String getHealthStatus() {
    float voltage = readBatteryVoltage();
    
    if (voltage > BATTERY_MAX_VOLTAGE) {
      return "OVER_VOLTAGE";
    } else if (voltage < BATTERY_MIN_VOLTAGE) {
      return "CRITICAL_LOW";
    } else if (voltage >= BATTERY_NOMINAL_VOLTAGE) {
      return "HEALTHY";
    } else {
      return "DEGRADED";
    }
  }

  // Print detailed battery information
  static void printBatteryInfo() {
    Serial.println("\n=== Battery Monitor Info ===");
    Serial.println("Voltage: " + String(readBatteryVoltage(), 2) + "V");
    Serial.println("Percentage: " + String(getBatteryPercentage()) + "%");
    Serial.println("Status: " + getBatteryStatus());
    Serial.println("Health: " + getHealthStatus());
    Serial.println("Raw ADC: " + String(readRawADC()));
    Serial.println("===========================\n");
  }

  // Get voltage specifications
  static float getMaxVoltage() { return BATTERY_MAX_VOLTAGE; }
  static float getMinVoltage() { return BATTERY_MIN_VOLTAGE; }
  static float getNominalVoltage() { return BATTERY_NOMINAL_VOLTAGE; }
};

#endif
