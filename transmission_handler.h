#ifndef TRANSMISSION_HANDLER_H
#define TRANSMISSION_HANDLER_H

#include "data_queue.h"
#include "customwifi.h"

class TransmissionHandler {
private:
  DataQueue dataQueue;

public:
  // Check transmission status and handle accordingly
  // Returns: true if data sent successfully, false if failed or in progress
  bool handleDataTransmission(String currentJSON) {
    // If queue is empty, send only current data
    if (dataQueue.isEmpty()) {
      bool success = CustomWiFi::sendSensorData(currentJSON);
      
      if (!success) {
        dataQueue.enqueue(currentJSON);
      }
      return success;
    }

    // If queue has 1 object: append current data and send as array
    else if (dataQueue.getLength() == 1) {
      dataQueue.enqueue(currentJSON);
      
      String jsonArray = dataQueue.createJSONArray();
      
      bool success = CustomWiFi::sendSensorData(jsonArray);
      
      if (!success) {
        // Remove the last item (current data) that we just added
        dataQueue.dequeue();
      } else {
        dataQueue.clear();
      }
      return success;
    }

    // If queue has more than 1 object: need BLE fallback
    else {
      // Still try to add current data to queue if space available
      if (dataQueue.getLength() < dataQueue.getMaxSize()) {
        dataQueue.enqueue(currentJSON);
      }
      
      return false;
    }
  }

  // Get reference to data queue for external monitoring
  DataQueue* getDataQueue() {
    return &dataQueue;
  }

};

#endif
