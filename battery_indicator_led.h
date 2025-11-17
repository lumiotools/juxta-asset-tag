#ifndef BATTERY_INDICATOR_LED_H
#define BATTERY_INDICATOR_LED_H

#include "ws2812b_simple.h"
#include "battery_monitor.h"

class BatteryIndicatorLED {
private:
  static const int BATTERY_LED_PIN = 38;
  WS2812B led;
  
public:
  BatteryIndicatorLED() : led(BATTERY_LED_PIN) {
    led.setBrightness(200);
  }

  void begin() {
    // Already initialized in constructor
  }

  void updateBatteryLED() {
    uint32_t color = BatteryMonitor::getBatteryColor();
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    led.setPixelColor(0, r, g, b);
  }

  void setColor(uint8_t red, uint8_t green, uint8_t blue) {
    led.setPixelColor(0, red, green, blue);
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
