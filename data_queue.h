#ifndef DATA_QUEUE_H
#define DATA_QUEUE_H

#include <Arduino.h>
#include <string.h>

class DataQueue {
private:
  static const int MAX_QUEUE_SIZE = 3; // Reduced to save memory
  String dataQueue[MAX_QUEUE_SIZE];
  int queueLength = 0;

public:
  // Add JSON data to queue
  bool enqueue(String jsonData) {
    if (queueLength >= MAX_QUEUE_SIZE) {
      return false;
    }
    dataQueue[queueLength] = jsonData;
    queueLength++;
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
    static char jsonBuffer[2500]; // Reduced: max 3 items * ~800 bytes
    int pos = 0;
    
    pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, "[");
    
    for (int i = 0; i < queueLength; i++) {
      const char* item = dataQueue[i].c_str();
      int itemLen = strlen(item);
      if (pos + itemLen + 2 < sizeof(jsonBuffer)) {
        if (i > 0) {
          jsonBuffer[pos++] = ',';
        }
        memcpy(jsonBuffer + pos, item, itemLen);
        pos += itemLen;
      }
    }
    
    jsonBuffer[pos++] = ']';
    jsonBuffer[pos] = '\0';
    
    return String(jsonBuffer);
  }

  // Clear entire queue
  void clear() {
    queueLength = 0;
  }
};

#endif
