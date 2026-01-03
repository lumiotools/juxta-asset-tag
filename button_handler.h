// Button Handler - Handles button press events with long press and double press detection
// Uses PKAE_Timer library for timing

#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <Arduino.h>
#include <PKAE_Timer.h>
#include <esp_system.h>

// Pin definitions
#define BUTTON_PIN 10
#define DEVICE_POWER_PIN 4

// Timing constants
#define LONG_PRESS_TIME_MS 5000  // 5 seconds for long press
#define DOUBLE_PRESS_WINDOW_MS 500  // 500ms window for double press detection

class ButtonHandler {
private:
  static bool buttonPressed;
  static bool buttonReleased;
  static unsigned long lastPressTime;
  static int pressCount;
  static PKAE_Timer longPressTimer;
  static PKAE_Timer doublePressTimer;
  static bool longPressTriggered;
  static bool initialized;

public:
  // Initialize button handler
  static void begin() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(DEVICE_POWER_PIN, OUTPUT);
    digitalWrite(DEVICE_POWER_PIN, HIGH); // Keep power on initially
    
    buttonPressed = false;
    buttonReleased = true;
    lastPressTime = 0;
    pressCount = 0;
    longPressTriggered = false;
    initialized = true;
    
    // Initialize timers
    longPressTimer = PKAE_Timer(LONG_PRESS_TIME_MS);
    doublePressTimer = PKAE_Timer(DOUBLE_PRESS_WINDOW_MS);
    
    Serial.println("Button handler initialized");
    Serial.print("Button pin: ");
    Serial.println(BUTTON_PIN);
    Serial.print("Device power pin: ");
    Serial.println(DEVICE_POWER_PIN);
  }
  
  // Update button handler (call this in loop)
  static void update() {
    if (!initialized) {
      return;
    }
    
    bool currentButtonState = (digitalRead(BUTTON_PIN) == LOW); // LOW when pressed (pull-up)
    
    if (currentButtonState) {
      // Button is pressed
      if (buttonReleased) {
        // Button was just pressed (transition from released to pressed)
        buttonReleased = false;
        buttonPressed = true;
        
        // Check for double press
        unsigned long currentTime = millis();
        if (currentTime - lastPressTime < DOUBLE_PRESS_WINDOW_MS && lastPressTime > 0) {
          // This is a double press
          pressCount = 2;
          Serial.println("Double press detected - restarting ESP...");
          delay(100); // Brief delay before restart
          ESP.restart();
          return; // Will not reach here
        } else {
          // First press or too much time has passed
          pressCount = 1;
          lastPressTime = currentTime;
          longPressTimer.Reset(); // Start long press timer
          longPressTriggered = false;
        }
      }
      
      // Check for long press
      if (!longPressTriggered && longPressTimer.IsTimeUp()) {
        // Long press detected (5 seconds)
        longPressTriggered = true;
        Serial.println("Long press detected (5s) - powering off device...");
        digitalWrite(DEVICE_POWER_PIN, LOW); // Set power pin LOW to turn off device
        Serial.println("Device power pin set to LOW");
      }
    } else {
      // Button is released
      if (buttonPressed) {
        // Button was just released (transition from pressed to released)
        buttonPressed = false;
        buttonReleased = true;
        
        // Reset long press timer
        longPressTimer.Reset();
        
        // If long press was not triggered, this was a short press
        if (!longPressTriggered && pressCount == 1) {
          // Single short press - do nothing or handle if needed
          // Could be used for other functions in the future
        }
        
        // Reset long press flag after release
        longPressTriggered = false;
      }
    }
  }
  
  // Check if button is currently pressed
  static bool isPressed() {
    return buttonPressed;
  }
  
  // Get current press count (for debugging)
  static int getPressCount() {
    return pressCount;
  }
};

// Static member initialization
bool ButtonHandler::buttonPressed = false;
bool ButtonHandler::buttonReleased = true;
unsigned long ButtonHandler::lastPressTime = 0;
int ButtonHandler::pressCount = 0;
PKAE_Timer ButtonHandler::longPressTimer(LONG_PRESS_TIME_MS);
PKAE_Timer ButtonHandler::doublePressTimer(DOUBLE_PRESS_WINDOW_MS);
bool ButtonHandler::longPressTriggered = false;
bool ButtonHandler::initialized = false;

#endif

