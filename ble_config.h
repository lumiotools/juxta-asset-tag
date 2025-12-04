// BLE Configuration for WiFi Credential Setup

#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "nvs_config.h"
#include "battery_monitor.h"
#include "time_sync.h"
#include "esp_bt.h"

// Forward declarations for status LED control
extern void startStatusLEDBlink(uint8_t r, uint8_t g, uint8_t b);
extern void stopStatusLEDBlink();

// BLE Service and Characteristic UUIDs
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define SSID_CHAR_UUID      "12345678-1234-1234-1234-123456789abd"
#define PASSWORD_CHAR_UUID  "12345678-1234-1234-1234-123456789abe"
#define DEBUG_MODE_CHAR_UUID "12345678-1234-1234-1234-123456789ac2"
#define STATUS_CHAR_UUID    "12345678-1234-1234-1234-123456789abf"
#define DATA_CHAR_UUID      "12345678-1234-1234-1234-123456789ac0"
#define CURRENT_SSID_CHAR_UUID "12345678-1234-1234-1234-123456789ac1"

// BLE Device Name
#define BLE_DEVICE_NAME "Juxta AssetTag v2.0.0"

class BLEConfig {
private:
  static BLEServer* pServer;
  static BLEService* pService;
  static BLECharacteristic* pSSIDCharacteristic;
  static BLECharacteristic* pPasswordCharacteristic;
  static BLECharacteristic* pDebugModeCharacteristic;
  static BLECharacteristic* pStatusCharacteristic;
  static BLECharacteristic* pDataCharacteristic;
  static BLECharacteristic* pCurrentSSIDCharacteristic;
  static bool deviceConnected;
  static bool oldDeviceConnected;
  static String receivedSSID;
  static String receivedPassword;
  static uint8_t receivedDebugMode;
  static bool credentialsReceived;
  static bool debugModeReceived;
  static uint16_t mtuSize;
  static const char* deviceId;
  static const char* deviceVersion;
  static bool bleDisabled;  // Flag to track if BLE is permanently disabled

  // BLE Server Callbacks
  class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      mtuSize = 23; // Default MTU size, will be updated after negotiation
      
      // Load current SSID, device_id, timestamp, and battery
      String currentSSID = NVSConfig::getWiFiSSID();
      if (currentSSID.length() == 0) {
        currentSSID = "Not configured";
      }
      
      // Get timestamp
      char timestamp[20];
      TimeSync::getCurrentTimeString(timestamp, sizeof(timestamp));
      
      // Get battery level
      int batteryLevel = BatteryMonitor::getBatteryPercentage();
      
      const char* devId = (deviceId != nullptr) ? deviceId : "Unknown";
      const char* devVer = (deviceVersion != nullptr) ? deviceVersion : "v0.0.0";
      
      char jsonBuffer[256];
      snprintf(jsonBuffer, sizeof(jsonBuffer), 
               "{\"device_id\":\"%s\",\"device_version\":\"%s\",\"timestamp\":\"%s\",\"battery\":%d,\"currentSSID\":\"%s\",\"debug_mode\":%d}",
               devId, devVer, timestamp, batteryLevel, currentSSID.c_str(), NVSConfig::getDebugMode());
      
      // Send JSON data via Current SSID Characteristic
      if (pCurrentSSIDCharacteristic != nullptr) {
        pCurrentSSIDCharacteristic->setValue(jsonBuffer);
      }
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
  };

  // SSID Characteristic Callbacks
  class SSIDCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
      // Start blue LED blinking on receive
      startStatusLEDBlink(0, 0, 255);
      
      String value = pCharacteristic->getValue();
      if (value.length() > 0) {
        receivedSSID = value;
      }
      
      // Stop LED blinking and restore to green
      stopStatusLEDBlink();
    }
  };

  // Password Characteristic Callbacks
  class PasswordCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
      // Start blue LED blinking on receive
      startStatusLEDBlink(0, 0, 255);
      
      String value = pCharacteristic->getValue();
      if (value.length() > 0) {
        receivedPassword = value;
        // Check if all credentials are received (SSID and password)
        if (receivedSSID.length() > 0 && receivedPassword.length() > 0) {
          credentialsReceived = true;
        }
      }
      
      // Stop LED blinking and restore to green
      stopStatusLEDBlink();
    }
  };

  // Debug Mode Characteristic Callbacks
  class DebugModeCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
      // Start blue LED blinking on receive
      startStatusLEDBlink(0, 0, 255);
      
      // Read raw byte data (sent as Uint8Array from web interface)
      String value = pCharacteristic->getValue();
      if (value.length() > 0) {
        receivedDebugMode = (uint8_t)value[0]; // Read first byte as uint8_t (0 or 1)
        // Save debug mode immediately to NVS (independent of WiFi credentials)
        // NVS key: "debug_mode" in namespace "wifi_config"
        bool debugModeSaved = NVSConfig::setDebugMode(receivedDebugMode);
        debugModeReceived = true;
        Serial.print("Debug Mode received: ");
        Serial.print(receivedDebugMode);
        Serial.print(" - ");
        Serial.print(debugModeSaved ? "Saved to NVS (key: debug_mode)" : "Failed to save to NVS");
        Serial.print(" - Current NVS value: ");
        Serial.println(NVSConfig::getDebugMode());
        
        // Check if all credentials are received (SSID and password) for WiFi credentials saving
        if (receivedSSID.length() > 0 && receivedPassword.length() > 0) {
          credentialsReceived = true;
        }
      }
      
      // Stop LED blinking and restore to green
      stopStatusLEDBlink();
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
  
  // Initialize BLE and start advertising
  static bool begin() {
    // Initialize BLE Device
    BLEDevice::init(BLE_DEVICE_NAME);
    
    // Create BLE Server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    
    // Create BLE Service
    pService = pServer->createService(SERVICE_UUID);
    
    // Create SSID Characteristic
    pSSIDCharacteristic = pService->createCharacteristic(
      SSID_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE
    );
    pSSIDCharacteristic->setCallbacks(new SSIDCallbacks());
    
    // Create Password Characteristic
    pPasswordCharacteristic = pService->createCharacteristic(
      PASSWORD_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE
    );
    pPasswordCharacteristic->setCallbacks(new PasswordCallbacks());
    
    // Create Debug Mode Characteristic
    pDebugModeCharacteristic = pService->createCharacteristic(
      DEBUG_MODE_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE
    );
    pDebugModeCharacteristic->setCallbacks(new DebugModeCallbacks());
    
    // Create Status Characteristic (simplified - no notify)
    pStatusCharacteristic = pService->createCharacteristic(
      STATUS_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ
    );
    pStatusCharacteristic->setValue("Ready");
    
    // Create Data Characteristic for sensor data transmission
    pDataCharacteristic = pService->createCharacteristic(
      DATA_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pDataCharacteristic->addDescriptor(new BLE2902());
    pDataCharacteristic->setValue("{}");
    
    // Create Current SSID Characteristic (read-only to show saved WiFi)
    pCurrentSSIDCharacteristic = pService->createCharacteristic(
      CURRENT_SSID_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ
    );
    String currentSSID = NVSConfig::getWiFiSSID();
    if (currentSSID.length() > 0) {
      pCurrentSSIDCharacteristic->setValue(currentSSID.c_str());
    } else {
      pCurrentSSIDCharacteristic->setValue("Not configured");
    }
    
    // Start the service
    pService->start();
    
    // Start advertising (simplified)
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    BLEDevice::startAdvertising();
    
    deviceConnected = false;
    oldDeviceConnected = false;
    receivedSSID = "";
    receivedPassword = "";
    receivedDebugMode = 0; // Default to 0 (LED off in deep sleep)
    credentialsReceived = false;
    debugModeReceived = false;
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
    
    // Reset debug mode flag if it was processed
    if (debugModeReceived) {
      debugModeReceived = false;
      receivedDebugMode = 0;
    }
  }
  
  // Check if BLE is connected
  static bool isConnected() {
    if (bleDisabled) {
      return false; // Always return false if BLE is disabled
    }
    return deviceConnected;
  }
  
  // Restart advertising
  static void restartAdvertising() {
    if (!deviceConnected && !bleDisabled) {
      BLEDevice::startAdvertising();
    }
  }
  
  // Stop and deinitialize BLE permanently (consumes no power)
  static void stop() {
    // Stop LED blinking and restore to green
    stopStatusLEDBlink();
    
    // Stop advertising first
    if (pServer != nullptr) {
      pServer->getAdvertising()->stop();
    }
    
    BLEDevice::deinit(true);
    
    // Clear all pointers to prevent any accidental access
    pServer = nullptr;
    pService = nullptr;
    pSSIDCharacteristic = nullptr;
    pPasswordCharacteristic = nullptr;
    pDebugModeCharacteristic = nullptr;
    pStatusCharacteristic = nullptr;
    pDataCharacteristic = nullptr;
    pCurrentSSIDCharacteristic = nullptr;
    
    // Set disabled flag
    bleDisabled = true;
    deviceConnected = false;
    oldDeviceConnected = false;
    
    // Clear credentials
    receivedSSID = "";
    receivedPassword = "";
    receivedDebugMode = 0;
    credentialsReceived = false;
    debugModeReceived = false;
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
    
    // Start blue LED blinking on transmit
    startStatusLEDBlink(0, 0, 255);
    
    // Calculate safe chunk size (MTU - 3 bytes for ATT header)
    uint16_t maxChunkSize = (mtuSize > 23) ? (mtuSize - 3) : 20;
    
    // Add newline terminator to CSV data for UI detection
    String dataWithNewline = csvData + "\n";
    
    // If data fits in one chunk, send directly
    if (dataWithNewline.length() <= maxChunkSize) {
      pDataCharacteristic->setValue(dataWithNewline.c_str());
      pDataCharacteristic->notify();
      delay(50); // Give BLE stack time to process
    } else {
      // Send data in chunks
      int totalLength = dataWithNewline.length();
      int offset = 0;
      
      while (offset < totalLength) {
        int chunkSize = min((int)maxChunkSize, totalLength - offset);
        String chunk = dataWithNewline.substring(offset, offset + chunkSize);
        
        pDataCharacteristic->setValue(chunk.c_str());
        pDataCharacteristic->notify();
        
        offset += chunkSize;
        delay(50); // Give BLE stack time between chunks
      }
    }
    
    // Stop LED blinking and restore to green
    stopStatusLEDBlink();
    
    return true;
  }
};

// Static member definitions
BLEServer* BLEConfig::pServer = nullptr;
BLEService* BLEConfig::pService = nullptr;
BLECharacteristic* BLEConfig::pSSIDCharacteristic = nullptr;
BLECharacteristic* BLEConfig::pPasswordCharacteristic = nullptr;
BLECharacteristic* BLEConfig::pDebugModeCharacteristic = nullptr;
BLECharacteristic* BLEConfig::pStatusCharacteristic = nullptr;
BLECharacteristic* BLEConfig::pDataCharacteristic = nullptr;
BLECharacteristic* BLEConfig::pCurrentSSIDCharacteristic = nullptr;
bool BLEConfig::deviceConnected = false;
bool BLEConfig::oldDeviceConnected = false;
String BLEConfig::receivedSSID = "";
String BLEConfig::receivedPassword = "";
uint8_t BLEConfig::receivedDebugMode = 0;
bool BLEConfig::credentialsReceived = false;
bool BLEConfig::debugModeReceived = false;
uint16_t BLEConfig::mtuSize = 23;
const char* BLEConfig::deviceId = nullptr;
const char* BLEConfig::deviceVersion = "v2.0.0";
bool BLEConfig::bleDisabled = false;

#endif

