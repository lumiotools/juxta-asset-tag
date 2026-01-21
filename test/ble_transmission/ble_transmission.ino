// BLE Transmission Test
// This test verifies BLE transmission functionality
// - Simple test without power latch
// - Repeats every 30 seconds
// - Turns on BLE, waits for connection, transmits fixed data, then repeats
//
// Based on PMC transmission logic for BLE transmission pattern

#include <Arduino.h>
#include "../../ble_config.h"
#include "../../device_id.h"
#include "../../time_sync.h"
#include "../../battery_monitor.h"
#include "../../customwifi.h"
#include "../../nvs_config.h"
#include "../../spi_flash_handler.h"
#include "../../unified_csv_storage.h"

// External objects required by ble_config.h
SPIFlashHandler spiFlash;
UnifiedCSVStorage unifiedCSVStorage;

// WiFi credentials (set these before uploading)
const char* WIFI_SSID = "Realme P2 Pro";
const char* WIFI_PASSWORD = "0000000000";

// Device ID buffer
static char deviceIdBuffer[32];
const char* DEVICE_ID = deviceIdBuffer;

// Test data parameters (same data types as PMC transmission)
int testScenario = 1;  // Scenario value
double testLatitude = 37.7749;  // Test latitude
double testLongitude = -122.4194;  // Test longitude
double testHdop = 1.5;  // Test HDOP value

// Timing variables
unsigned long long lastTransmissionTime = 0;
const unsigned long long TRANSMISSION_INTERVAL_MS = 20000; // 30 seconds

// BLE connection timeout (4 seconds as per PMC transmission)
const unsigned long long BLE_CONNECTION_TIMEOUT_MS = 4000; // 4 seconds

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n========================================");
  Serial.println("BLE Transmission Test");
  Serial.println("========================================\n");

  // Initialize NVS
  Serial.println("Initializing NVS...");
  NVSConfig::initializeNVS();
  
  // Save WiFi credentials to NVS
  Serial.println("Saving WiFi credentials to NVS...");
  NVSConfig::setWiFiSSID(WIFI_SSID);
  NVSConfig::setWiFiPassword(WIFI_PASSWORD);
  Serial.print("WiFi SSID: ");
  Serial.println(WIFI_SSID);
  
  // Initialize Device ID
  DeviceID::getDeviceId(deviceIdBuffer, sizeof(deviceIdBuffer));
  Serial.print("Device ID: ");
  Serial.println(DEVICE_ID);

  // Initialize battery monitor
  Serial.println("Initializing battery monitor...");
  BatteryMonitor::initializeADC();
  
  // Connect WiFi for time sync (same as main ino file)
  Serial.println("Connecting WiFi for time sync...");
  CustomWiFi::connectWiFi();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected - attempting time sync...");
    TimeSync::begin();
    bool syncSuccess = TimeSync::syncTimeNTP();
    if (syncSuccess) {
      Serial.println("Time sync successful");
    } else {
      Serial.println("Time sync failed, will continue without synced time");
    }
    CustomWiFi::disconnectWiFi();
    Serial.println("WiFi disconnected after time sync");
  } else {
    Serial.println("WiFi connection failed - skipping time sync");
  }

  // Initialize BLE with device ID
  Serial.println("Initializing BLE...");
  BLEConfig::setDeviceId(DEVICE_ID);
  BLEConfig::setDeviceVersion("v2.0.0");
  // BLEConfig::begin();
  
  Serial.println("BLE initialized");
  Serial.println("Test will start in 2 seconds...");
  delay(2000);
  
  // Initialize last transmission time using TimeSync (same as main ino)
  lastTransmissionTime = TimeSync::getCurrentTimeMillis();
  
  Serial.println("\n=== Setup Complete ===");
  Serial.println("Waiting for 30-second cycle to begin...\n");
}

void loop() {
  unsigned long long currentTime = TimeSync::getCurrentTimeMillis();
  
  // Check if 30 seconds have passed since last transmission
  if ((currentTime - lastTransmissionTime) >= TRANSMISSION_INTERVAL_MS) {
    Serial.println("\n========== Transmission Cycle Started ==========");
    
    // Turn on BLE (it was stopped after last transmission)
    Serial.println("Turning BLE on...");
    BLEConfig::begin();
    delay(100); // Give BLE time to initialize
    
    // Wait for BLE connection (with timeout) - same as PMC transmission
    Serial.println("Waiting for BLE connection (4 sec timeout)...");
    unsigned long long bleStartTime = TimeSync::getCurrentTimeMillis();
    bool bleConnectionAchieved = false;
    
    while (!BLEConfig::isConnected() && 
           (TimeSync::getCurrentTimeMillis() - bleStartTime) < BLE_CONNECTION_TIMEOUT_MS) {
      BLEConfig::update(); // Process BLE events
      delay(100); // Small delay to prevent tight loop
    }
    
    bleConnectionAchieved = BLEConfig::isConnected();
    
    if (bleConnectionAchieved) {
      Serial.println("BLE connection achieved!");
      
      // Gather current device values (same as PMC transmission)
      char deviceIdBuffer[32];
      DeviceID::getDeviceId(deviceIdBuffer, sizeof(deviceIdBuffer));
      const char* deviceId = deviceIdBuffer;
      int batteryPercent = BatteryMonitor::getBatteryPercentage();
      float batteryVoltage = BatteryMonitor::readBatteryVoltage();
      unsigned long long timestamp = TimeSync::getCurrentTimeMillis();
      
      // Format data exactly as PMC transmission (same data types and format)
      // Format: device_id,battery%,voltage,timestamp,scenario,lat,lon,hdop
      char payload[200];
      snprintf(payload, sizeof(payload), "%s,%d,%.2f,%llu,%d,%.7f,%.7f,%.2f", 
               deviceId, batteryPercent, batteryVoltage, timestamp, testScenario, 
               testLatitude, testLongitude, testHdop);
      
      Serial.println("========== BLE Transmission Data ==========");
      Serial.print("Device ID: ");
      Serial.println(deviceId);
      Serial.print("Battery: ");
      Serial.print(batteryPercent);
      Serial.print("% (");
      Serial.print(batteryVoltage, 2);
      Serial.println("V)");
      Serial.print("Timestamp: ");
      Serial.println(timestamp);
      Serial.print("Scenario: ");
      Serial.println(testScenario);
      Serial.print("GPS: (");
      Serial.print(testLatitude, 7);
      Serial.print(", ");
      Serial.print(testLongitude, 7);
      Serial.print("), HDOP: ");
      Serial.println(testHdop, 2);
      Serial.print("Payload: ");
      Serial.println(payload);
      Serial.println("===========================================");
      
      // Send data via BLE
      bool bleSuccess = BLEConfig::sendDataViaBLE(String(payload));
      
      if (bleSuccess) {
        Serial.println("BLE transmission successful!");
      } else {
        Serial.println("BLE transmission failed");
      }
      
    } else {
      Serial.println("BLE connection timeout (4 sec) - no device connected");
    }
    
    // Turn off BLE after transmission
    Serial.println("Turning BLE off...");
    BLEConfig::stop();
    Serial.println("BLE turned off");
    
    // Update last transmission time using TimeSync
    lastTransmissionTime = TimeSync::getCurrentTimeMillis();
    
    Serial.println("========== Transmission Cycle Complete ==========");
    Serial.print("Next transmission in ");
    Serial.print(TRANSMISSION_INTERVAL_MS / 1000);
    Serial.println(" seconds\n");
  }
  
  // Update BLE events even when not transmitting (if BLE is enabled)
  if (BLEConfig::isEnabled()) {
    BLEConfig::update();
  }
  
  // Small delay to prevent tight loop
  delay(100);
}
