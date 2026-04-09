// Battery Indicator LED - RGB NeoPixel LED that indicates charging state
// Uses pixel 1 of shared NeoPixel strip (pixel 0 is for device status)
// Behavior:
//   - When charger is connected: solid green on
//   - When charger is disconnected: use low-battery red indicator only

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
enum BatteryLEDState {
  BATTERY_OFF,
  BATTERY_RED,
  BATTERY_RED_BLINK,
  BATTERY_GREEN
};

class BatteryIndicatorLED {
private:
  CRGB* neopixel;  // Pointer to shared NeoPixel strip
  uint8_t pixelIndex;            // Pixel index (1 for battery, 0 is device status)
  bool enabled;                  // Track if LED is enabled
  BatteryLEDState blinkState;    // Current battery LED state
  unsigned long blinkStartTime;  // Time when blinking cycle started
  bool blinkOnState;             // Current on/off state during blinking
  CRGB currentColor;             // Current solid/blink color
  
  void setPixelColor(CRGB color) {
    if (neopixel && enabled) {
      neopixel[pixelIndex] = color;
      FastLED.show();
    }
  }
  
  void ledOn() {
    if (!enabled || !neopixel) return;
    setPixelColor(currentColor);
  }
  
  void ledOff() {
    if (!enabled || !neopixel) return;
    neopixel[pixelIndex] = CRGB::Black;
    FastLED.show();
  }
  
  bool isBlinkingState() const {
    return blinkState == BATTERY_RED_BLINK;
  }
  
  void updateBlink() {
    if (!enabled || !neopixel || !isBlinkingState()) {
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
                          blinkState(BATTERY_OFF), blinkStartTime(0), blinkOnState(false), currentColor(CRGB::Black) {
  }

  void begin(CRGB* np, uint8_t pixel = 1) {
    neopixel = np;
    pixelIndex = pixel;
    enabled = true;
    blinkState = BATTERY_OFF;
    blinkStartTime = millis();
    blinkOnState = false;
    currentColor = CRGB::Black;
    
    // Initialize pixel to off
    if (neopixel) {
      neopixel[pixelIndex] = CRGB::Black;
      FastLED.show();
    }
    
    Serial.print("Battery LED initialized on pixel ");
    Serial.print(pixelIndex);
    Serial.print(" (charger connected = solid green, otherwise battery low red)");
    Serial.println();
  }

  void updateBatteryLED() {
    if (!enabled || !neopixel) return;
    
    float currentBatteryVoltage = BatteryMonitor::readBatteryVoltage();
    int currentBatteryPercent = BatteryMonitor::getBatteryPercentageV(currentBatteryVoltage);
    bool charging = BatteryMonitor::isCharging();
    BatteryLEDState newState;
    
    if (charging) {
      newState = BATTERY_GREEN;
    } else {
      if (currentBatteryPercent >= BATTERY_THRESHOLD_PERCENT) {
        newState = BATTERY_OFF;
      } else if (currentBatteryPercent >= BATTERY_CRITICAL_PERCENT) {
        newState = BATTERY_RED;
      } else {
        newState = BATTERY_RED_BLINK;
      }
    }
    
    // Update current color for solid/blink states
    switch (newState) {
      case BATTERY_OFF:
        currentColor = CRGB::Black;
        break;
      case BATTERY_RED:
      case BATTERY_RED_BLINK:
        currentColor = CRGB::Red;
        break;
      case BATTERY_GREEN:
        currentColor = CRGB::Green;
        break;
    }
    
    // State changed - update LED immediately
    if (newState != blinkState) {
      blinkState = newState;
      blinkStartTime = millis();
      blinkOnState = false;
      
      if (blinkState == BATTERY_OFF) {
        ledOff();
      } else if (isBlinkingState()) {
        // Start blinking - turn on first
        ledOn();
        blinkOnState = true;
      } else {
        ledOn();
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
    blinkState = BATTERY_RED_BLINK;
    currentColor = CRGB::Red;
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

