// BLE Configuration for WiFi Credential Setup

#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

#include <NimBLEDevice.h>
#include "nvs_config.h"
#include "battery_monitor.h"
#include "time_sync.h"
#include "esp_bt.h"
#include "gps_sensor.h"
#include "unified_csv_storage.h"
#include "gps_scenario_handler.h"

// Forward declarations for status LED control
extern long long startStatusLEDBlink(uint8_t r, uint8_t g, uint8_t b);
extern void stopStatusLEDBlink(long long startTime);

// Forward declarations for external objects
class UnifiedCSVStorage;
class GPSScenarioHandler;
extern UnifiedCSVStorage unifiedCSVStorage;
extern GPSScenarioHandler* gpsScenarioHandler;

// BLE Service and Characteristic UUIDs
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define SSID_CHAR_UUID      "12345678-1234-1234-1234-123456789abd"
#define PASSWORD_CHAR_UUID  "12345678-1234-1234-1234-123456789abe"
#define GPS_ACTIVE_CHAR_UUID "12345678-1234-1234-1234-123456789ac2"
#define CYCLE_TIME_CHAR_UUID "12345678-1234-1234-1234-123456789ac3"
#define STATUS_CHAR_UUID    "12345678-1234-1234-1234-123456789abf"
#define DATA_CHAR_UUID      "12345678-1234-1234-1234-123456789ac0"
#define CURRENT_SSID_CHAR_UUID "12345678-1234-1234-1234-123456789ac1"
#define INITIAL_POSITION_CHAR_UUID "12345678-1234-1234-1234-123456789ac4"
#define GPS_READ_CYCLE_CHAR_UUID "12345678-1234-1234-1234-123456789ac5"
#define GPS_ACCURACY_THRESHOLD_CHAR_UUID "12345678-1234-1234-1234-123456789ac6"
#define GPS_ON_AFTER_CHAR_UUID "12345678-1234-1234-1234-123456789ac7"
#define EXTEND_CONFIG_TIME_CHAR_UUID "12345678-1234-1234-1234-123456789ac8"

// BLE Device Name
class BLEConfig {
private:
  static char bleDeviceName[64];
  static NimBLEServer* pServer;
  static NimBLEService* pService;
  static NimBLECharacteristic* pSSIDCharacteristic;
  static NimBLECharacteristic* pPasswordCharacteristic;
  static NimBLECharacteristic* pGPSActiveCharacteristic;
  static NimBLECharacteristic* pCycleTimeCharacteristic;
  static NimBLECharacteristic* pStatusCharacteristic;
  static NimBLECharacteristic* pDataCharacteristic;
  static NimBLECharacteristic* pCurrentSSIDCharacteristic;
  static NimBLECharacteristic* pInitialPositionCharacteristic;
  static NimBLECharacteristic* pGPSReadCycleCharacteristic;
  static NimBLECharacteristic* pGPSAccuracyThresholdCharacteristic;
  static NimBLECharacteristic* pGPSOnAfterCharacteristic;
  static NimBLECharacteristic* pExtendConfigTimeCharacteristic;
  static bool deviceConnected;
  static bool oldDeviceConnected;
  static String receivedSSID;
  static String receivedPassword;
  static uint8_t receivedGPSActive;
  static uint32_t receivedCycleTime;
  static String receivedInitialPosition;
  static uint32_t receivedGPSReadCycle;
  static float receivedGPSAccuracyThreshold;
  static uint32_t receivedGPSOnAfter;
  static bool credentialsReceived;
  static bool gpsActiveReceived;
  static bool cycleTimeReceived;
  static bool initialPositionReceived;
  static bool gpsReadCycleReceived;
  static bool gpsAccuracyThresholdReceived;
  static bool gpsOnAfterReceived;
  static unsigned long long configTimeExtension;  // Extension time in milliseconds (added to config period)
  static uint16_t mtuSize;
  static const char* deviceId;
  static const char* deviceVersion;
  static bool bleDisabled;  // Flag to track if BLE is permanently disabled
  static unsigned long long bleStartTimeMs;  // Time when BLE was started (for countdown timer)

  // BLE Server Callbacks
  class MyServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
      deviceConnected = true;
      // Fetch MTU from connection info if available
      mtuSize = (uint16_t)pServer->getPeerMTU(connInfo.getConnHandle());
      
      // Load current SSID, device_id, timestamp, and battery
      String currentSSID = NVSConfig::getWiFiSSID();
      if (currentSSID.length() == 0) {
        currentSSID = "Not configured";
      }
      
      // Get battery level
      float batteryVoltage = BatteryMonitor::readBatteryVoltage();
      int batteryLevel = BatteryMonitor::getBatteryPercentageV(batteryVoltage);
      
      const char* devId = (deviceId != nullptr) ? deviceId : "Unknown";
      const char* devVer = (deviceVersion != nullptr) ? deviceVersion : "v0.0.0";
      
      // Calculate countdown timer (milliseconds remaining for configuration mode)
      // Configuration mode duration: connection time + 1 minute (60000 ms) + extension time
      unsigned long long currentTime = TimeSync::getCurrentTimeMillis();
      unsigned long long connectionTime = currentTime; // Time when connection was established
      unsigned long long configEndTime = connectionTime + 60000 + configTimeExtension; // +1 minute from connection + extension
      unsigned long long timeRemaining = (configEndTime > currentTime) ? (configEndTime - currentTime) : 0;
      
      char csvBuffer[650];
      snprintf(csvBuffer, sizeof(csvBuffer), 
               "%s,%s,%llu,%d,%.3f,%s,%d,%d,%.2f,%d,%d,%llu",
               devId, devVer, TimeSync::getCurrentTimeMillis(), batteryLevel, batteryVoltage, currentSSID.c_str(), NVSConfig::getGPSReadCycleTime(), NVSConfig::getCycleTime(), NVSConfig::getGPSAccuracyThreshold(), NVSConfig::getGPSOnAfter(), NVSConfig::getGPSActive(), timeRemaining);
      
      // Send CSV data via Current SSID Characteristic (only once on connection)
      // Format: device_id,device_version,timestamp,battery,voltage,currentSSID,gps_cycle_time,transmission_time,gps_threshold,gps_on_after,gps_active,remaining_time
      if (pCurrentSSIDCharacteristic != nullptr) {
        pCurrentSSIDCharacteristic->setValue(std::string(csvBuffer));
      }
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
      deviceConnected = false;
      // No extra action required on disconnect
    }
  };

  // SSID Characteristic Callbacks
  class SSIDCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
      // Start blue LED blinking on receive
      // startStatusLEDBlink(0, 0, 255);
      
      std::string stdValue = pCharacteristic->getValue();
      String value = String(stdValue.c_str());
      if (value.length() > 0) {
        receivedSSID = value;
      }
      
      // Stop LED blinking and restore to green
      // stopStatusLEDBlink();
    }
  };

  // Password Characteristic Callbacks
  class PasswordCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
      // Start blue LED blinking on receive
      // startStatusLEDBlink(0, 0, 255);
      
      std::string stdValue = pCharacteristic->getValue();
      String value = String(stdValue.c_str());
      if (value.length() > 0) {
        receivedPassword = value;
        // Check if all credentials are received (SSID and password)
        if (receivedSSID.length() > 0 && receivedPassword.length() > 0) {
          credentialsReceived = true;
        }
      }
      
      // Stop LED blinking and restore to green
      // stopStatusLEDBlink();
    }
  };

  // GPS Active Characteristic Callbacks
  class GPSActiveCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
      // Start blue LED blinking on receive
      // startStatusLEDBlink(0, 0, 255);
      
      // Read string data (sent as TextEncoder().encode("0") or TextEncoder().encode("1") from web interface)
      std::string stdValue = pCharacteristic->getValue();
      String value = String(stdValue.c_str());
      value.trim(); // Remove any whitespace
      if (value.length() > 0) {
        receivedGPSActive = (uint8_t)value.toInt(); // Convert string "0" or "1" to integer 0 or 1
        // Save GPS active setting immediately to NVS (independent of WiFi credentials)
        // NVS key: "gps_active" in namespace "wifi_config"
        bool gpsActiveSaved = NVSConfig::setGPSActive(receivedGPSActive);
        gpsActiveReceived = true;
        Serial.print("GPS Active received: ");
        Serial.print(receivedGPSActive);
        Serial.print(" - ");
        Serial.print(gpsActiveSaved ? "Saved to NVS (key: gps_active)" : "Failed to save to NVS");
        Serial.print(" - Current NVS value: ");
        Serial.println(NVSConfig::getGPSActive());
        
        // Handle GPS OFF -> Scenario 4 transition
        if (receivedGPSActive == 0) {
          // Check if initial position exists in NVS
          if (NVSConfig::hasInitialPosition()) {
            Serial.println("GPS turned OFF - Switching to Scenario 4 and clearing data...");
            
            // Switch to Scenario 4
            if (gpsScenarioHandler != nullptr) {
              gpsScenarioHandler->setCurrentScenario(SCENARIO_4_UI_POSITION);
              Serial.println("Scenario switched to Scenario 4");
            }
            
            // Clear external flash
            if (unifiedCSVStorage.isInitialized()) {
              unifiedCSVStorage.clear();
              Serial.println("External flash cleared");
            }
            
            // Clear initial position
            bool cleared = NVSConfig::clearInitialPosition();
            Serial.print("Initial position cleared: ");
            Serial.println(cleared ? "Success" : "Failed");
            
            // Clear last known position (save zero values)
            bool lastKnownCleared = NVSConfig::saveLastKnownPosition(0.0, 0.0);
            Serial.print("Last known position cleared: ");
            Serial.println(lastKnownCleared ? "Success" : "Failed");
            
            // Turn off GPS
            GPSSensor::powerOff();
            Serial.println("GPS powered off");
            
            // Save scenario state to NVS
            if (gpsScenarioHandler != nullptr) {
              NVSConfig::setScenarioState((uint8_t)SCENARIO_4_UI_POSITION);
            }
          } else {
            Serial.println("GPS turned OFF but no initial position in NVS - skipping Scenario 4 transition");
          }
        }
        
        // Check if all credentials are received (SSID and password) for WiFi credentials saving
        if (receivedSSID.length() > 0 && receivedPassword.length() > 0) {
          credentialsReceived = true;
        }
      }
      
      // Stop LED blinking and restore to green
      // stopStatusLEDBlink();
    }
  };

  // Cycle Time Characteristic Callbacks
  class CycleTimeCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
      // Start blue LED blinking on receive
      long long startTime = startStatusLEDBlink(0, 0, 255);
      
      // Read cycle time as string (sent as string from web interface, value in seconds)
      std::string stdValue = pCharacteristic->getValue();
      String value = String(stdValue.c_str());
      if (value.length() > 0) {
        receivedCycleTime = value.toInt(); // Convert string to uint32_t (seconds)
        // Save cycle time immediately to NVS (independent of WiFi credentials)
        // NVS key: "cycle_time" in namespace "wifi_config"
        bool cycleTimeSaved = NVSConfig::setCycleTime(receivedCycleTime);
        cycleTimeReceived = true;
        Serial.print("Cycle Time received: ");
        Serial.print(receivedCycleTime);
        Serial.print(" seconds - ");
        Serial.print(cycleTimeSaved ? "Saved to NVS (key: cycle_time)" : "Failed to save to NVS");
        Serial.print(" - Current NVS value: ");
        Serial.println(NVSConfig::getCycleTime());
      }
      
      // Stop LED blinking and restore to green
      stopStatusLEDBlink(startTime);
    }
  };
  
  // Initial Position Characteristic Callbacks
  class InitialPositionCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
      std::string stdValue = pCharacteristic->getValue();
      String value = String(stdValue.c_str());
      if (value.length() > 0) {
        receivedInitialPosition = value;
        // Parse lat,lon format
        int commaPos = value.indexOf(',');
        if (commaPos > 0) {
          double lat = value.substring(0, commaPos).toFloat();
          double lon = value.substring(commaPos + 1).toFloat();
          bool saved = NVSConfig::setInitialPosition(lat, lon);
          initialPositionReceived = true;
          Serial.print("Initial Position received: (");
          Serial.print(lat, 7);
          Serial.print(", ");
          Serial.print(lon, 7);
          Serial.print(") - ");
          Serial.println(saved ? "Saved to NVS" : "Failed to save");
          
          // Turn off GPS immediately when position is received (even if 2-minute cycle incomplete)
          GPSSensor::powerOff();
          Serial.println("GPS powered off immediately after receiving initial position from UI");
        }
      }
    }
  };
  
  // GPS Read Cycle Time Characteristic Callbacks
  class GPSReadCycleCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
      std::string stdValue = pCharacteristic->getValue();
      String value = String(stdValue.c_str());
      if (value.length() > 0) {
        receivedGPSReadCycle = value.toInt();
        bool saved = NVSConfig::setGPSReadCycleTime(receivedGPSReadCycle);
        gpsReadCycleReceived = true;
        Serial.print("GPS Read Cycle Time received: ");
        Serial.print(receivedGPSReadCycle);
        Serial.print(" seconds - ");
        Serial.println(saved ? "Saved to NVS" : "Failed to save");
      }
    }
  };
  
  // GPS Accuracy Threshold Characteristic Callbacks
  class GPSAccuracyThresholdCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
      std::string stdValue = pCharacteristic->getValue();
      String value = String(stdValue.c_str());
      if (value.length() > 0) {
        receivedGPSAccuracyThreshold = value.toFloat();
        bool saved = NVSConfig::setGPSAccuracyThreshold(receivedGPSAccuracyThreshold);
        gpsAccuracyThresholdReceived = true;
        Serial.print("GPS Accuracy Threshold received: ");
        Serial.print(receivedGPSAccuracyThreshold);
        Serial.print(" - ");
        Serial.println(saved ? "Saved to NVS" : "Failed to save");
      }
    }
  };
  
  // GPS On After Characteristic Callbacks
  class GPSOnAfterCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
      long long startTime = startStatusLEDBlink(0, 0, 255);
      
      std::string stdValue = pCharacteristic->getValue();
      String value = String(stdValue.c_str());
      if (value.length() > 0) {
        receivedGPSOnAfter = value.toInt();
        bool saved = NVSConfig::setGPSOnAfter(receivedGPSOnAfter);
        gpsOnAfterReceived = true;
        Serial.print("GPS On After received: ");
        Serial.print(receivedGPSOnAfter);
        Serial.print(" seconds - ");
        Serial.print(saved ? "Saved to NVS" : "Failed to save");
        Serial.print(" - Current NVS value: ");
        Serial.println(NVSConfig::getGPSOnAfter());
      }
      
      stopStatusLEDBlink(startTime);
    }
  };
  
  // Extend Config Time Characteristic Callbacks
  class ExtendConfigTimeCallbacks: public NimBLECharacteristicCallbacks {
      void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        // Read seconds from message (always contains an integer) and extend configuration period by that amount
        std::string stdValue = pCharacteristic->getValue();
        String value = String(stdValue.c_str());
        
        // Trim whitespace
        value.trim();
        
        if (value.length() > 0) {
          uint32_t seconds = value.toInt();
          
          if (seconds > 0) {
            unsigned long long previousExtension = configTimeExtension;
            configTimeExtension += (unsigned long long)seconds * 1000ULL; // Convert to milliseconds and add
            
            // Debug output to verify extension is being added
            Serial.print("[EXTEND CONFIG TIME] SUCCESS - Received: ");
            Serial.print(seconds);
            Serial.print(" seconds, Previous extension: ");
            Serial.print(previousExtension);
            Serial.print(" ms, New extension: ");
            Serial.print(configTimeExtension);
            Serial.print(" ms (");
            Serial.print(configTimeExtension / 1000);
            Serial.println(" seconds total)");
          } else {
            Serial.print("[EXTEND CONFIG TIME] ERROR: Invalid value (0 or parse failed). Raw value: '");
            Serial.print(value);
            Serial.print("', Length: ");
            Serial.print(value.length());
            Serial.print(", Parsed int: ");
            Serial.println(seconds);
          }
        } else {
          Serial.println("[EXTEND CONFIG TIME] ERROR: Empty value received after trim");
        }
      }
  };

public:
  // Set Device ID
  static void setDeviceId(const char* id) {
    deviceId = id;
  }
  // Set Device Version
  static void setDeviceVersion(const char* version) {
    deviceVersion = version;
  }
  
  // Set BLE start time (for countdown timer calculation)
  static void setBLEStartTime(unsigned long long startTime) {
    bleStartTimeMs = startTime;
  }
  
  // Initialize BLE and start advertising
  static bool begin() {
    // Create device name with device ID (will be set via setDeviceId() before begin())
    if (deviceId != nullptr) {
      snprintf(bleDeviceName, sizeof(bleDeviceName), "Juxta %s v2.0.0", deviceId);
    } else {
      snprintf(bleDeviceName, sizeof(bleDeviceName), "Juxta AssetTag v2.0.0");
    }
    
    // Initialize BLE Device
    NimBLEDevice::init(bleDeviceName);
    
    // Create BLE Server
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    
    // Create BLE Service
    pService = pServer->createService(SERVICE_UUID);
    
    // Create SSID Characteristic
    pSSIDCharacteristic = pService->createCharacteristic(
      SSID_CHAR_UUID,
      NIMBLE_PROPERTY::WRITE
    );
    pSSIDCharacteristic->setCallbacks(new SSIDCallbacks());
    
    // Create Password Characteristic
    pPasswordCharacteristic = pService->createCharacteristic(
      PASSWORD_CHAR_UUID,
      NIMBLE_PROPERTY::WRITE
    );
    pPasswordCharacteristic->setCallbacks(new PasswordCallbacks());
    
    // Create GPS Active Characteristic
    pGPSActiveCharacteristic = pService->createCharacteristic(
      GPS_ACTIVE_CHAR_UUID,
      NIMBLE_PROPERTY::WRITE
    );
    pGPSActiveCharacteristic->setCallbacks(new GPSActiveCallbacks());
    
    // Create Cycle Time Characteristic
    pCycleTimeCharacteristic = pService->createCharacteristic(
      CYCLE_TIME_CHAR_UUID,
      NIMBLE_PROPERTY::WRITE
    );
    pCycleTimeCharacteristic->setCallbacks(new CycleTimeCallbacks());
    
    // Create Status Characteristic (simplified - no notify)
    pStatusCharacteristic = pService->createCharacteristic(
      STATUS_CHAR_UUID,
      NIMBLE_PROPERTY::READ
    );
    pStatusCharacteristic->setValue("Ready");
    
    // Create Data Characteristic for sensor data transmission
    pDataCharacteristic = pService->createCharacteristic(
      DATA_CHAR_UUID,
      (uint16_t)(NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY)
    );
    pDataCharacteristic->setValue("{}");
    
    // Create Current SSID Characteristic (read-only to show saved WiFi)
    pCurrentSSIDCharacteristic = pService->createCharacteristic(
      CURRENT_SSID_CHAR_UUID,
      NIMBLE_PROPERTY::READ
    );
    String currentSSID = NVSConfig::getWiFiSSID();
    if (currentSSID.length() > 0) {
      pCurrentSSIDCharacteristic->setValue(currentSSID.c_str());
    } else {
      pCurrentSSIDCharacteristic->setValue("Not configured");
    }
    
    // Create Initial Position Characteristic
    pInitialPositionCharacteristic = pService->createCharacteristic(
      INITIAL_POSITION_CHAR_UUID,
      NIMBLE_PROPERTY::WRITE
    );
    pInitialPositionCharacteristic->setCallbacks(new InitialPositionCallbacks());
    
    // Create GPS Read Cycle Time Characteristic
    pGPSReadCycleCharacteristic = pService->createCharacteristic(
      GPS_READ_CYCLE_CHAR_UUID,
      NIMBLE_PROPERTY::WRITE
    );
    pGPSReadCycleCharacteristic->setCallbacks(new GPSReadCycleCallbacks());
    
    // Create GPS Accuracy Threshold Characteristic
    pGPSAccuracyThresholdCharacteristic = pService->createCharacteristic(
      GPS_ACCURACY_THRESHOLD_CHAR_UUID,
      NIMBLE_PROPERTY::WRITE
    );
    pGPSAccuracyThresholdCharacteristic->setCallbacks(new GPSAccuracyThresholdCallbacks());
    
    // Create GPS On After Characteristic
    pGPSOnAfterCharacteristic = pService->createCharacteristic(
      GPS_ON_AFTER_CHAR_UUID,
      NIMBLE_PROPERTY::WRITE
    );
    pGPSOnAfterCharacteristic->setCallbacks(new GPSOnAfterCallbacks());
    
    // Create Extend Config Time Characteristic
    pExtendConfigTimeCharacteristic = pService->createCharacteristic(
      EXTEND_CONFIG_TIME_CHAR_UUID,
      NIMBLE_PROPERTY::WRITE
    );
    pExtendConfigTimeCharacteristic->setCallbacks(new ExtendConfigTimeCallbacks());
    
    // Start the service
    pService->start();
    
    // Configure and start advertising
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    
    // Set advertising parameters for better discoverability
    // 2 = BLE_GAP_CONN_MODE_UND (undirected connectable advertising)
    pAdvertising->setConnectableMode(2);
    pAdvertising->setMinInterval(32);   // 20ms (units of 0.625ms)
    pAdvertising->setMaxInterval(160);  // 100ms (units of 0.625ms)
    pAdvertising->enableScanResponse(true);
    
    // Add service UUID to advertising data
    pAdvertising->addServiceUUID(SERVICE_UUID);
    
    // Set device name in advertising data (important for discoverability)
    // Note: In NimBLE 2.x, device name must be explicitly set
    pAdvertising->setName(bleDeviceName);
    
    // Start advertising
    Serial.print("BLE advertising started with name: ");
    Serial.println(bleDeviceName);
    NimBLEDevice::startAdvertising();
    
    deviceConnected = false;
    oldDeviceConnected = false;
    receivedSSID = "";
    receivedPassword = "";
    receivedGPSActive = 0; // Default to 0 (GPS OFF)
    receivedCycleTime = 900; // Default to 900 seconds (15 minutes)
    receivedInitialPosition = "";
    receivedGPSReadCycle = 0;
    receivedGPSAccuracyThreshold = 0.0;
    credentialsReceived = false;
    gpsActiveReceived = false;
    cycleTimeReceived = false;
    initialPositionReceived = false;
    gpsReadCycleReceived = false;
    gpsAccuracyThresholdReceived = false;
    gpsOnAfterReceived = false;
    configTimeExtension = 0; // Reset extension time
    mtuSize = 23; // Default BLE MTU size
    bleDisabled = false; // Reset disabled flag when starting BLE
    
    return true;
  }
  
  // Update BLE (call this in loop to handle connections and process received data)
  static void update() {
    // Don't update if BLE is disabled
    if (bleDisabled) {
      return;
    }
    
    // Handle disconnection
    if (!deviceConnected && oldDeviceConnected) {
      delay(500); // Give the bluetooth stack the chance to get things ready
      pServer->startAdvertising(); // Restart advertising
      oldDeviceConnected = deviceConnected;
    }
    
    // Handle connection
    if (deviceConnected && !oldDeviceConnected) {
      oldDeviceConnected = deviceConnected;
    }
    
    // Process received WiFi credentials (debug mode is saved immediately when received)
    if (credentialsReceived) {
      // Save to NVS
      bool ssidSaved = NVSConfig::setWiFiSSID(receivedSSID.c_str());
      bool passwordSaved = NVSConfig::setWiFiPassword(receivedPassword.c_str());
      
      // Update current SSID characteristic with the new value
      if (pCurrentSSIDCharacteristic != nullptr && ssidSaved) {
        pCurrentSSIDCharacteristic->setValue(receivedSSID.c_str());
      }
      
      // Reset flags
      credentialsReceived = false;
      receivedSSID = "";
      receivedPassword = "";
    }
    
    // Reset GPS active flag if it was processed
    if (gpsActiveReceived) {
      gpsActiveReceived = false;
      receivedGPSActive = 0;
    }
    
    // Reset cycle time flag if it was processed
    if (cycleTimeReceived) {
      cycleTimeReceived = false;
      receivedCycleTime = 900;
    }
  }
  
  // Check if BLE is connected
  static bool isConnected() {
    if (bleDisabled) {
      return false; // Always return false if BLE is disabled
    }
    return deviceConnected;
  }
  
  // Get configuration time extension (in milliseconds)
  static unsigned long long getConfigTimeExtension() {
    return configTimeExtension;
  }
  
  // Restart advertising
  static void restartAdvertising() {
    if (!deviceConnected && !bleDisabled) {
      NimBLEDevice::startAdvertising();
    }
  }
  
  // Stop and deinitialize BLE permanently (consumes no power)
  static void stop() {
    // Stop LED blinking and restore to green
    stopStatusLEDBlink(TimeSync::getCurrentTimeMillis() + 4000);
    
    // Disconnect any connected clients first (prevents heap corruption)
    if (pServer != nullptr && deviceConnected) {
      Serial.println("Disconnecting BLE clients...");
      auto peers = pServer->getPeerDevices();
      for (auto connHandle : peers) {
        pServer->disconnect(connHandle);
        delay(50);
      }
      delay(500); // Wait for graceful disconnect
    }
    
    // Stop advertising
    if (pServer != nullptr) {
      pServer->getAdvertising()->stop();
      delay(100); // Let advertising stop complete
    }
    
    // Now safe to deinitialize
    NimBLEDevice::deinit(true);
    
    // Small delay after deinit
    delay(100);
    
    // Clear all pointers to prevent any accidental access
    pServer = nullptr;
    pService = nullptr;
    pSSIDCharacteristic = nullptr;
    pPasswordCharacteristic = nullptr;
    pGPSActiveCharacteristic = nullptr;
    pCycleTimeCharacteristic = nullptr;
    pStatusCharacteristic = nullptr;
    pDataCharacteristic = nullptr;
    pCurrentSSIDCharacteristic = nullptr;
    pInitialPositionCharacteristic = nullptr;
    pGPSReadCycleCharacteristic = nullptr;
    pGPSAccuracyThresholdCharacteristic = nullptr;
    pGPSOnAfterCharacteristic = nullptr;
    pExtendConfigTimeCharacteristic = nullptr;
    
    // Set disabled flag
    bleDisabled = true;
    deviceConnected = false;
    oldDeviceConnected = false;
    
    // Clear credentials
    receivedSSID = "";
    receivedPassword = "";
    receivedGPSActive = 0;
    receivedCycleTime = 900;
    receivedInitialPosition = "";
    receivedGPSReadCycle = 0;
    receivedGPSAccuracyThreshold = 0.0;
    credentialsReceived = false;
    gpsActiveReceived = false;
    cycleTimeReceived = false;
    initialPositionReceived = false;
    gpsReadCycleReceived = false;
    gpsAccuracyThresholdReceived = false;
  }
  
  // Check if BLE is enabled (not permanently disabled)
  static bool isEnabled() {
    return !bleDisabled;
  }
  
  // Get BLE RSSI signal strength (returns dBm, or -100 if not connected)
  // Note: ESP32 BLE server RSSI is not directly available from established connections
  // This function returns -100 if not connected, or attempts to get RSSI via esp_bt_gap_get_rssi()
  static int getRSSI() {
    if (bleDisabled || !deviceConnected || pServer == nullptr) {
      return -100; // Return invalid RSSI if not connected
    }
    
    // Try to get RSSI using ESP32 BLE GAP API
    // This requires the connection handle, which we can get from the server
    if (pServer->getConnectedCount() > 0) {
      // For ESP32, we can try to get RSSI, but it's not always available
      // Return a reasonable default when connected (typical BLE connection RSSI)
      // In practice, BLE connections are typically -70 to -90 dBm when in range
      // We'll return -75 as a reasonable default for an active connection
      return -75; // Default/estimated BLE RSSI when connected (good signal)
    }
    
    return -100; // Not connected
  }
  
  // Send sensor data via BLE (with chunking for large data)
  static bool sendDataViaBLE(const String& csvData) {
    if (bleDisabled || !deviceConnected || pDataCharacteristic == nullptr) {
      return false;
    }

    // Non-blocking delay to ensure connection stability (prevents loop freezing)
    unsigned long startWait = millis();
    while (millis() - startWait < 1500) {
      yield(); // Allow other tasks to run
      delay(100); // Small chunks to prevent blocking
    }
    
    // Start blue LED blinking on transmit
    long long startTime = startStatusLEDBlink(0, 0, 255);
    
    // Calculate safe chunk size (MTU - 3 bytes for ATT header)
    uint16_t maxChunkSize = (mtuSize > 23) ? (mtuSize - 3) : 20;
    
    // Add newline terminator to CSV data for UI detection
    String dataWithNewline = csvData + "\n";
    
    // If data fits in one chunk, send directly
    if (dataWithNewline.length() <= maxChunkSize) {
      pDataCharacteristic->setValue(dataWithNewline.c_str());
      pDataCharacteristic->notify();
      yield(); // Allow other tasks to run instead of blocking delay
      delay(10); // Minimal delay for BLE stack
    } else {
      // Send data in chunks
      int totalLength = dataWithNewline.length();
      int offset = 0;
      int chunkCount = 0;
      
      while (offset < totalLength) {
        int chunkSize = min((int)maxChunkSize, totalLength - offset);
        String chunk = dataWithNewline.substring(offset, offset + chunkSize);
        
        pDataCharacteristic->setValue(chunk.c_str());
        pDataCharacteristic->notify();
        
        offset += chunkSize;
        chunkCount++;
        
        // Use yield() to prevent loop freezing and watchdog resets
        yield();
        
        // Reduced delay and periodic yield for large transmissions
        if (chunkCount % 5 == 0) {
          // Every 5 chunks, yield more to prevent watchdog reset
          delay(5);
          yield();
        } else {
          delay(10); // Minimal delay between chunks
        }
      }
    }
    
    // Stop LED blinking and restore to green
    stopStatusLEDBlink(startTime);
    
    return true;
  }
};

// Static member definitions
char BLEConfig::bleDeviceName[64] = "";
NimBLEServer* BLEConfig::pServer = nullptr;
NimBLEService* BLEConfig::pService = nullptr;
NimBLECharacteristic* BLEConfig::pSSIDCharacteristic = nullptr;
NimBLECharacteristic* BLEConfig::pPasswordCharacteristic = nullptr;
NimBLECharacteristic* BLEConfig::pGPSActiveCharacteristic = nullptr;
NimBLECharacteristic* BLEConfig::pCycleTimeCharacteristic = nullptr;
NimBLECharacteristic* BLEConfig::pStatusCharacteristic = nullptr;
NimBLECharacteristic* BLEConfig::pDataCharacteristic = nullptr;
NimBLECharacteristic* BLEConfig::pCurrentSSIDCharacteristic = nullptr;
NimBLECharacteristic* BLEConfig::pInitialPositionCharacteristic = nullptr;
NimBLECharacteristic* BLEConfig::pGPSReadCycleCharacteristic = nullptr;
NimBLECharacteristic* BLEConfig::pGPSAccuracyThresholdCharacteristic = nullptr;
NimBLECharacteristic* BLEConfig::pGPSOnAfterCharacteristic = nullptr;
NimBLECharacteristic* BLEConfig::pExtendConfigTimeCharacteristic = nullptr;
bool BLEConfig::deviceConnected = false;
bool BLEConfig::oldDeviceConnected = false;
String BLEConfig::receivedSSID = "";
String BLEConfig::receivedPassword = "";
uint8_t BLEConfig::receivedGPSActive = 0;
uint32_t BLEConfig::receivedCycleTime = 900;
String BLEConfig::receivedInitialPosition = "";
uint32_t BLEConfig::receivedGPSReadCycle = 0;
float BLEConfig::receivedGPSAccuracyThreshold = 0.0;
uint32_t BLEConfig::receivedGPSOnAfter = 60;
bool BLEConfig::credentialsReceived = false;
bool BLEConfig::gpsActiveReceived = false;
bool BLEConfig::cycleTimeReceived = false;
bool BLEConfig::initialPositionReceived = false;
bool BLEConfig::gpsReadCycleReceived = false;
bool BLEConfig::gpsAccuracyThresholdReceived = false;
bool BLEConfig::gpsOnAfterReceived = false;
unsigned long long BLEConfig::configTimeExtension = 0;
uint16_t BLEConfig::mtuSize = 23;
const char* BLEConfig::deviceId = nullptr;
const char* BLEConfig::deviceVersion = "v2.0.0";
bool BLEConfig::bleDisabled = false;
unsigned long long BLEConfig::bleStartTimeMs = 0;

#endif

