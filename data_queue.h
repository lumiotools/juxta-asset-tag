#ifndef DATA_QUEUE_H
#define DATA_QUEUE_H

#include <Arduino.h>

class DataQueue {
private:
  static const int MAX_QUEUE_SIZE = 10;
  String dataQueue[MAX_QUEUE_SIZE];
  int queueLength = 0;

public:
  // Add JSON data to queue
  bool enqueue(String jsonData) {
    if (queueLength >= MAX_QUEUE_SIZE) {
      Serial.println("ERROR: Data queue is full!");
      return false;
    }
    dataQueue[queueLength] = jsonData;
    queueLength++;
    Serial.println("Data queued. Queue length: " + String(queueLength));
    return true;
  }

  // Remove data from queue (FIFO)
  String dequeue() {
    if (queueLength == 0) {
      return "";
    }
    String data = dataQueue[0];
    // Shift all items down
    for (int i = 0; i < queueLength - 1; i++) {
      dataQueue[i] = dataQueue[i + 1];
    }
    queueLength--;
    return data;
  }

  // Get current queue length
  int getLength() {
    return queueLength;
  }

  // Check if queue is empty
  bool isEmpty() {
    return queueLength == 0;
  }

  // Get queue length for reference
  int getMaxSize() {
    return MAX_QUEUE_SIZE;
  }

  // Create JSON array from queued data
  String createJSONArray() {
    String jsonArray = "[";
    
    for (int i = 0; i < queueLength; i++) {
      jsonArray += dataQueue[i];
      if (i < queueLength - 1) {
        jsonArray += ",";
      }
    }
    
    jsonArray += "]";
    return jsonArray;
  }

  // Clear entire queue
  void clear() {
    queueLength = 0;
    Serial.println("Data queue cleared");
  }

  // Print queue contents for debugging
  void printQueueStatus() {
    Serial.println("\n=== Data Queue Status ===");
    Serial.println("Queue Length: " + String(queueLength) + "/" + String(MAX_QUEUE_SIZE));
    
    if (queueLength == 0) {
      Serial.println("Queue is empty");
    } else {
      for (int i = 0; i < queueLength; i++) {
        Serial.println("Item " + String(i + 1) + " size: " + String(dataQueue[i].length()) + " bytes");
      }
    }
    Serial.println("========================\n");
  }
};

#endif
