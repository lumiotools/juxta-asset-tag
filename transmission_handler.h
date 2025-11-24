#ifndef TRANSMISSION_HANDLER_H
#define TRANSMISSION_HANDLER_H

#include "data_queue.h"
#include "customwifi.h"
#include "ble_config.h"

class TransmissionHandler {
private:
  DataQueue dataQueue;

  // Unified send function - tries both WiFi and BLE
  // Only attempts transmission if WiFi is connected or BLE is connected
  bool sendData(const String& data) {
    bool wifiSuccess = false;
    bool bleSuccess = false;
    
    // Try BLE transmission if connected
    if (BLEConfig::isConnected()) {
      bleSuccess = BLEConfig::sendDataViaBLE(data);
    }

    // Only try WiFi if BLE didn't succeed and WiFi is connected
    if (!bleSuccess && CustomWiFi::isConnected()) {
      wifiSuccess = CustomWiFi::sendSensorData(data);
    }
    
    // Consider successful if either method worked
    return (wifiSuccess || bleSuccess);
  }

public:
  // Initialize the data queue (must be called before use)
  bool begin() {
    return dataQueue.begin();
  }

  // Check transmission status and handle accordingly
  // Always sends data as CSV array format (single item or multiple items)
  // Returns: true if data sent successfully (via WiFi or BLE), false if saved to queue or failed
  bool handleDataTransmission(String currentCSV) {
    // Always add current data to queue first
    dataQueue.enqueue(currentCSV);
    
    // Check if WiFi or BLE is connected before attempting transmission
    bool wifiConnected = CustomWiFi::isConnected();
    bool bleConnected = BLEConfig::isConnected();
    
    // Only attempt to send if WiFi or BLE is connected
    if (wifiConnected || bleConnected) {
      // Create CSV string from queue (1 or more items, newline separated)
      String csvData = dataQueue.createCSVString();
      
      // Try to send the CSV data
      bool success = sendData(csvData);
      
      if (success) {
        // Clear queue on successful transmission
        dataQueue.clear();
        return true;
      } else {
        // Transmission failed, data stays in queue for next attempt
        return false;
      }
    } else {
      // WiFi and BLE not connected, save to queue and don't attempt transmission
      return false;
    }
  }

  // Get reference to data queue for external monitoring
  DataQueue* getDataQueue() {
    return &dataQueue;
  }

};

#endif
