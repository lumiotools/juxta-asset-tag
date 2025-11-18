#ifndef TRANSMISSION_HANDLER_H
#define TRANSMISSION_HANDLER_H

#include "data_queue.h"
#include "customwifi.h"
#include "ble_config.h"

class TransmissionHandler {
private:
  DataQueue dataQueue;

  // Unified send function - tries both WiFi and BLE
  bool sendData(const String& data) {
    bool wifiSuccess = false;
    bool bleSuccess = false;
    
    // Try BLE transmission if connected
    if (BLEConfig::isConnected()) {
      bleSuccess = BLEConfig::sendDataViaBLE(data);
    }

    if (!bleSuccess) {
      // Try WiFi transmission
      wifiSuccess = CustomWiFi::sendSensorData(data);
    }
    
    // Consider successful if either method worked
    return (wifiSuccess || bleSuccess);
  }

public:
  // Check transmission status and handle accordingly
  // Always sends data as array format (single item or multiple items)
  // Returns: true if data sent successfully (via WiFi or BLE), false if failed or in progress
  bool handleDataTransmission(String currentJSON) {
    // Always add current data to queue first
    dataQueue.enqueue(currentJSON);
    
    // Create JSON array from queue (1 or more items)
    String jsonArray = dataQueue.createJSONArray();
    
    // Try to send the array
    bool success = sendData(jsonArray);
    
    if (success) {
      // Clear queue on successful transmission
      dataQueue.clear();
    } else {
      // Remove the current item we just added if sending failed
      // Keep it in queue for next transmission attempt
      // Queue now contains the current item that failed to send
    }
    
    return success;
  }

  // Get reference to data queue for external monitoring
  DataQueue* getDataQueue() {
    return &dataQueue;
  }

};

#endif
