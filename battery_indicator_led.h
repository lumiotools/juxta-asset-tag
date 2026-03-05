// Battery Indicator LED - RGB NeoPixel LED that indicates battery level
// Uses pixel 1 of shared NeoPixel strip (pixel 0 is for device status)
// Behavior: 
//   - >= 20%: OFF
//   - 10-20%: RED (solid on)
//   - < 10%: RED (blinking at 500ms intervals, asynchronous)

#ifndef BATTERY_INDICATOR_LED_H
#define BATTERY_INDICATOR_LED_H

#include <Arduino.h>
#include "battery_monitor.h"
#include <FastLED.h>

// Battery thresholds
#define BATTERY_THRESHOLD_PERCENT 20  // LED on (red) when below this percentage
#define BATTERY_CRITICAL_PERCENT 10   // LED blinks when below this percentage

// Blink timing constants
#define BLINK_INTERVAL_MS 500  // 500ms on/off interval for blinking

// Battery state enum
enum BatteryBlinkState {
  BATTERY_OFF,      // >= 20% - LED off
  BATTERY_ON,       // 10-20% - LED on (red, solid)
  BATTERY_BLINKING  // < 10% - LED blinking (red, 500ms)
};

class BatteryIndicatorLED {
private:
  CRGB* neopixel;  // Pointer to shared NeoPixel strip
  uint8_t pixelIndex;            // Pixel index (1 for battery, 0 is device status)
  bool enabled;                  // Track if LED is enabled
  BatteryBlinkState blinkState;  // Current blinking state
  unsigned long blinkStartTime;  // Time when blinking cycle started
  bool blinkOnState;             // Current on/off state during blinking
  
  void setPixelColor(CRGB color) {
    if (neopixel && enabled) {
      neopixel[pixelIndex] = color;
      FastLED.show();
    }
  }
  
  void ledOn() {
    if (!enabled || !neopixel) return;
    setPixelColor(CRGB::Red); // Red
  }
  
  void ledOff() {
    if (neopixel) {
      neopixel[pixelIndex] = CRGB::Black;
      FastLED.show();
    }
  }
  
  void updateBlink() {
    if (!enabled || !neopixel || blinkState != BATTERY_BLINKING) {
      return;
    }
    
    // Non-blocking blink: toggle every 500ms
    unsigned long currentTime = millis();
    unsigned long elapsed = currentTime - blinkStartTime;
    unsigned long cyclePosition = elapsed % (BLINK_INTERVAL_MS * 2); // Full cycle = 1000ms (500ms on + 500ms off)
    
    bool shouldBeOn = (cyclePosition < BLINK_INTERVAL_MS);
    
    if (shouldBeOn != blinkOnState) {
      blinkOnState = shouldBeOn;
      if (blinkOnState) {
        ledOn();
      } else {
        ledOff();
      }
    }
    
    // Reset timer every minute to prevent overflow
    if (elapsed > 60000) {
      blinkStartTime = currentTime;
    }
  }
  
public:
  BatteryIndicatorLED() : neopixel(nullptr), pixelIndex(1), enabled(true), 
                          blinkState(BATTERY_OFF), blinkStartTime(0), blinkOnState(false) {
  }

  void begin(CRGB* np, uint8_t pixel = 1) {
    neopixel = np;
    pixelIndex = pixel;
    enabled = true;
    blinkState = BATTERY_OFF;
    blinkStartTime = millis();
    blinkOnState = false;
    
    // Initialize pixel to off
    if (neopixel) {
      neopixel[pixelIndex] = CRGB::Black;
      FastLED.show();
    }
    
    Serial.print("Battery LED initialized on pixel ");
    Serial.print(pixelIndex);
    Serial.print(" (thresholds: >= 20% = OFF, 10-20% = RED, < 10% = RED blinking)");
    Serial.println();
  }

  void updateBatteryLED() {
    if (!enabled || !neopixel) return;
    
    float currentBatteryVoltage = BatteryMonitor::readBatteryVoltage();
    int currentBatteryPercent = BatteryMonitor::getBatteryPercentageV(currentBatteryVoltage);
    BatteryBlinkState newState;
    
    if (currentBatteryPercent >= BATTERY_THRESHOLD_PERCENT) {
      newState = BATTERY_OFF;
    } else if (currentBatteryPercent >= BATTERY_CRITICAL_PERCENT) {
      newState = BATTERY_ON;
    } else {
      newState = BATTERY_BLINKING;
    }
    
    // State changed - update LED immediately
    if (newState != blinkState) {
      blinkState = newState;
      blinkStartTime = millis();
      blinkOnState = false;
      
      if (blinkState == BATTERY_OFF) {
        ledOff();
      } else if (blinkState == BATTERY_ON) {
        ledOn();
      } else if (blinkState == BATTERY_BLINKING) {
        // Start blinking - turn on first
        ledOn();
        blinkOnState = true;
      }
    }
  }

  // Turn off LED
  void turnOff() {
    ledOff();
    blinkState = BATTERY_OFF;
  }

  void enable() {
    enabled = true;
    // Update LED state based on current battery level
    updateBatteryLED();
  }

  void disable() {
    enabled = false;
    ledOff();
  }

  bool isEnabled() {
    return enabled;
  }

  void doubleBlink() {
    // Optional: Can be used for critical alerts
    if (!enabled) return;
    blinkState = BATTERY_BLINKING;
    blinkStartTime = millis();
    ledOn();
    blinkOnState = true;
  }

  // Update blinking state (call this in loop for non-blocking blink)
  void update() {
    // Update non-blocking battery reading
    BatteryMonitor::updateBatteryReading();
    
    // Only update LED state when new reading is available
    if (BatteryMonitor::isNewReadingAvailable()) {
      updateBatteryLED(); // Check battery level and update state
    }
    
    updateBlink(); // Handle asynchronous blinking
  }
  
  // Get current battery percentage (helper method)
  int getBatteryPercentage() {
    return BatteryMonitor::getBatteryPercentage();
  }
  
  // Get current battery voltage (helper method)
  float getBatteryVoltage() {
    return BatteryMonitor::readBatteryVoltage();
  }
  
};

#endif

