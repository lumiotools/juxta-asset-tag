#ifndef TRANSMISSION_HANDLER_H
#define TRANSMISSION_HANDLER_H

#include "data_queue.h"
#include "customwifi.h"

class TransmissionHandler {
private:
  DataQueue dataQueue;
  bool transmissionInProgress = false;
  unsigned long transmissionStartTime = 0;
  const unsigned long TRANSMISSION_TIMEOUT = 10000; // 10 seconds

public:
  // Check transmission status and handle accordingly
  // Returns: true if data sent successfully, false if failed or in progress
  bool handleDataTransmission(String currentJSON) {
    // If queue is empty, send only current data
    if (dataQueue.isEmpty()) {
      Serial.println("\n--- Transmission: Queue Empty (sending single data) ---");
      bool success = CustomWiFi::sendSensorData(currentJSON);
      
      if (!success) {
        Serial.println("FAILED: Single transmission failed. Adding to queue.");
        dataQueue.enqueue(currentJSON);
      } else {
        Serial.println("SUCCESS: Single data transmitted");
      }
      return success;
    }

    // If queue has 1 object: append current data and send as array
    else if (dataQueue.getLength() == 1) {
      Serial.println("\n--- Transmission: Queue has 1 item (creating array with 2 items) ---");
      dataQueue.enqueue(currentJSON);
      
      String jsonArray = dataQueue.createJSONArray();
      Serial.println("Sending array of " + String(dataQueue.getLength()) + " items");
      Serial.println("Array size: " + String(jsonArray.length()) + " bytes");
      
      bool success = CustomWiFi::sendSensorData(jsonArray);
      
      if (!success) {
        Serial.println("FAILED: Array transmission failed. Keeping items in queue.");
        // Remove the last item (current data) that we just added
        dataQueue.dequeue();
      } else {
        Serial.println("SUCCESS: Array data transmitted. Clearing queue.");
        dataQueue.clear();
      }
      return success;
    }

    // If queue has more than 1 object: need BLE fallback
    else {
      Serial.println("\n!!! WARNING: Queue has " + String(dataQueue.getLength()) + " items !!!");
      Serial.println("!!! NEED BLE HERE - Queue is accumulating failed transmissions !!!");
      dataQueue.printQueueStatus();
      
      // Still try to add current data to queue if space available
      if (dataQueue.getLength() < dataQueue.getMaxSize()) {
        dataQueue.enqueue(currentJSON);
        Serial.println("Current data added to queue for later retry");
      } else {
        Serial.println("ERROR: Queue is full! Data cannot be added.");
      }
      
      return false;
    }
  }

  // Get reference to data queue for external monitoring
  DataQueue* getDataQueue() {
    return &dataQueue;
  }

  // Get queue status
  void printStatus() {
    dataQueue.printQueueStatus();
  }
};

#endif
