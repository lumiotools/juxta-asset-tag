#ifndef DATA_QUEUE_H
#define DATA_QUEUE_H

#include <Arduino.h>
#include <string.h>
#include "nvs_config.h"
#include "spi_flash_handler.h"

// External LED blink functions from main.ino
extern void startStatusLEDBlink(uint8_t r, uint8_t g, uint8_t b);
extern void stopStatusLEDBlink();  

class DataQueue {
private:
  bool initialized = false;
  SPIFlashHandler* flashHandler = nullptr;
  const uint32_t QUEUE_FLASH_ADDR = 0x10000;
  
  uint32_t queueStartAddr;
  uint32_t queueEndAddr;
  uint32_t queueSizeBytes;
  uint32_t writePtr;
  uint32_t readPtr;
  
  const size_t BATCH_SIZE = 2048;
  
  void calculateQueueBounds() {
    if (flashHandler == nullptr) {
      queueSizeBytes = 256 * 1024;
    } else {
      uint32_t totalSpace = flashHandler->getCapacity();
      if (totalSpace == 0) totalSpace = 16 * 1024 * 1024;
      queueSizeBytes = totalSpace - QUEUE_FLASH_ADDR;
    }
    
    queueStartAddr = QUEUE_FLASH_ADDR;
    queueEndAddr = queueStartAddr + queueSizeBytes;
    
    Serial.print("Queue bounds: Start=0x");
    Serial.print(queueStartAddr, HEX);
    Serial.print(", End=0x");
    Serial.print(queueEndAddr, HEX);
    Serial.print(", Size=");
    Serial.print(queueSizeBytes);
    Serial.println(" bytes");
  }
  
  // Check if we need to wrap around (circular buffer)
  uint32_t wrapAddress(uint32_t addr) {
    if (addr >= queueEndAddr) {
      return queueStartAddr + (addr - queueEndAddr);
    }
    return addr;
  }
  
  // Erase sector if we're writing to it (flash requires sector erase before write)
  // Flash memory can only change bits from 1->0, so we must erase (set all to 1) before writing
  void ensureSectorErased(uint32_t addr, size_t writeLen) {
    uint32_t sectorStart = (addr / 4096) * 4096;
    
    if (addr == sectorStart) {
      Serial.print("Erasing sector at 0x");
      Serial.println(sectorStart, HEX);
      flashHandler->eraseSector(sectorStart);
      delay(10);
    }
    
    uint32_t writeEnd = addr + writeLen;
    uint32_t endSector = (writeEnd / 4096) * 4096;
    if (endSector > sectorStart && endSector < queueEndAddr) {
      Serial.print("Erasing next sector at 0x");
      Serial.println(endSector, HEX);
      flashHandler->eraseSector(endSector);
      delay(10);
    }
  }

public:
  DataQueue() : initialized(false), flashHandler(nullptr), 
                queueStartAddr(0), queueEndAddr(0), queueSizeBytes(0),
                writePtr(0), readPtr(0) {}
  
  ~DataQueue() {}
  
  bool begin(SPIFlashHandler* handler) {
    if (initialized) return true;

    this->flashHandler = handler;
    if (this->flashHandler == nullptr || !this->flashHandler->isInitialized()) {
        Serial.println("DataQueue: Invalid or uninitialized Flash Handler!");
        return false;
    }
    
    // Calculate queue boundaries
    calculateQueueBounds();
    
    // Check if first boot
    bool firstBoot = NVSConfig::isFirstBoot();
    
    Serial.println("========== DataQueue Initialization ==========");
    Serial.print("First boot flag: ");
    Serial.println(firstBoot ? "YES (first time)" : "NO (subsequent boot)");
    
    if (firstBoot) {
      // Initialize pointers to start
      writePtr = queueStartAddr;
      readPtr = queueStartAddr;
      
      Serial.println("FIRST BOOT: Initializing queue pointers to start");
      Serial.print("  Setting writePtr = 0x");
      Serial.println(writePtr, HEX);
      Serial.print("  Setting readPtr = 0x");
      Serial.println(readPtr, HEX);
      
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
      
      // Erase first sector to start clean
      Serial.println("  Erasing first sector...");
      flashHandler->eraseSector(queueStartAddr);
    } else {
      // Load pointers from NVS
      Serial.println("SUBSEQUENT BOOT: Loading queue pointers from NVS");
      
      uint32_t loadedWritePtr = NVSConfig::getQueueWritePtr();
      uint32_t loadedReadPtr = NVSConfig::getQueueReadPtr();
      
      Serial.print("  Loaded writePtr from NVS: 0x");
      Serial.println(loadedWritePtr, HEX);
      Serial.print("  Loaded readPtr from NVS: 0x");
      Serial.println(loadedReadPtr, HEX);
      
      // Handle case where NVS returned 0 (might be first write after initial setup)
      if (loadedWritePtr == 0 && queueStartAddr != 0) {
        Serial.println("  WARNING: Write pointer is 0, but queue starts at non-zero address");
        Serial.println("  This might indicate NVS read failure or first write");
        loadedWritePtr = queueStartAddr;
      }
      
      if (loadedReadPtr == 0 && queueStartAddr != 0) {
        Serial.println("  WARNING: Read pointer is 0, but queue starts at non-zero address");
        Serial.println("  This might indicate NVS read failure or first write");
        loadedReadPtr = queueStartAddr;
      }
      
      // Validate pointers are within bounds
      if (loadedWritePtr < queueStartAddr || loadedWritePtr >= queueEndAddr) {
        Serial.print("  ERROR: Write pointer 0x");
        Serial.print(loadedWritePtr, HEX);
        Serial.println(" is out of bounds! Resetting to queue start.");
        writePtr = queueStartAddr;
      } else {
        writePtr = loadedWritePtr;
        Serial.print("  Write pointer validated: 0x");
        Serial.println(writePtr, HEX);
      }
      
      if (loadedReadPtr < queueStartAddr || loadedReadPtr >= queueEndAddr) {
        Serial.print("  ERROR: Read pointer 0x");
        Serial.print(loadedReadPtr, HEX);
        Serial.println(" is out of bounds! Resetting to queue start.");
        readPtr = queueStartAddr;
      } else {
        readPtr = loadedReadPtr;
        Serial.print("  Read pointer validated: 0x");
        Serial.println(readPtr, HEX);
      }
      
      // Calculate and display queued data
      int queuedBytes = 0;
      if (writePtr >= readPtr) {
        queuedBytes = writePtr - readPtr;
      } else {
        queuedBytes = (queueEndAddr - readPtr) + (writePtr - queueStartAddr);
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
    
    ensureSectorErased(writePtr, dataLen);
    
    const char* dataStr = csvData.c_str();
    Serial.print("Writing ");
    Serial.print(dataLen);
    Serial.print(" bytes to flash at 0x");
    Serial.println(writePtr, HEX);
    
    startStatusLEDBlink(0, 255, 255);
    
    if (!flashHandler->writeCharArray(writePtr, dataStr, dataLen)) {
      Serial.print("ERROR: Failed to write to flash at 0x");
      Serial.print(writePtr, HEX);
      Serial.print(" (length: ");
      Serial.print(dataLen);
      Serial.println(" bytes)");
      stopStatusLEDBlink();
      
      return false;
    }
    
    Serial.println("Flash write successful");
    stopStatusLEDBlink();
    
    writePtr += dataLen;
    
    // Wrap around if needed
    if (writePtr >= queueEndAddr) {
      writePtr = queueStartAddr + (writePtr - queueEndAddr);
      // If we wrapped, we may have overwritten old data
      // Update readPtr if it's behind (circular buffer: oldest data is lost)
      if (readPtr < writePtr && readPtr > queueStartAddr) {
        // ReadPtr is now invalid (behind writePtr after wrap), reset to writePtr
        readPtr = writePtr;
        NVSConfig::setQueueReadPtr(readPtr);
      }
    }
    
    Serial.print("Saving writePtr to NVS: 0x");
    Serial.println(writePtr, HEX);
    
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
    
    if (readPtr == writePtr) {
      Serial.println("readBatch: Queue is empty (readPtr == writePtr)");
      return "";
    }
    
    if (readPtr < queueStartAddr || readPtr >= queueEndAddr) {
      Serial.print("ERROR: readPtr out of bounds: 0x");
      Serial.print(readPtr, HEX);
      Serial.print(" (valid range: 0x");
      Serial.print(queueStartAddr, HEX);
      Serial.print(" - 0x");
      Serial.print(queueEndAddr, HEX);
      Serial.println(")");
      return "";
    }
    
    if (writePtr < queueStartAddr || writePtr >= queueEndAddr) {
      Serial.print("ERROR: writePtr out of bounds: 0x");
      Serial.print(writePtr, HEX);
      Serial.print(" (valid range: 0x");
      Serial.print(queueStartAddr, HEX);
      Serial.print(" - 0x");
      Serial.print(queueEndAddr, HEX);
      Serial.println(")");
      return "";
    }
    
    char* buffer = (char*)malloc(BATCH_SIZE + 1);
    if (buffer == nullptr) {
      Serial.println("ERROR: Failed to allocate memory for read buffer");
      return "";
    }
    
    size_t bytesRead = 0;
    uint32_t currentReadPtr = readPtr;
    
    Serial.print("readBatch: Starting read from 0x");
    Serial.print(readPtr, HEX);
    Serial.print(" to 0x");
    Serial.print(writePtr, HEX);
    Serial.println();
    
    while (bytesRead < BATCH_SIZE && currentReadPtr != writePtr) {
      uint32_t bytesToEnd = (writePtr > currentReadPtr) ? 
                            (writePtr - currentReadPtr) : 
                            (queueEndAddr - currentReadPtr);
      
      size_t chunkSize = (bytesToEnd < (BATCH_SIZE - bytesRead)) ? bytesToEnd : (BATCH_SIZE - bytesRead);
      
      if (!flashHandler->readCharArray(currentReadPtr, buffer + bytesRead, chunkSize)) {
        Serial.print("ERROR: Flash read failed for ");
        Serial.print(chunkSize);
        Serial.print(" bytes at 0x");
        Serial.print(currentReadPtr, HEX);
        Serial.print(" (bytesRead so far: ");
        Serial.print(bytesRead);
        Serial.println(")");
        free(buffer);
        return "";
      }
      
      bytesRead += chunkSize;
      currentReadPtr += chunkSize;
      
      if (currentReadPtr >= queueEndAddr) currentReadPtr = queueStartAddr;
      if (currentReadPtr == writePtr) break;
    }
    
    buffer[bytesRead] = '\0';
    
    Serial.print("readBatch: Total bytes read: ");
    Serial.println(bytesRead);
    
    // Validate that we didn't just read erased flash (all 0xFF or mostly 0xFF)
    // Erased flash appears as 0xFF bytes which show as garbage characters
    if (bytesRead > 0) {
      size_t checkSize = (bytesRead < 32) ? bytesRead : 32;
      size_t validChars = 0;
      
      for (size_t i = 0; i < checkSize; i++) {
        char c = buffer[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
            c == ',' || c == '.' || c == '-' || c == '\n' || c == '_' || c == ':' || c == ' ') {
          validChars++;
        }
      }
      
      // If less than 50% valid characters, data is likely corrupted
      if (validChars < (checkSize / 2)) {
        Serial.println("ERROR: Data appears corrupted - resetting queue");
        readPtr = queueStartAddr;
        writePtr = queueStartAddr;
        NVSConfig::setQueueReadPtr(readPtr);
        NVSConfig::setQueueWritePtr(writePtr);
        free(buffer);
        return "";
      }
    }
    
    // Find last complete line (end with newline)
    int lastNewline = -1;
    for (int i = bytesRead - 1; i >= 0; i--) {
      if (buffer[i] == '\n') {
        lastNewline = i;
        break;
      }
    }
    
    String result;
    if (lastNewline >= 0) {
      buffer[lastNewline + 1] = '\0';
      result = String(buffer);
      Serial.print("readBatch: Returning ");
      Serial.print(result.length());
      Serial.println(" bytes");
    } else if (bytesRead > 0) {
      result = String(buffer);
      Serial.print("readBatch: WARNING - No newline found, returning ");
      Serial.print(result.length());
      Serial.println(" bytes (no newline)");
    }
    
    free(buffer);
    return result;
  }

  bool commitRead(const String& sentData) {
    if (!initialized || sentData.length() == 0) return false;
    
    size_t bytesToAdvance = sentData.length();
    Serial.print("commitRead: Advancing read pointer by ");
    Serial.print(bytesToAdvance);
    Serial.println(" bytes");
    
    readPtr += bytesToAdvance;
    
    // Wrap around if needed
    if (readPtr >= queueEndAddr) {
      readPtr = queueStartAddr + (readPtr - queueEndAddr);
    }
    
    Serial.print("commitRead: New read pointer: 0x");
    Serial.println(readPtr, HEX);
    
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
      return (writePtr - readPtr) / 350;
    } else {
      // Wrapped around
      return ((queueEndAddr - readPtr) + (writePtr - queueStartAddr)) / 350;
    }
  }

  bool isInitialized() { return initialized; }
  
  bool isEmpty() {
    if (!initialized) return true;
    return (readPtr == writePtr);
  }

  int getMaxSize() { return queueSizeBytes; }

  void clear() {
    if (!initialized) return;
    
    Serial.println("Clearing queue...");
    readPtr = queueStartAddr;
    writePtr = queueStartAddr;
    NVSConfig::setQueueReadPtr(readPtr);
    NVSConfig::setQueueWritePtr(writePtr);
    flashHandler->eraseSector(queueStartAddr);
    Serial.println("Queue cleared");
  }
  
  void forceSavePointers() {
    if (!initialized) {
      Serial.println("forceSavePointers: Queue not initialized");
      return;
    }
    
    Serial.println("========== Force Saving Queue Pointers ==========");
    Serial.print("Current writePtr: 0x");
    Serial.println(writePtr, HEX);
    Serial.print("Current readPtr: 0x");
    Serial.println(readPtr, HEX);
    
    bool writePtrSaved = NVSConfig::setQueueWritePtr(writePtr);
    bool readPtrSaved = NVSConfig::setQueueReadPtr(readPtr);
    
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
    uint32_t verifyWrite = NVSConfig::getQueueWritePtr();
    uint32_t verifyRead = NVSConfig::getQueueReadPtr();
    
    Serial.print("Verification - writePtr read back: 0x");
    Serial.println(verifyWrite, HEX);
    Serial.print("Verification - readPtr read back: 0x");
    Serial.println(verifyRead, HEX);
    
    if (verifyWrite == writePtr && verifyRead == readPtr) {
      Serial.println("SUCCESS: All pointers verified!");
    } else {
      Serial.println("WARNING: Pointer verification mismatch!");
    }
    Serial.println("================================================");
  }
};

#endif
