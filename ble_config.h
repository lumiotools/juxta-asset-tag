// BLE Configuration for WiFi Credential Setup

#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "nvs_config.h"
#include <Ticker.h>

// BLE Service and Characteristic UUIDs
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define SSID_CHAR_UUID      "12345678-1234-1234-1234-123456789abd"
#define PASSWORD_CHAR_UUID  "12345678-1234-1234-1234-123456789abe"
#define STATUS_CHAR_UUID    "12345678-1234-1234-1234-123456789abf"
#define DATA_CHAR_UUID      "12345678-1234-1234-1234-123456789ac0"
#define CURRENT_SSID_CHAR_UUID "12345678-1234-1234-1234-123456789ac1"

// BLE Device Name
#define BLE_DEVICE_NAME     "AssetTag-Config"

class BLEConfig {
private:
  static BLEServer* pServer;
  static BLEService* pService;
  static BLECharacteristic* pSSIDCharacteristic;
  static BLECharacteristic* pPasswordCharacteristic;
  static BLECharacteristic* pStatusCharacteristic;
  static BLECharacteristic* pDataCharacteristic;
  static BLECharacteristic* pCurrentSSIDCharacteristic;
  static bool deviceConnected;
  static bool oldDeviceConnected;
  static String receivedSSID;
  static String receivedPassword;
  static bool credentialsReceived;
  static uint16_t mtuSize;
  
  // BLE LED Configuration
  static int bleLedPin;
  static Ticker* bleLedTicker;
  static volatile bool bleLedState;
  
  static void toggleBLELED() {
    if (bleLedPin >= 0) {
      bleLedState = !bleLedState;
      digitalWrite(bleLedPin, bleLedState);
    }
  }

  // BLE Server Callbacks
  class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      mtuSize = 23; // Default MTU size, will be updated after negotiation
      
      // Load and send current SSID when client connects
      String currentSSID = NVSConfig::getWiFiSSID();
      if (pCurrentSSIDCharacteristic != nullptr) {
        if (currentSSID.length() > 0) {
          pCurrentSSIDCharacteristic->setValue(currentSSID.c_str());
        } else {
          pCurrentSSIDCharacteristic->setValue("Not configured");
        }
      }
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
  };

  // SSID Characteristic Callbacks
  class SSIDCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
      // Start LED blinking on receive
      if (bleLedPin >= 0 && bleLedTicker != nullptr) {
        bleLedState = false;
        bleLedTicker->attach_ms(20, toggleBLELED);
      }
      
      String value = pCharacteristic->getValue();
      if (value.length() > 0) {
        receivedSSID = value;
      }
      
      // Stop LED blinking
      if (bleLedPin >= 0 && bleLedTicker != nullptr) {
        bleLedTicker->detach();
        digitalWrite(bleLedPin, LOW);
      }
    }
  };

  // Password Characteristic Callbacks
  class PasswordCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
      // Start LED blinking on receive
      if (bleLedPin >= 0 && bleLedTicker != nullptr) {
        bleLedState = false;
        bleLedTicker->attach_ms(20, toggleBLELED);
      }
      
      String value = pCharacteristic->getValue();
      if (value.length() > 0) {
        receivedPassword = value;
        if (receivedSSID.length() > 0) {
          credentialsReceived = true;
        }
      }
      
      // Stop LED blinking
      if (bleLedPin >= 0 && bleLedTicker != nullptr) {
        bleLedTicker->detach();
        digitalWrite(bleLedPin, LOW);
      }
    }
  };

public:
  // Set BLE LED Pin
  static void setBLELEDPin(int pin) {
    bleLedPin = pin;
    if (pin >= 0) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
      bleLedTicker = new Ticker();
    }
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
    credentialsReceived = false;
    mtuSize = 23; // Default BLE MTU size
    
    return true;
  }
  
  // Update BLE (call this in loop to handle connections and process received data)
  static void update() {
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
    
    // Process received credentials
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
  }
  
  // Check if BLE is connected
  static bool isConnected() {
    return deviceConnected;
  }
  
  // Restart advertising
  static void restartAdvertising() {
    if (!deviceConnected) {
      BLEDevice::startAdvertising();
    }
  }
  
  // Send sensor data via BLE (with chunking for large data)
  static bool sendDataViaBLE(const String& jsonData) {
    if (!deviceConnected || pDataCharacteristic == nullptr) {
      return false;
    }
    
    // Start LED blinking on transmit
    if (bleLedPin >= 0 && bleLedTicker != nullptr) {
      bleLedState = false;
      bleLedTicker->attach_ms(20, toggleBLELED);
    }
    
    // Calculate safe chunk size (MTU - 3 bytes for ATT header)
    uint16_t maxChunkSize = (mtuSize > 23) ? (mtuSize - 3) : 20;
    
    // If data fits in one chunk, send directly
    if (jsonData.length() <= maxChunkSize) {
      pDataCharacteristic->setValue(jsonData.c_str());
      pDataCharacteristic->notify();
      delay(50); // Give BLE stack time to process
    } else {
      // Send data in chunks
      int totalLength = jsonData.length();
      int offset = 0;
      
      while (offset < totalLength) {
        int chunkSize = min((int)maxChunkSize, totalLength - offset);
        String chunk = jsonData.substring(offset, offset + chunkSize);
        
        pDataCharacteristic->setValue(chunk.c_str());
        pDataCharacteristic->notify();
        
        offset += chunkSize;
        delay(50); // Give BLE stack time between chunks
      }
    }
    
    // Stop LED blinking
    if (bleLedPin >= 0 && bleLedTicker != nullptr) {
      bleLedTicker->detach();
      digitalWrite(bleLedPin, LOW);
    }
    
    return true;
  }
};

// Static member definitions
BLEServer* BLEConfig::pServer = nullptr;
BLEService* BLEConfig::pService = nullptr;
BLECharacteristic* BLEConfig::pSSIDCharacteristic = nullptr;
BLECharacteristic* BLEConfig::pPasswordCharacteristic = nullptr;
BLECharacteristic* BLEConfig::pStatusCharacteristic = nullptr;
BLECharacteristic* BLEConfig::pDataCharacteristic = nullptr;
BLECharacteristic* BLEConfig::pCurrentSSIDCharacteristic = nullptr;
bool BLEConfig::deviceConnected = false;
bool BLEConfig::oldDeviceConnected = false;
String BLEConfig::receivedSSID = "";
String BLEConfig::receivedPassword = "";
bool BLEConfig::credentialsReceived = false;
uint16_t BLEConfig::mtuSize = 23;
int BLEConfig::bleLedPin = -1;
Ticker* BLEConfig::bleLedTicker = nullptr;
volatile bool BLEConfig::bleLedState = false;

#endif

