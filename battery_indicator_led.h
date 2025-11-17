#ifndef BATTERY_INDICATOR_LED_H
#define BATTERY_INDICATOR_LED_H

#include <Adafruit_NeoPixel.h>
#include "battery_monitor.h"

class BatteryIndicatorLED {
private:
  static const int BATTERY_LED_PIN = 38;
  static const int NUM_PIXELS = 1;
  Adafruit_NeoPixel led;
  
public:
  BatteryIndicatorLED() : led(NUM_PIXELS, BATTERY_LED_PIN, NEO_GRB + NEO_KHZ800) {
    // Don't initialize in constructor - causes boot crash
  }

  void begin() {
    led.begin();  // Initialize GPIO here, after boot
    led.setBrightness(200);
    led.show();
  }

  void updateBatteryLED() {
    uint32_t color = BatteryMonitor::getBatteryColor();
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    led.setPixelColor(0, led.Color(r, g, b));
    led.show();
  }

  void setColor(uint8_t red, uint8_t green, uint8_t blue) {
    led.setPixelColor(0, led.Color(red, green, blue));
    led.show();
  }

  void doubleBlink() {
    for (int i = 0; i < 2; i++) {
      setColor(255, 0, 0);
      delay(100);
      setColor(0, 0, 0);
      delay(100);
    }
  }
};

#endif
