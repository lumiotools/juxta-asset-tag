#ifndef DATA_QUEUE_H
#define DATA_QUEUE_H

#include <Arduino.h>
#include <string.h>
#include "nvs_config.h"
// #include "spi_flash_handler.h"  // COMMENTED OUT: Using internal flash instead

// External LED blink functions from main.ino
extern long long startStatusLEDBlink(uint8_t r, uint8_t g, uint8_t b);
extern void stopStatusLEDBlink(long long startTime);  

class DataQueue {
private:
  bool initialized = false;
  // SPIFlashHandler* flashHandler = nullptr;  // COMMENTED OUT: No longer using external flash
  
  // Internal flash queue parameters
  // Using NVS string storage (max ~4000 bytes per key, using 3000 for safety)
  const size_t MAX_QUEUE_SIZE_BYTES = 1000000;  // Max queue size in internal flash
  String queueData;  // In-memory queue buffer
  uint32_t writePtr;  // Write pointer (byte offset in queueData)
  uint32_t readPtr;   // Read pointer (byte offset in queueData)
  
  const size_t BATCH_SIZE = 200;
  
  // Load queue data from NVS
  bool loadQueueFromNVS() {
    String storedData = NVSConfig::getQueueData();
    if (storedData.length() > MAX_QUEUE_SIZE_BYTES) {
      Serial.println("WARNING: Stored queue data exceeds max size, truncating");
      storedData = storedData.substring(0, MAX_QUEUE_SIZE_BYTES);
    }
    queueData = storedData;
    return true;
  }
  
  // Save queue data to NVS
  bool saveQueueToNVS() {
    if (queueData.length() > MAX_QUEUE_SIZE_BYTES) {
      Serial.println("WARNING: Queue data exceeds max size, truncating before save");
      queueData = queueData.substring(0, MAX_QUEUE_SIZE_BYTES);
    }
    return NVSConfig::setQueueData(queueData.c_str());
  }

public:
  DataQueue() : initialized(false), 
                writePtr(0), readPtr(0) {
    queueData.reserve(MAX_QUEUE_SIZE_BYTES + 100);  // Reserve memory
  }
  
  ~DataQueue() {}
  
  bool begin(void* handler) {  // handler parameter kept for compatibility but ignored
    if (initialized) return true;

    // handler is ignored - we use internal flash (NVS)
    // if (handler == nullptr) {
    //   Serial.println("DataQueue: Handler parameter ignored (using internal flash)");
    // }
    
    // Check if first boot
    bool firstBoot = NVSConfig::isFirstBoot();
    
    Serial.println("========== DataQueue Initialization (Internal Flash) ==========");
    Serial.print("First boot flag: ");
    Serial.println(firstBoot ? "YES (first time)" : "NO (subsequent boot)");
    Serial.print("Max queue size: ");
    Serial.print(MAX_QUEUE_SIZE_BYTES);
    Serial.println(" bytes");
    
    if (firstBoot) {
      // Initialize queue to empty
      queueData = "";
      writePtr = 0;
      readPtr = 0;
      
      Serial.println("FIRST BOOT: Initializing queue to empty");
      Serial.print("  Setting writePtr = ");
      Serial.println(writePtr);
      Serial.print("  Setting readPtr = ");
      Serial.println(readPtr);
      
      if (!saveQueueToNVS()) {
        Serial.println("  ERROR: Failed to save initial queue data!");
      } else {
        Serial.println("  Queue data saved to NVS");
      }
      
      if (!NVSConfig::setQueueWritePtr(writePtr)) {
        Serial.println("  ERROR: Failed to save initial write pointer!");
      } else {
        Serial.println("  Write pointer saved to NVS");
      }
      
      if (!NVSConfig::setQueueReadPtr(readPtr)) {
        Serial.println("  ERROR: Failed to save initial read pointer!");
      } else {
        Serial.println("  Read pointer saved to NVS");
      }
      
      // Mark first boot as complete
      if (!NVSConfig::setFirstBootComplete()) {
        Serial.println("  ERROR: Failed to set first boot complete flag!");
      } else {
        Serial.println("  First boot flag set to complete");
      }
    } else {
      // Load queue data and pointers from NVS
      Serial.println("SUBSEQUENT BOOT: Loading queue from NVS");
      
      if (!loadQueueFromNVS()) {
        Serial.println("  ERROR: Failed to load queue data from NVS!");
        queueData = "";
      } else {
        Serial.print("  Loaded queue data: ");
        Serial.print(queueData.length());
        Serial.println(" bytes");
      }
      
      uint32_t loadedWritePtr = NVSConfig::getQueueWritePtr();
      uint32_t loadedReadPtr = NVSConfig::getQueueReadPtr();
      
      Serial.print("  Loaded writePtr from NVS: ");
      Serial.println(loadedWritePtr);
      Serial.print("  Loaded readPtr from NVS: ");
      Serial.println(loadedReadPtr);
      
      // Validate pointers are within bounds
      if (loadedWritePtr > queueData.length()) {
        Serial.print("  ERROR: Write pointer ");
        Serial.print(loadedWritePtr);
        Serial.print(" exceeds queue length ");
        Serial.print(queueData.length());
        Serial.println("! Resetting to queue end.");
        writePtr = queueData.length();
      } else {
        writePtr = loadedWritePtr;
        Serial.print("  Write pointer validated: ");
        Serial.println(writePtr);
      }
      
      if (loadedReadPtr > queueData.length()) {
        Serial.print("  ERROR: Read pointer ");
        Serial.print(loadedReadPtr);
        Serial.print(" exceeds queue length ");
        Serial.print(queueData.length());
        Serial.println("! Resetting to queue start.");
        readPtr = 0;
      } else {
        readPtr = loadedReadPtr;
        Serial.print("  Read pointer validated: ");
        Serial.println(readPtr);
      }
      
      // Calculate and display queued data
      int queuedBytes = 0;
      if (writePtr >= readPtr) {
        queuedBytes = writePtr - readPtr;
      } else {
        // Should not happen with linear buffer, but handle it
        queuedBytes = 0;
      }
      
      Serial.print("  Queued data detected: ");
      Serial.print(queuedBytes);
      Serial.println(" bytes");
    }
    
    Serial.println("========== Queue Initialization Complete ==========");
    
    initialized = true;
    return true;
  }

  bool enqueue(String csvData) {
    if (!initialized) {
      Serial.println("Queue not initialized! Call begin() first.");
      return false;
    }
    
    if (!csvData.endsWith("\n")) csvData += "\n";
    
    size_t dataLen = csvData.length();
    
    // Check if adding this data would exceed max size
    size_t currentSize = queueData.length();
    size_t newSize = (writePtr > currentSize) ? writePtr + dataLen : currentSize + dataLen;
    
    if (newSize > MAX_QUEUE_SIZE_BYTES) {
      // Queue is full - need to remove old data
      Serial.println("WARNING: Queue full, removing oldest data");
      
      // Remove data up to readPtr if readPtr > 0
      if (readPtr > 0) {
        queueData = queueData.substring(readPtr);
        writePtr -= readPtr;
        readPtr = 0;
      } else {
        // No data to remove, queue is truly full
        Serial.println("ERROR: Queue is completely full, cannot add more data!");
        return false;
      }
      
      // Recalculate new size
      currentSize = queueData.length();
      newSize = currentSize + dataLen;
      
      if (newSize > MAX_QUEUE_SIZE_BYTES) {
        Serial.println("ERROR: Data too large to fit in queue even after cleanup!");
        return false;
      }
    }
    
    // Append data at writePtr position
    if (writePtr >= queueData.length()) {
      // Append to end
      queueData += csvData;
      writePtr = queueData.length();
    } else {
      // Insert at writePtr (shouldn't normally happen, but handle it)
      queueData = queueData.substring(0, writePtr) + csvData + queueData.substring(writePtr);
      writePtr += dataLen;
    }
    
    Serial.print("Writing ");
    Serial.print(dataLen);
    Serial.print(" bytes to queue (writePtr=");
    Serial.print(writePtr);
    Serial.println(")");
    
    long long startTime = startStatusLEDBlink(150, 75, 0);
    
    // Save to NVS
    if (!saveQueueToNVS()) {
      Serial.println("ERROR: Failed to save queue data to NVS!");
      stopStatusLEDBlink(startTime);
      return false;
    }
    
    Serial.println("Queue write successful (saved to internal flash)");
    stopStatusLEDBlink(startTime);
    
    // Save write pointer
    if (!NVSConfig::setQueueWritePtr(writePtr)) {
      Serial.println("ERROR: Failed to save write pointer to NVS!");
    } else {
      Serial.println("Write pointer saved successfully");
    }
    
    return true;
  }

  String readBatch() {
    if (!initialized) {
      Serial.println("ERROR: readBatch() called but queue not initialized");
      return "";
    }
    
    if (readPtr >= writePtr) {
      Serial.println("readBatch: Queue is empty (readPtr >= writePtr)");
      return "";
    }
    
    if (readPtr >= queueData.length()) {
      Serial.println("readBatch: Read pointer beyond queue data length");
      return "";
    }
    
    // Calculate how much data to read
    size_t availableBytes = writePtr - readPtr;
    size_t bytesToRead = (availableBytes < BATCH_SIZE) ? availableBytes : BATCH_SIZE;
    
    Serial.print("readBatch: Reading ");
    Serial.print(bytesToRead);
    Serial.print(" bytes from position ");
    Serial.print(readPtr);
    Serial.println();
    
    // Extract data from queue
    String result = queueData.substring(readPtr, readPtr + bytesToRead);
    
    // Find last complete line (end with newline)
    int lastNewline = result.lastIndexOf('\n');
    if (lastNewline >= 0) {
      result = result.substring(0, lastNewline + 1);
      Serial.print("readBatch: Returning ");
      Serial.print(result.length());
      Serial.println(" bytes (complete lines)");
    } else if (result.length() > 0) {
      Serial.print("readBatch: WARNING - No newline found, returning ");
      Serial.print(result.length());
      Serial.println(" bytes (no newline)");
    }
    
    return result;
  }

  bool commitRead(const String& sentData) {
    if (!initialized || sentData.length() == 0) return false;
    
    size_t bytesToAdvance = sentData.length();
    Serial.print("commitRead: Advancing read pointer by ");
    Serial.print(bytesToAdvance);
    Serial.println(" bytes");
    
    readPtr += bytesToAdvance;
    
    // Ensure readPtr doesn't exceed writePtr
    if (readPtr > writePtr) {
      Serial.println("WARNING: readPtr exceeded writePtr, resetting to writePtr");
      readPtr = writePtr;
    }
    
    Serial.print("commitRead: New read pointer: ");
    Serial.println(readPtr);
    
    // Clean up data before readPtr if we've read a significant amount
    if (readPtr > 100 && readPtr > queueData.length() / 2) {
      // Remove processed data to free memory
      queueData = queueData.substring(readPtr);
      writePtr -= readPtr;
      readPtr = 0;
      
      // Save updated queue
      saveQueueToNVS();
    }
    
    if (!NVSConfig::setQueueReadPtr(readPtr)) {
      Serial.println("commitRead: ERROR - Failed to save read pointer to NVS!");
      return false;
    }
    
    Serial.println("commitRead: Read pointer saved successfully");
    return true;
  }

  int getLength() {
    if (!initialized) return 0;
    if (writePtr >= readPtr) {
      return (writePtr - readPtr) / 350;  // Approximate entries (350 bytes per entry)
    } else {
      return 0;
    }
  }

  bool isInitialized() { return initialized; }
  
  bool isEmpty() {
    if (!initialized) return true;
    return (readPtr >= writePtr);
  }

  int getMaxSize() { return MAX_QUEUE_SIZE_BYTES; }

  void clear() {
    if (!initialized) return;
    
    Serial.println("Clearing queue...");
    queueData = "";
    readPtr = 0;
    writePtr = 0;
    NVSConfig::setQueueReadPtr(readPtr);
    NVSConfig::setQueueWritePtr(writePtr);
    saveQueueToNVS();
    Serial.println("Queue cleared");
  }
  
  void forceSavePointers() {
    if (!initialized) {
      Serial.println("forceSavePointers: Queue not initialized");
      return;
    }
    
    Serial.println("========== Force Saving Queue State ==========");
    Serial.print("Current writePtr: ");
    Serial.println(writePtr);
    Serial.print("Current readPtr: ");
    Serial.println(readPtr);
    Serial.print("Queue data length: ");
    Serial.println(queueData.length());
    
    bool queueSaved = saveQueueToNVS();
    bool writePtrSaved = NVSConfig::setQueueWritePtr(writePtr);
    bool readPtrSaved = NVSConfig::setQueueReadPtr(readPtr);
    
    if (queueSaved) {
      Serial.println("Queue data saved to NVS successfully");
    } else {
      Serial.println("ERROR: Failed to save queue data!");
    }
    
    if (writePtrSaved) {
      Serial.println("Write pointer saved to NVS successfully");
    } else {
      Serial.println("ERROR: Failed to save write pointer!");
    }
    
    if (readPtrSaved) {
      Serial.println("Read pointer saved to NVS successfully");
    } else {
      Serial.println("ERROR: Failed to save read pointer!");
    }
    
    // Verify by reading back
    String verifyQueue = NVSConfig::getQueueData();
    uint32_t verifyWrite = NVSConfig::getQueueWritePtr();
    uint32_t verifyRead = NVSConfig::getQueueReadPtr();
    
    Serial.print("Verification - queue data length read back: ");
    Serial.println(verifyQueue.length());
    Serial.print("Verification - writePtr read back: ");
    Serial.println(verifyWrite);
    Serial.print("Verification - readPtr read back: ");
    Serial.println(verifyRead);
    
    if (verifyWrite == writePtr && verifyRead == readPtr) {
      Serial.println("SUCCESS: All pointers verified!");
    } else {
      Serial.println("WARNING: Pointer verification mismatch!");
    }
    Serial.println("================================================");
  }
};

#endif
