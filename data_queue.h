#ifndef DATA_QUEUE_H
#define DATA_QUEUE_H

#include <Arduino.h>
#include <string.h>
#include "nvs_config.h"
#include <ArduinoJson.h>

class DataQueue {
private:
  int MAX_QUEUE_SIZE;
  String* dataQueue;
  int queueLength = 0;
  bool initialized = false;
  
  // Calculate max queue size based on available NVS space
  // Leaves buffer for: firstboot flag, SSID, password, max_queue_size, and other variables
  int calculateMaxQueueSize() {
    // Estimate space needed for other variables
    // - firstboot: ~32 bytes
    // - SSID: ~64 bytes (max 32 chars + overhead)
    // - password: ~64 bytes (max 32 chars + overhead)
    // - max_queue_size: ~32 bytes
    // - Buffer for other variables: ~256 bytes
    const int RESERVED_SPACE = 32 + 64 + 64 + 32 + 256; // ~448 bytes reserved
    
    // Get available NVS space
    int availableSpace = NVSConfig::getAvailableNVSSpace();
    
    if (availableSpace <= 0) {
      // Fallback to conservative estimate if we can't get stats
      // Assume we have at least 4KB available, reserve 448 bytes
      availableSpace = 4096 - RESERVED_SPACE;
    } else {
      // Use 80% of available space for queue (leave 20% buffer)
      availableSpace = (availableSpace * 80) / 100;
      availableSpace -= RESERVED_SPACE;
    }
    
    // Estimate size per queue item: ~1000 bytes per JSON item + overhead
    const int BYTES_PER_ITEM = 1050; // ~1000 bytes JSON + 50 bytes overhead
    
    // Calculate max items that fit
    int maxItems = availableSpace / BYTES_PER_ITEM;
    
    Serial.print("Calculated max queue size: ");
    Serial.print(maxItems);
    Serial.print(" items (available space: ");
    Serial.print(availableSpace);
    Serial.println(" bytes)");
    
    return maxItems;
  }
  
  // Save queue data to NVS
  void saveToNVS() {
    if (!initialized) return;
    
    // Create JSON array string
    String jsonArray = createJSONArray();
    
    // Save to NVS
    NVSConfig::setQueueData(jsonArray.c_str());
  }
  
  // Load queue data from NVS
  void loadFromNVS() {
    String queueData = NVSConfig::getQueueData();
    
    if (queueData.length() == 0) {
      queueLength = 0;
      return;
    }
    
    // Parse JSON array - buffer size based on max queue size
    // Estimate: ~850 bytes per item + overhead
    int docSize = (MAX_QUEUE_SIZE * 900) + 500; // Add 500 bytes overhead
    if (docSize > 50000) docSize = 50000; // Cap at 50KB to prevent excessive memory usage
    
    DynamicJsonDocument doc(docSize);
    DeserializationError error = deserializeJson(doc, queueData);
    
    if (error) {
      Serial.print("Failed to parse queue data from NVS: ");
      Serial.println(error.c_str());
      queueLength = 0;
      return;
    }
    
    // Load items into queue
    queueLength = 0;
    if (doc.is<JsonArray>()) {
      JsonArray arr = doc.as<JsonArray>();
      for (JsonVariant item : arr) {
        if (queueLength < MAX_QUEUE_SIZE && item.is<const char*>()) {
          dataQueue[queueLength] = String(item.as<const char*>());
          queueLength++;
        }
      }
    }
    
    Serial.print("Loaded ");
    Serial.print(queueLength);
    Serial.println(" items from NVS queue");
  }

public:
  // Constructor
  DataQueue() : MAX_QUEUE_SIZE(3), dataQueue(nullptr), queueLength(0), initialized(false) {
  }
  
  // Destructor
  ~DataQueue() {
    if (dataQueue != nullptr) {
      delete[] dataQueue;
      dataQueue = nullptr;
    }
  }
  
  // Initialize queue - must be called before use
  // Calculates max queue size on first boot
  bool begin() {
    if (initialized) {
      return true; // Already initialized
    }
    
    // Check if first boot
    bool firstBoot = NVSConfig::isFirstBoot();
    
    if (firstBoot) {
      // Calculate max queue size on first boot
      MAX_QUEUE_SIZE = calculateMaxQueueSize();
      
      // Save max queue size to NVS
      NVSConfig::setMaxQueueSize(MAX_QUEUE_SIZE);
      
      // Mark first boot as complete
      NVSConfig::setFirstBootComplete();
      
      Serial.println("First boot: Calculated and saved max queue size");
    } else {
      // Load max queue size from NVS
      int savedSize = NVSConfig::getMaxQueueSize();
      if (savedSize > 0) {
        MAX_QUEUE_SIZE = savedSize;
        Serial.print("Loaded max queue size from NVS: ");
        Serial.println(MAX_QUEUE_SIZE);
      } else {
        // Fallback if not found
        MAX_QUEUE_SIZE = 3;
        Serial.println("Max queue size not found in NVS, using default: 3");
      }
    }
    
    // Allocate queue array
    dataQueue = new String[MAX_QUEUE_SIZE];
    if (dataQueue == nullptr) {
      Serial.println("Failed to allocate queue memory!");
      return false;
    }
    
    // Load existing queue data from NVS into RAM (only on initialization)
    // After this, all operations work with RAM, only writing to NVS when data changes
    loadFromNVS();
    
    initialized = true;
    return true;
  }

public:
  // Add JSON data to queue
  bool enqueue(String jsonData) {
    if (!initialized) {
      Serial.println("Queue not initialized! Call begin() first.");
      return false;
    }
    
    if (queueLength >= MAX_QUEUE_SIZE) {
      return false;
    }
    dataQueue[queueLength] = jsonData;
    queueLength++;
    
    // Save to NVS after enqueue
    saveToNVS();
    
    return true;
  }

  // Remove data from queue (FIFO)
  String dequeue() {
    if (!initialized || queueLength == 0) {
      return "";
    }
    String data = dataQueue[0];
    // Shift all items down
    for (int i = 0; i < queueLength - 1; i++) {
      dataQueue[i] = dataQueue[i + 1];
    }
    queueLength--;
    
    // Save to NVS after dequeue
    saveToNVS();
    
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
    if (!initialized) {
      return "[]";
    }
    
    // Calculate required buffer size dynamically
    int totalSize = 2; // "[]"
    for (int i = 0; i < queueLength; i++) {
      totalSize += dataQueue[i].length();
      if (i > 0) totalSize++; // comma
    }
    
    // Allocate buffer (with some extra for safety)
    char* jsonBuffer = (char*)malloc(totalSize + 100);
    if (jsonBuffer == nullptr) {
      return "[]"; // Return empty array on allocation failure
    }
    
    int pos = 0;
    pos += snprintf(jsonBuffer + pos, totalSize + 100 - pos, "[");
    
    for (int i = 0; i < queueLength; i++) {
      const char* item = dataQueue[i].c_str();
      int itemLen = strlen(item);
      if (pos + itemLen + 2 < totalSize + 100) {
        if (i > 0) {
          jsonBuffer[pos++] = ',';
        }
        memcpy(jsonBuffer + pos, item, itemLen);
        pos += itemLen;
      }
    }
    
    jsonBuffer[pos++] = ']';
    jsonBuffer[pos] = '\0';
    
    String result = String(jsonBuffer);
    free(jsonBuffer);
    
    return result;
  }

  // Clear entire queue
  void clear() {
    if (!initialized) return;
    
    queueLength = 0;
    
    // Save to NVS after clear
    saveToNVS();
  }
};

#endif
