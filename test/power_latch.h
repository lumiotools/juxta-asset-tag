// Power Latch Control Header
// Shared header for BMI323 test files
// Controls GPIO 4 (power latch pin) for power management
// Handles button long press (5s) for power off

#ifndef POWER_LATCH_H
#define POWER_LATCH_H

#include <Arduino.h>
#include "driver/gpio.h"

// Pin definitions
#define POWER_LATCH_PIN 4
#define BUTTON_PIN 10

// Long press timing
#define LONG_PRESS_TIME_MS 5000  // 5 seconds for long press

// Button state tracking
static bool buttonPressed = false;
static bool buttonReleased = true;
static unsigned long buttonPressStartTime = 0;
static bool longPressTriggered = false;

// Set power latch pin HIGH (power on) or LOW (power off)
// HIGH: Configure as OUTPUT with pull-up, set HIGH
// LOW: Configure as OUTPUT with pull-down, set LOW
void setPowerLatchPin(bool high) {
  if (high) {
    // Set HIGH: Configure as OUTPUT with pull-up
    pinMode(POWER_LATCH_PIN, OUTPUT);
    gpio_set_pull_mode(GPIO_NUM_4, GPIO_PULLUP_ONLY);
    digitalWrite(POWER_LATCH_PIN, HIGH);
    Serial.println("Power latch pin (IO4) set HIGH with pull-up");
  } else {
    // Set LOW: Configure as OUTPUT with pull-down
    pinMode(POWER_LATCH_PIN, OUTPUT);
    gpio_set_pull_mode(GPIO_NUM_4, GPIO_PULLDOWN_ONLY);
    digitalWrite(POWER_LATCH_PIN, LOW);
    Serial.println("Power latch pin (IO4) set LOW with pull-down");
  }
}

// Power off with 5 second delay
// Waits 5 seconds before turning off power latch
void powerOff() {
  Serial.println("\n========== POWERING OFF ==========");
  Serial.println("Waiting 5 seconds before power off...");
  // delay(5000);
  Serial.println("Turning off power latch...");
  setPowerLatchPin(false);
  Serial.println("Power latch turned off");
  Serial.println("========================================\n");
}

// Initialize power latch and button
// Call this in setup()
void initPowerLatch() {
  // Set power latch HIGH on boot
  setPowerLatchPin(true);
  
  // Initialize button pin
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Reset button state
  buttonPressed = false;
  buttonReleased = true;
  buttonPressStartTime = 0;
  longPressTriggered = false;
  
  Serial.println("Power latch initialized - power on");
  Serial.print("Button pin: ");
  Serial.println(BUTTON_PIN);
}

// Update button handler - call this in loop()
// Detects long press (5 seconds) and powers off
void updateButtonHandler() {
  bool currentButtonState = (digitalRead(BUTTON_PIN) == LOW); // LOW when pressed (pull-up)
  
  if (currentButtonState) {
    // Button is pressed
    if (buttonReleased) {
      // Button was just pressed (transition from released to pressed)
      buttonReleased = false;
      buttonPressed = true;
      buttonPressStartTime = millis();
      longPressTriggered = false;
      Serial.println("Button pressed - hold for 5 seconds to power off");
    }
    
    // Check for long press (5 seconds)
    if (!longPressTriggered && buttonPressed && (millis() - buttonPressStartTime >= LONG_PRESS_TIME_MS)) {
      // Long press detected (5 seconds) - turn off immediately
      longPressTriggered = true;
      Serial.println("\n========== LONG PRESS DETECTED (5s) ==========");
      Serial.println("Powering off device immediately...");
      setPowerLatchPin(false); // Turn off power latch immediately
      Serial.println("Power latch turned off");
      Serial.println("========================================\n");
    }
  } else {
    // Button is released
    if (buttonPressed) {
      // Button was just released (transition from pressed to released)
      buttonPressed = false;
      buttonReleased = true;
      buttonPressStartTime = 0;
      
      if (!longPressTriggered) {
        Serial.println("Button released - short press (no action)");
      }
      
      // Reset long press flag after release
      longPressTriggered = false;
    }
  }
}

#endif // POWER_LATCH_H

