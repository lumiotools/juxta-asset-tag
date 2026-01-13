// Power Latch and LED Test
// Tests power latch functionality and sensor initialization with LED status indication
// 
// Hardware:
// - Power Latch → GPIO 4 (POWER_LATCH_PIN)
// - Status LEDs → GPIO 11 (2 NeoPixel LEDs)
// - Button → GPIO 10 (for power off via long press)
//
// Test Procedure:
// 1. Device turns on with power latch
// 2. Initialize all sensors (IMU, GPS, SPI Flash, Unified CSV Storage)
// 3. LED color indicates initialization status:
//    - GREEN: All sensors initialized successfully
//    - RED: Any sensor initialization failed
// 4. Serial monitor shows initialization status
// 5. Long press button (5s) to power off

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "power_latch.h"
#include "imu_sensor.h"
#include "gps_sensor.h"
#include "spi_flash_handler.h"
#include "unified_csv_storage.h"

// LED configuration
#define STATUS_LED_PIN 11
#define STATUS_LED_COUNT 2  // 2 pixels: pixel 0 and pixel 1

// Create NeoPixel instance
Adafruit_NeoPixel statusLED(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

// Create sensor instances
IMUSensor imuSensor;
GPSSensor gpsSensor;
SPIFlashHandler spiFlash;
UnifiedCSVStorage unifiedCSVStorage;

// Sensor initialization status flags
bool imuInitialized = false;
bool gpsInitialized = false;
bool flashInitialized = false;
bool csvStorageInitialized = false;

// Helper function to set pixel color and display
void setPixelAndShow(uint8_t pixel, uint8_t r, uint8_t g, uint8_t b) {
  statusLED.setPixelColor(pixel, statusLED.Color(r, g, b));
  statusLED.show();
}

// Set LED color based on sensor initialization status
// GREEN if all sensors initialized, RED if any failed
void updateStatusLED() {
  if (imuInitialized && gpsInitialized && flashInitialized && csvStorageInitialized) {
    // All sensors initialized successfully - GREEN
    setPixelAndShow(0, 0, 255, 0); // Green on pixel 0
    setPixelAndShow(1, 0, 255, 0); // Green on pixel 1
    Serial.println("All sensors initialized - LEDs set to GREEN");
  } else {
    // At least one sensor failed - RED
    setPixelAndShow(0, 255, 0, 0); // Red on pixel 0
    setPixelAndShow(1, 255, 0, 0); // Red on pixel 1
    Serial.println("Sensor initialization failed - LEDs set to RED");
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n========================================");
  Serial.println("Power Latch & Sensor Initialization Test");
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
  
  // Initialize External SPI Flash
  Serial.println("\nInitializing SPI Flash...");
  flashInitialized = spiFlash.begin();
  if (flashInitialized) {
    Serial.println("SPI Flash initialized successfully");
    Serial.print("Flash Capacity: ");
    Serial.print(spiFlash.getCapacity());
    Serial.println(" bytes");
  } else {
    Serial.println("ERROR: SPI Flash initialization failed!");
  }
  
  // Initialize Unified CSV Storage (uses entire external flash)
  Serial.println("\nInitializing Unified CSV Storage...");
  if (flashInitialized) {
    csvStorageInitialized = unifiedCSVStorage.begin(&spiFlash);
    if (csvStorageInitialized) {
      Serial.println("Unified CSV Storage initialized successfully");
    } else {
      Serial.println("ERROR: Unified CSV Storage initialization failed!");
    }
  } else {
    Serial.println("Skipping CSV Storage initialization (Flash not initialized)");
  }
  
  // Initialize IMU sensor
  Serial.println("\nInitializing IMU sensor...");
  imuInitialized = imuSensor.begin();
  if (imuInitialized) {
    Serial.println("IMU initialized successfully");
  } else {
    Serial.println("ERROR: IMU initialization failed!");
  }
  
  // Initialize GPS sensor
  Serial.println("\nInitializing GPS sensor...");
  gpsInitialized = gpsSensor.begin();
  if (gpsInitialized) {
    Serial.println("GPS initialized successfully");
  } else {
    Serial.println("ERROR: GPS initialization failed!");
  }
  
  // Update LED color based on sensor initialization status
  Serial.println("\n========================================");
  Serial.println("Sensor Initialization Summary");
  Serial.println("========================================");
  Serial.print("  IMU: ");
  Serial.println(imuInitialized ? "OK" : "FAILED");
  Serial.print("  GPS: ");
  Serial.println(gpsInitialized ? "OK" : "FAILED");
  Serial.print("  SPI Flash: ");
  Serial.println(flashInitialized ? "OK" : "FAILED");
  Serial.print("  CSV Storage: ");
  Serial.println(csvStorageInitialized ? "OK" : "FAILED");
  Serial.println("========================================");
  
  // Set LED color: GREEN if all OK, RED if any failed
  updateStatusLED();
  
  Serial.println("\n========================================");
  Serial.println("Test Complete!");
  Serial.println("========================================");
  Serial.println("Status:");
  Serial.println("  - Power latch: ON (GPIO 4 HIGH)");
  if (imuInitialized && gpsInitialized && flashInitialized && csvStorageInitialized) {
    Serial.println("  - LED Pixel 0: GREEN (All sensors OK)");
    Serial.println("  - LED Pixel 1: GREEN (All sensors OK)");
  } else {
    Serial.println("  - LED Pixel 0: RED (Sensor initialization failed)");
    Serial.println("  - LED Pixel 1: RED (Sensor initialization failed)");
  }
  Serial.println("\nPress and hold button for 5 seconds to power off");
  Serial.println("========================================\n");
}

void loop() {
  // Update button handler to detect long press (5s) for power off
  updateButtonHandler();
  
  // Small delay to prevent CPU overload
  delay(10);
}

