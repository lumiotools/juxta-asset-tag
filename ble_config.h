/*
 * BLE Configuration Module for WiFi Credential Setup
 * 
 * This module provides BLE (Bluetooth Low Energy) functionality for receiving
 * WiFi SSID and password from a mobile app or BLE client. The BLE server
 * continuously advertises and accepts connections to configure WiFi credentials.
 * 
 * Features:
 * - Always-on BLE advertising (device name: "AssetTag-Config")
 * - Two writable characteristics for SSID and Password
 * - Status characteristic for feedback
 * - Automatic saving to NVS storage
 * - Handles connection/disconnection automatically
 * 
 * Usage:
 * 1. Call BLEConfig::begin() in setup() to initialize BLE
 * 2. Call BLEConfig::update() in loop() to handle BLE operations
 * 3. Connect with a BLE client (mobile app) and write SSID and Password
 * 4. Credentials are automatically saved to NVS when both are received
 * 
 * BLE Service UUID: 12345678-1234-1234-1234-123456789abc
 * SSID Characteristic UUID: 12345678-1234-1234-1234-123456789abd
 * Password Characteristic UUID: 12345678-1234-1234-1234-123456789abe
 * Status Characteristic UUID: 12345678-1234-1234-1234-123456789abf
 */

#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "nvs_config.h"

// BLE Service and Characteristic UUIDs
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define SSID_CHAR_UUID      "12345678-1234-1234-1234-123456789abd"
#define PASSWORD_CHAR_UUID  "12345678-1234-1234-1234-123456789abe"
#define STATUS_CHAR_UUID    "12345678-1234-1234-1234-123456789abf"

// BLE Device Name
#define BLE_DEVICE_NAME     "AssetTag-Config"

class BLEConfig {
private:
  static BLEServer* pServer;
  static BLEService* pService;
  static BLECharacteristic* pSSIDCharacteristic;
  static BLECharacteristic* pPasswordCharacteristic;
  static BLECharacteristic* pStatusCharacteristic;
  static bool deviceConnected;
  static bool oldDeviceConnected;
  static String receivedSSID;
  static String receivedPassword;
  static bool credentialsReceived;

  // BLE Server Callbacks
  class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("BLE Client connected");
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("BLE Client disconnected");
    }
  };

  // SSID Characteristic Callbacks
  class SSIDCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      if (value.length() > 0) {
        receivedSSID = String(value.c_str());
        Serial.println("BLE SSID received: " + receivedSSID);
        pStatusCharacteristic->setValue("SSID received");
        pStatusCharacteristic->notify();
      }
    }
  };

  // Password Characteristic Callbacks
  class PasswordCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      if (value.length() > 0) {
        receivedPassword = String(value.c_str());
        Serial.println("BLE Password received (length: " + String(value.length()) + ")");
        pStatusCharacteristic->setValue("Password received");
        pStatusCharacteristic->notify();
        
        // Check if both SSID and password are received
        if (receivedSSID.length() > 0 && receivedPassword.length() > 0) {
          credentialsReceived = true;
        }
      }
    }
  };

public:
  // Initialize BLE and start advertising
  static bool begin() {
    Serial.println("Initializing BLE...");
    
    // Initialize BLE Device
    BLEDevice::init(BLE_DEVICE_NAME);
    
    // Create BLE Server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    
    // Create BLE Service
    pService = pServer->createService(SERVICE_UUID);
    
    // Create SSID Characteristic (for receiving WiFi SSID)
    pSSIDCharacteristic = pService->createCharacteristic(
      SSID_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_NOTIFY
    );
    pSSIDCharacteristic->setCallbacks(new SSIDCallbacks());
    pSSIDCharacteristic->setValue("Send SSID here");
    
    // Create Password Characteristic (for receiving WiFi Password)
    pPasswordCharacteristic = pService->createCharacteristic(
      PASSWORD_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_NOTIFY
    );
    pPasswordCharacteristic->setCallbacks(new PasswordCallbacks());
    pPasswordCharacteristic->setValue("Send Password here");
    
    // Create Status Characteristic (for sending status updates)
    pStatusCharacteristic = pService->createCharacteristic(
      STATUS_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_NOTIFY
    );
    pStatusCharacteristic->setValue("Ready for WiFi credentials");
    
    // Add descriptors
    pSSIDCharacteristic->addDescriptor(new BLE2902());
    pPasswordCharacteristic->addDescriptor(new BLE2902());
    pStatusCharacteristic->addDescriptor(new BLE2902());
    
    // Start the service
    pService->start();
    
    // Start advertising (always on)
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    
    Serial.println("BLE initialized and advertising as: " + String(BLE_DEVICE_NAME));
    Serial.println("Waiting for WiFi credentials via BLE...");
    
    deviceConnected = false;
    oldDeviceConnected = false;
    receivedSSID = "";
    receivedPassword = "";
    credentialsReceived = false;
    
    return true;
  }
  
  // Update BLE (call this in loop to handle connections and process received data)
  static void update() {
    // Handle disconnection
    if (!deviceConnected && oldDeviceConnected) {
      delay(500); // Give the bluetooth stack the chance to get things ready
      pServer->startAdvertising(); // Restart advertising
      Serial.println("BLE: Restarting advertising");
      oldDeviceConnected = deviceConnected;
    }
    
    // Handle connection
    if (deviceConnected && !oldDeviceConnected) {
      oldDeviceConnected = deviceConnected;
    }
    
    // Process received credentials
    if (credentialsReceived) {
      Serial.println("\n=== Processing BLE WiFi Credentials ===");
      Serial.println("SSID: " + receivedSSID);
      Serial.println("Password length: " + String(receivedPassword.length()));
      
      // Save to NVS
      bool ssidSaved = NVSConfig::setWiFiSSID(receivedSSID.c_str());
      bool passwordSaved = NVSConfig::setWiFiPassword(receivedPassword.c_str());
      
      if (ssidSaved && passwordSaved) {
        Serial.println("SUCCESS: WiFi credentials saved to NVS!");
        pStatusCharacteristic->setValue("Credentials saved successfully!");
        pStatusCharacteristic->notify();
      } else {
        Serial.println("ERROR: Failed to save WiFi credentials");
        pStatusCharacteristic->setValue("Error saving credentials");
        pStatusCharacteristic->notify();
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
  
  // Get current SSID (if received)
  static String getReceivedSSID() {
    return receivedSSID;
  }
  
  // Get current password (if received)
  static String getReceivedPassword() {
    return receivedPassword;
  }
  
  // Send status message via BLE
  static void sendStatus(const char* message) {
    if (deviceConnected && pStatusCharacteristic != nullptr) {
      pStatusCharacteristic->setValue(message);
      pStatusCharacteristic->notify();
    }
  }
  
  // Restart advertising (useful if advertising stops)
  static void restartAdvertising() {
    if (!deviceConnected) {
      BLEDevice::startAdvertising();
      Serial.println("BLE: Advertising restarted");
    }
  }
};

// Static member definitions
BLEServer* BLEConfig::pServer = nullptr;
BLEService* BLEConfig::pService = nullptr;
BLECharacteristic* BLEConfig::pSSIDCharacteristic = nullptr;
BLECharacteristic* BLEConfig::pPasswordCharacteristic = nullptr;
BLECharacteristic* BLEConfig::pStatusCharacteristic = nullptr;
bool BLEConfig::deviceConnected = false;
bool BLEConfig::oldDeviceConnected = false;
String BLEConfig::receivedSSID = "";
String BLEConfig::receivedPassword = "";
bool BLEConfig::credentialsReceived = false;

#endif

