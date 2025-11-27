#ifndef BATTERY_INDICATOR_LED_H
#define BATTERY_INDICATOR_LED_H

#include <Adafruit_NeoPixel.h>
#include "battery_monitor.h"

class BatteryIndicatorLED {
private:
  Adafruit_NeoPixel* led;  // Pointer to shared NeoPixel instance
  int pixelIndex;          // Which pixel to control (pixel 1 for battery)
  bool enabled = true;     // Track if LED is enabled
  
  // Helper function to set pixel color and display
  void setPixelAndShow(uint8_t r, uint8_t g, uint8_t b) {
    if (!led) return;
    led->setPixelColor(pixelIndex, led->Color(r, g, b));
    led->show();
  }
  
public:
  BatteryIndicatorLED() : led(nullptr), pixelIndex(1) {
    // Don't initialize in constructor - causes boot crash
  }

  void begin(Adafruit_NeoPixel* sharedLED, int pixelIdx = 1) {
    led = sharedLED;       // Use shared NeoPixel instance
    pixelIndex = pixelIdx; // Set which pixel to control (default is pixel 1)
    enabled = true;
  }

  void updateBatteryLED() {
    if (!enabled || !led) return;  // Don't update if disabled or not initialized
    uint32_t color = BatteryMonitor::getBatteryColor();
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    setPixelAndShow(r, g, b);
  }

  void setColor(uint8_t red, uint8_t green, uint8_t blue) {
    if (!enabled || !led) return;  // Don't set color if disabled or not initialized
    setPixelAndShow(red, green, blue);
  }

  void enable() {
    enabled = true;
    updateBatteryLED();  // Restore battery LED color
  }

  void disable() {
    if (!led) return;
    enabled = false;
    setPixelAndShow(0, 0, 0);  // Turn off LED
  }

  bool isEnabled() {
    return enabled;
  }

  void doubleBlink() {
    if (!enabled) return;  // Don't blink if disabled
    for (int i = 0; i < 2; i++) {
      setColor(255, 0, 0);
      delay(100);
      setColor(0, 0, 0);
      delay(100);
    }
  }

  void dimTo10Percent() {
    // Brightness is controlled by the shared NeoPixel instance
    // Since the brightness is shared across all pixels on the strip,
    // the main code should handle dimming the entire strip to 10%
    if (enabled && led) {
      updateBatteryLED();  // Just update the color, brightness is shared
    }
  }
};

#endif
