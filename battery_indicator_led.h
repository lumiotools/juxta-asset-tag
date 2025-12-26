// Battery Indicator LED - Simple single-color LED that indicates battery level
// Two thresholds: 20% (LED on constantly) and 10% (fast blink for critical)

#ifndef BATTERY_INDICATOR_LED_H
#define BATTERY_INDICATOR_LED_H

#include <Arduino.h>
#include "battery_monitor.h"

// Pin definition for battery indicator LED (single-color GPIO)
#define BATTERY_LED_PIN D12  // Change this to your desired GPIO pin

// Battery thresholds
#define BATTERY_THRESHOLD_1_PERCENT 20  // First threshold: LED on constantly
#define BATTERY_THRESHOLD_2_PERCENT 10  // Second threshold: fast blinking (critical)

// Blink timing constants (only used for critical state)
#define BLINK_ON_TIME_CRITICAL_MS 500  // LED on time for critical blink
#define BLINK_OFF_TIME_CRITICAL_MS 500 // LED off time for critical blink

// Battery state enum
enum BatteryBlinkState {
  BATTERY_OFF,      // Above 20% - LED off
  BATTERY_NORMAL,  // Below 20% but >= 10% - LED on constantly
  BATTERY_CRITICAL // Below 10% - fast blink
};

class BatteryIndicatorLED {
private:
  int ledPin;                    // GPIO pin for LED
  bool enabled;                  // Track if LED is enabled
  BatteryBlinkState blinkState; // Current blinking state
  unsigned long blinkStartTime; // Time when blinking cycle started
  
  void ledOn() {
    if (enabled && ledPin > 0) analogWrite(ledPin, 128); // 50% brightness (128/255)
  }
  
  void ledOff() {
    if (ledPin > 0) analogWrite(ledPin, 0); // 0% brightness
  }
  
  void updateBlink() {
    if (!enabled || blinkState == BATTERY_OFF) {
      ledOff();
      return;
    }
    
    if (blinkState == BATTERY_NORMAL) {
      ledOn();
      return;
    }
    
    // BATTERY_CRITICAL - fast blinking
    unsigned long currentTime = millis();
    unsigned long elapsed = currentTime - blinkStartTime;
    unsigned long cycleTime = BLINK_ON_TIME_CRITICAL_MS + BLINK_OFF_TIME_CRITICAL_MS;
    unsigned long cyclePosition = elapsed % cycleTime;
    
    if (cyclePosition < BLINK_ON_TIME_CRITICAL_MS) {
      ledOn();
    } else {
      ledOff();
    }
    
    if (elapsed > 60000) blinkStartTime = currentTime; // Reset every minute
  }
  
public:
  BatteryIndicatorLED() : ledPin(BATTERY_LED_PIN), enabled(true), 
                          blinkState(BATTERY_OFF), blinkStartTime(0) {
  }

  void begin(int pin = BATTERY_LED_PIN) {
    ledPin = pin;
    enabled = true;
    blinkState = BATTERY_OFF;
    blinkStartTime = millis();
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW);
    
    Serial.print("Battery LED initialized on pin ");
    Serial.print(ledPin);
    Serial.print(" (thresholds: ");
    Serial.print(BATTERY_THRESHOLD_1_PERCENT);
    Serial.print("%/");
    Serial.print(BATTERY_THRESHOLD_2_PERCENT);
    Serial.println("%)");
  }

  void updateBatteryLED() {
    if (!enabled || ledPin <= 0) return;
    
    int currentBatteryPercent = BatteryMonitor::getBatteryPercentage();
    BatteryBlinkState newState;
    
    if (currentBatteryPercent >= BATTERY_THRESHOLD_1_PERCENT) {
      newState = BATTERY_OFF;
    } else if (currentBatteryPercent >= BATTERY_THRESHOLD_2_PERCENT) {
      newState = BATTERY_NORMAL;
    } else {
      newState = BATTERY_CRITICAL;
    }
    
    if (newState != blinkState) {
      blinkState = newState;
      blinkStartTime = millis();
      
      if (blinkState == BATTERY_OFF) {
        ledOff();
      } else if (blinkState == BATTERY_NORMAL) {
        ledOn();
      }
    }
  }

  void enable() {
    enabled = true;
  }

  void disable() {
    enabled = false;
    ledOff();
  }

  bool isEnabled() {
    return enabled;
  }

  void doubleBlink() {
    if (!enabled) return;
    blinkState = BATTERY_CRITICAL;
    blinkStartTime = millis();
  }

  
};

#endif
