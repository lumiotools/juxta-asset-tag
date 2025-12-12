// BLE Configuration for WiFi Credential Setup
// Using NimBLE for better stability and memory management

#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

#include "NimBLEDevice.h"
#include "NimBLEServer.h"
#include "NimBLEUtils.h"
#include "NimBLEDescriptor.h"
#include "nvs_config.h"
#include "battery_monitor.h"
#include "time_sync.h"

// Forward declarations for status LED control
extern long long startStatusLEDBlink(uint8_t r, uint8_t g, uint8_t b);
extern void stopStatusLEDBlink(long long startTime);

// BLE Service and Characteristic UUIDs
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define SSID_CHAR_UUID      "12345678-1234-1234-1234-123456789abd"
#define PASSWORD_CHAR_UUID  "12345678-1234-1234-1234-123456789abe"
#define GPS_ACTIVE_CHAR_UUID "12345678-1234-1234-1234-123456789ac2"
#define CYCLE_TIME_CHAR_UUID "12345678-1234-1234-1234-123456789ac3"
#define STATUS_CHAR_UUID    "12345678-1234-1234-1234-123456789abf"
#define DATA_CHAR_UUID      "12345678-1234-1234-1234-123456789ac0"
#define CURRENT_SSID_CHAR_UUID "12345678-1234-1234-1234-123456789ac1"

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
  static bool deviceConnected;
  static bool oldDeviceConnected;
  static String receivedSSID;
  static String receivedPassword;
  static uint8_t receivedGPSActive;
  static uint32_t receivedCycleTime;
  static bool credentialsReceived;
  static bool gpsActiveReceived;
  static bool cycleTimeReceived;
  static uint16_t mtuSize;
  static const char* deviceId;
  static const char* deviceVersion;
  static bool bleDisabled;  // Flag to track if BLE is permanently disabled

  // BLE Server Callbacks
  class MyServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
      deviceConnected = true;
      mtuSize = connInfo.getMTU(); // Get actual MTU from connection
      if (mtuSize < 23) mtuSize = 23; // Default to minimum if not set
      
      Serial.print("BLE Client connected - MTU: ");
      Serial.println(mtuSize);
      
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
      
      char jsonBuffer[350];
      snprintf(jsonBuffer, sizeof(jsonBuffer), 
               "{\"device_id\":\"%s\",\"device_version\":\"%s\",\"timestamp\":\"%llu\",\"battery\":%d,\"voltage\":%.3f,\"currentSSID\":\"%s\",\"gps_active\":%d,\"cycle_time\":%d}",
               devId, devVer, TimeSync::getCurrentTimeMillis(), batteryLevel, batteryVoltage, currentSSID.c_str(), NVSConfig::getGPSActive(), NVSConfig::getCycleTime());
      
      // Send JSON data via Current SSID Characteristic
      if (pCurrentSSIDCharacteristic != nullptr) {
        pCurrentSSIDCharacteristic->setValue(jsonBuffer);
      }
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
      deviceConnected = false;
      Serial.print("BLE Client disconnected - reason: ");
      Serial.println(reason);
    }
    
    void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) {
      mtuSize = MTU;
      Serial.print("BLE MTU updated to: ");
      Serial.println(mtuSize);
    }
  };

  // SSID Characteristic Callbacks
  class SSIDCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
      std::string value = pCharacteristic->getValue();
      if (value.length() > 0) {
        receivedSSID = String(value.c_str());
      }
    }
  };

  // Password Characteristic Callbacks
  class PasswordCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
      std::string value = pCharacteristic->getValue();
      if (value.length() > 0) {
        receivedPassword = String(value.c_str());
        // Check if all credentials are received (SSID and password)
        if (receivedSSID.length() > 0 && receivedPassword.length() > 0) {
          credentialsReceived = true;
        }
      }
    }
  };

  // GPS Active Characteristic Callbacks
  class GPSActiveCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
      std::string value = pCharacteristic->getValue();
      if (value.length() > 0) {
        receivedGPSActive = (uint8_t)value[0]; // Read first byte as uint8_t (0 or 1)
        // Save GPS active setting immediately to NVS (independent of WiFi credentials)
        bool gpsActiveSaved = NVSConfig::setGPSActive(receivedGPSActive);
        gpsActiveReceived = true;
        Serial.print("GPS Active received: ");
        Serial.print(receivedGPSActive);
        Serial.print(" - ");
        Serial.print(gpsActiveSaved ? "Saved to NVS (key: gps_active)" : "Failed to save to NVS");
        Serial.print(" - Current NVS value: ");
        Serial.println(NVSConfig::getGPSActive());
        
        // Check if all credentials are received (SSID and password) for WiFi credentials saving
        if (receivedSSID.length() > 0 && receivedPassword.length() > 0) {
          credentialsReceived = true;
        }
      }
    }
  };

  // Cycle Time Characteristic Callbacks
  class CycleTimeCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
      // Start blue LED blinking on receive
      long long startTime = startStatusLEDBlink(0, 0, 255);
      
      // Read cycle time as string (sent as string from web interface, value in seconds)
      std::string value = pCharacteristic->getValue();
      if (value.length() > 0) {
        receivedCycleTime = String(value.c_str()).toInt(); // Convert string to uint32_t (seconds)
        // Save cycle time immediately to NVS (independent of WiFi credentials)
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
    // Create device name with device ID (will be set via setDeviceId() before begin())
    if (deviceId != nullptr) {
      snprintf(bleDeviceName, sizeof(bleDeviceName), "Juxta %s v2.0.0", deviceId);
    } else {
      snprintf(bleDeviceName, sizeof(bleDeviceName), "Juxta AssetTag v2.0.0");
    }
    
    // Initialize NimBLE Device (NimBLE handles init/deinit properly, safe to call after deinit)
    // NimBLE can safely handle init() even if already initialized
    NimBLEDevice::init(bleDeviceName);
    
    // Set power (optional, can help with power consumption)
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Maximum power
    
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
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    // Add CCCD descriptor for notifications (NimBLE creates this automatically, but we can set it explicitly)
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
    
    // Start the service
    pService->start();
    
    // Start advertising
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    NimBLEDevice::startAdvertising();
    
    deviceConnected = false;
    oldDeviceConnected = false;
    receivedSSID = "";
    receivedPassword = "";
    receivedGPSActive = 0; // Default to 0 (GPS OFF)
    receivedCycleTime = 900; // Default to 900 seconds (15 minutes)
    credentialsReceived = false;
    gpsActiveReceived = false;
    cycleTimeReceived = false;
    mtuSize = 23; // Default BLE MTU size
    bleDisabled = false; // Reset disabled flag when starting BLE
    
    Serial.println("NimBLE initialized and advertising");
    
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
      if (pServer != nullptr) {
        NimBLEDevice::startAdvertising(); // Restart advertising
      }
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
      Serial.println("Disconnecting BLE client...");
      // NimBLE will handle disconnection when we stop advertising
      // The connection will be closed automatically
      delay(500); // Wait for any ongoing operations to complete
    }
    
    // Stop advertising
    if (pServer != nullptr) {
      NimBLEDevice::getAdvertising()->stop();
      delay(100); // Let advertising stop complete
    }
    
    // Clear all pointers before deinit to prevent any accidental access
    pServer = nullptr;
    pService = nullptr;
    pSSIDCharacteristic = nullptr;
    pPasswordCharacteristic = nullptr;
    pGPSActiveCharacteristic = nullptr;
    pCycleTimeCharacteristic = nullptr;
    pStatusCharacteristic = nullptr;
    pDataCharacteristic = nullptr;
    pCurrentSSIDCharacteristic = nullptr;
    
    // NimBLE handles deinit properly - safe to call and re-init later
    // Just call deinit - NimBLE handles checking if already deinitialized
    NimBLEDevice::deinit();
    delay(100); // Small delay after deinit
    
    // Set disabled flag
    bleDisabled = true;
    deviceConnected = false;
    oldDeviceConnected = false;
    
    // Clear credentials
    receivedSSID = "";
    receivedPassword = "";
    receivedGPSActive = 0;
    receivedCycleTime = 900;
    credentialsReceived = false;
    gpsActiveReceived = false;
    cycleTimeReceived = false;
    
    Serial.println("BLE stopped and deinitialized (NimBLE can be safely re-initialized)");
  }
  
  // Check if BLE is enabled (not permanently disabled)
  static bool isEnabled() {
    return !bleDisabled;
  }
  
  // Get BLE RSSI signal strength (returns dBm, or -100 if not connected)
  static int getRSSI() {
    if (bleDisabled || !deviceConnected || pServer == nullptr) {
      return -100; // Return invalid RSSI if not connected
    }
    
    // Try to get RSSI using NimBLE
    if (pServer->getConnectedCount() > 0) {
      // For NimBLE, we can try to get RSSI from connection info
      // Return a reasonable default when connected (typical BLE connection RSSI)
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
bool BLEConfig::deviceConnected = false;
bool BLEConfig::oldDeviceConnected = false;
String BLEConfig::receivedSSID = "";
String BLEConfig::receivedPassword = "";
uint8_t BLEConfig::receivedGPSActive = 0;
uint32_t BLEConfig::receivedCycleTime = 900;
bool BLEConfig::credentialsReceived = false;
bool BLEConfig::gpsActiveReceived = false;
bool BLEConfig::cycleTimeReceived = false;
uint16_t BLEConfig::mtuSize = 23;
const char* BLEConfig::deviceId = nullptr;
const char* BLEConfig::deviceVersion = "v2.0.0";
bool BLEConfig::bleDisabled = false;

#endif
