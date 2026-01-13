// Power Latch and LED Test
// Tests power latch functionality and sets both LEDs to red at 50% brightness
// 
// Hardware:
// - Power Latch → GPIO 4 (POWER_LATCH_PIN)
// - Status LEDs → GPIO 22 (2 NeoPixel LEDs)
// - Button → GPIO 10 (for power off via long press)
//
// Test Procedure:
// 1. Device turns on with power latch
// 2. Both LEDs (pixel 0 and pixel 1) turn red at 50% brightness
// 3. Serial monitor shows initialization status
// 4. Long press button (5s) to power off

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "../power_latch.h"

// LED configuration
#define STATUS_LED_PIN 11
#define STATUS_LED_COUNT 2  // 2 pixels: pixel 0 and pixel 1

// Create NeoPixel instance
Adafruit_NeoPixel statusLED(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

// Helper function to set pixel color and display
void setPixelAndShow(uint8_t pixel, uint8_t r, uint8_t g, uint8_t b) {
  statusLED.setPixelColor(pixel, statusLED.Color(r, g, b));
  statusLED.show();
}

// Set both LEDs to red at 50% brightness
void setBothLedsRed50Percent() {
  // Red at 50% brightness: R=128, G=0, B=0
  // (50% of 255 = 127.5, rounded to 128)
  statusLED.setPixelColor(0, statusLED.Color(128, 0, 0));
  statusLED.setPixelColor(1, statusLED.Color(128, 0, 0));
  statusLED.show();
  
  Serial.println("Both LEDs set to red at 50% brightness");
  Serial.println("  Pixel 0: RED (128, 0, 0)");
  Serial.println("  Pixel 1: RED (128, 0, 0)");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n========================================");
  Serial.println("Power Latch & LED Test");
  Serial.println("========================================\n");
  
  // Initialize power latch (set HIGH to keep device powered on)
  Serial.println("Initializing power latch...");
  delay(5000);
  initPowerLatch();
  Serial.println("Power latch initialized - device powered on");
  
  // Initialize NeoPixel LEDs
  Serial.println("\nInitializing NeoPixel LEDs...");
  statusLED.begin();
  statusLED.setBrightness(255);  // Full brightness (we control brightness via color values)
  statusLED.show();  // Initialize all pixels to 'off'
  Serial.println("NeoPixel LEDs initialized");
  
  // Test each LED individually
  Serial.println("\n--- Testing LEDs Individually ---");
  
  // Test Pixel 0
  Serial.println("Testing Pixel 0 (RED at 50%)...");
  statusLED.clear();
  statusLED.setPixelColor(0, statusLED.Color(128, 0, 0));
  statusLED.show();
  delay(2000);
  
  // Test Pixel 1
  Serial.println("Testing Pixel 1 (GREEN at 50%)...");
  statusLED.clear();
  statusLED.setPixelColor(1, statusLED.Color(0, 128, 0));
  statusLED.show();
  delay(2000);
  
  // Test both together
  Serial.println("Testing both pixels (Pixel 0=RED, Pixel 1=BLUE)...");
  statusLED.clear();
  statusLED.setPixelColor(0, statusLED.Color(128, 0, 0));  // Red
  statusLED.setPixelColor(1, statusLED.Color(0, 0, 128));  // Blue
  statusLED.show();
  delay(2000);
  
  // Set both LEDs to red at 50% brightness
  Serial.println("\nSetting both LEDs to red at 50% brightness...");
  setBothLedsRed50Percent();
  
  Serial.println("\n========================================");
  Serial.println("Test Complete!");
  Serial.println("========================================");
  Serial.println("Status:");
  Serial.println("  - Power latch: ON (GPIO 4 HIGH)");
  Serial.println("  - LED Pixel 0: RED at 50%");
  Serial.println("  - LED Pixel 1: RED at 50%");
  Serial.println("\nPress and hold button for 5 seconds to power off");
  Serial.println("========================================\n");
}

void loop() {
  // Update button handler to detect long press (5s) for power off
  updateButtonHandler();
  
  // Small delay to prevent CPU overload
  delay(10);
}
