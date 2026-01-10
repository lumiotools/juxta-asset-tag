// GPS Sensor Test with Power Latch Control
// This test verifies the AT6558 GPS sensor initialization and data reading
// with power latch control for power management
// 
// Hardware Connections:
// - GPS TX → GPIO 17 (ESP32 RX - RXD0)
// - GPS RX → GPIO 16 (ESP32 TX - TXD0)
// - GPS Power → GPIO 19 (GPS_POWER_PIN)
// - Power Latch → GPIO 4 (POWER_LATCH_PIN)
// - Button → GPIO 10 (BUTTON_PIN, for power off)

#include <Arduino.h>
#include "../power_latch.h"
#include "../../gps_sensor.h"
#include "../../nvs_config.h"

// GPS sensor instance
GPSSensor gpsSensor;
bool gpsInitialized = false;

// Timing variables
unsigned long lastGPSUpdateTime = 0;
const unsigned long GPS_UPDATE_INTERVAL_MS = 1000; // Update GPS every 1 second

// Fix acquisition timeout
const unsigned long GPS_FIX_TIMEOUT_MS = 120000; // 2 minutes timeout for fix
unsigned long gpsStartTime = 0;
bool fixAcquired = false;

void setup() {
  Serial.begin(115200);
  // No delay on boot - start immediately
  
  // Initialize power latch (set HIGH on boot)
  initPowerLatch();
  
  Serial.println("\n========================================");
  Serial.println("GPS Sensor Test with Power Latch");
  Serial.println("========================================\n");
  
  // Initialize NVS (required for GPS sensor to load/save last known position)
  Serial.println("Initializing NVS...");
  if (NVSConfig::initializeNVS()) {
    Serial.println("NVS initialized successfully");
  } else {
    Serial.println("WARNING: NVS initialization failed - GPS hot start may not work");
  }
  
  // Initialize GPS sensor
  Serial.println("Initializing GPS sensor...");
  gpsStartTime = millis();
  gpsInitialized = gpsSensor.begin();
  
  if (gpsInitialized) {
    Serial.println("GPS initialized successfully");
    Serial.println("Waiting for GPS fix...");
    Serial.println("(This may take up to 2 minutes)");
  } else {
    Serial.println("GPS initialization failed - continuing without GPS");
  }
  
  Serial.println("\n========================================");
  Serial.println("Test Setup Complete!");
  Serial.println("Reading GPS data every 1 second...");
  Serial.println("Hold button for 5 seconds to power off");
  Serial.println("========================================\n");
}

void loop() {
  // Update button handler (check for long press to power off)
  updateButtonHandler();
  
  if (!gpsInitialized) {
    delay(1000);
    return;
  }
  
  // Update GPS data at regular intervals
  unsigned long currentTime = millis();
  if (currentTime - lastGPSUpdateTime >= GPS_UPDATE_INTERVAL_MS) {
    lastGPSUpdateTime = currentTime;
    
    // Update GPS sensor (reads NMEA sentences)
    gpsSensor.update();
    
    // Get GPS data
    GPSData gpsData = gpsSensor.getGPSData();
    
    // Print GPS status
    Serial.println("--- GPS Data ---");
    
    if (gpsData.hasValidFix) {
      if (!fixAcquired) {
        fixAcquired = true;
        unsigned long fixTime = currentTime - gpsStartTime;
        Serial.print("*** GPS FIX ACQUIRED! (Time: ");
        Serial.print(fixTime / 1000);
        Serial.println(" seconds) ***");
      }
      
      Serial.print("Status: VALID FIX (");
      if (gpsData.fixType == 2) {
        Serial.print("2D");
      } else if (gpsData.fixType == 3) {
        Serial.print("3D");
      } else {
        Serial.print("Type ");
        Serial.print(gpsData.fixType);
      }
      Serial.println(")");
      
      Serial.print("Latitude:  ");
      Serial.print(gpsData.latitude, 7);
      Serial.println("°");
      
      Serial.print("Longitude: ");
      Serial.print(gpsData.longitude, 7);
      Serial.println("°");
      
      Serial.print("Altitude:  ");
      Serial.print(gpsData.altitude, 2);
      Serial.println(" m");
      
      Serial.print("Speed:     ");
      Serial.print(gpsData.speed, 2);
      Serial.println(" m/s");
      
      Serial.print("Heading:   ");
      Serial.print(gpsData.heading, 1);
      Serial.println("°");
      
      Serial.print("Satellites: ");
      Serial.println(gpsData.satellites);
      
      Serial.print("HDOP:      ");
      Serial.println(gpsData.hdop, 2);
      
      if (gpsData.lastFixTimeMillis > 0) {
        Serial.print("Last Fix Time: ");
        Serial.print(gpsData.lastFixTimeMillis);
        Serial.println(" ms (since epoch or millis)");
      }
    } else {
      // No fix
      unsigned long elapsedTime = currentTime - gpsStartTime;
      Serial.print("Status: NO FIX (Elapsed: ");
      Serial.print(elapsedTime / 1000);
      Serial.print(" seconds");
      
      if (elapsedTime >= GPS_FIX_TIMEOUT_MS) {
        Serial.print(" - TIMEOUT");
      }
      Serial.println(")");
      
      // Show last known position if available
      if (gpsData.latitude != 0.0 || gpsData.longitude != 0.0) {
        Serial.println("Last known position (stale data):");
        Serial.print("  Latitude:  ");
        Serial.print(gpsData.latitude, 7);
        Serial.println("°");
        Serial.print("  Longitude: ");
        Serial.print(gpsData.longitude, 7);
        Serial.println("°");
      } else {
        Serial.println("No position data available");
      }
    }
    
    Serial.println();
  }
  
  // Small delay to prevent excessive CPU usage
  delay(10);
}

