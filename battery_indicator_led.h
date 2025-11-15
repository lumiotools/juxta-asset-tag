#ifndef BATTERY_INDICATOR_LED_H
#define BATTERY_INDICATOR_LED_H

#include <Adafruit_NeoPixel.h>
#include "battery_monitor.h"

class BatteryIndicatorLED {
private:
  static const int BATTERY_LED_PIN = 38;
  static const int NUM_LEDS = 1;
  Adafruit_NeoPixel* pixels;
  
public:
  // Constructor
  BatteryIndicatorLED() {
    pixels = new Adafruit_NeoPixel(NUM_LEDS, BATTERY_LED_PIN, NEO_GRB + NEO_KHZ800);
  }

  // Initialize the battery indicator LED
  void begin() {
    pixels->begin();
    pixels->setBrightness(200); // Slightly dimmer than status LED to differentiate
    Serial.println("Battery Indicator LED initialized on GPIO " + String(BATTERY_LED_PIN));
  }

  // Update LED color based on battery percentage
  void updateBatteryLED() {
    uint32_t color = BatteryMonitor::getBatteryColor();
    pixels->setPixelColor(0, color);
    pixels->show();
  }

  // Set custom color (for testing or special states)
  void setColor(uint8_t red, uint8_t green, uint8_t blue) {
    uint32_t color = pixels->Color(red, green, blue);
    pixels->setPixelColor(0, color);
    pixels->show();
  }

  // Pulse LED (battery low warning)
  void pulseLED(int delayMs = 500) {
    // Fade in
    for (int i = 0; i <= 200; i += 10) {
      pixels->setBrightness(i);
      pixels->show();
      delay(30);
    }
    // Fade out
    for (int i = 200; i >= 0; i -= 10) {
      pixels->setBrightness(i);
      pixels->show();
      delay(30);
    }
  }

  // Double blink (critical battery)
  void doubleBlink() {
    for (int i = 0; i < 2; i++) {
      setColor(255, 0, 0); // Red
      delay(100);
      setColor(0, 0, 0); // Off
      delay(100);
    }
    pixels->setBrightness(200);
  }

  // Turn off LED
  void turnOff() {
    pixels->setPixelColor(0, pixels->Color(0, 0, 0));
    pixels->show();
  }

  // Get current battery percentage (for display)
  int getBatteryPercentage() {
    return BatteryMonitor::getBatteryPercentage();
  }

  // Get current battery voltage (for display)
  float getBatteryVoltage() {
    return BatteryMonitor::readBatteryVoltage();
  }

  // Print LED status
  void printStatus() {
    Serial.println("Battery Indicator LED Status:");
    Serial.println("  Percentage: " + String(getBatteryPercentage()) + "%");
    Serial.println("  Voltage: " + String(getBatteryVoltage(), 2) + "V");
    Serial.println("  Color Code: " + getColorName());
  }

  // Get color name for current battery level
  String getColorName() {
    int percentage = getBatteryPercentage();
    if (percentage >= 75) {
      return "GREEN (Excellent)";
    } else if (percentage >= 25) {
      return "YELLOW (Low)";
    } else {
      return "RED (Critical)";
    }
  }

  // Destructor
  ~BatteryIndicatorLED() {
    if (pixels) {
      delete pixels;
    }
  }
};

#endif
