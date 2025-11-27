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

  // Address where the queue data starts in SPI Flash
  const uint32_t QUEUE_FLASH_ADDR = 0x10000; // 64KB offset
  
  // Queue boundaries
  uint32_t queueStartAddr;
  uint32_t queueEndAddr;
  uint32_t queueSizeBytes;
  
  // Read/Write pointers (stored in NVS, loaded on begin)
  uint32_t writePtr;
  uint32_t readPtr;
  
  // Batch size for reading (2KB chunks)
  const size_t BATCH_SIZE = 2048;
  
  // Calculate queue size based on flash capacity
  void calculateQueueBounds() {
    if (flashHandler == nullptr) {
      queueSizeBytes = 256 * 1024; // Fallback: 256KB
    } else {
      uint32_t totalSpace = flashHandler->getCapacity();
      if (totalSpace == 0) totalSpace = 16 * 1024 * 1024; // Assume 16MB if unknown
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
    uint32_t writeEnd = addr + writeLen;
    
    // Check if we're at the start of a sector (new sector) - always erase at sector start
    bool atSectorStart = (addr == sectorStart);
    
    // Check if the EXACT write range is already erased (all 0xFF)
    // We must check the entire range we're about to write to
    bool needsErase = false;
    
    // Read the exact write range to check if it's erased
    uint8_t* testBytes = (uint8_t*)malloc(writeLen);
    if (testBytes != nullptr) {
      if (flashHandler->readBytes(addr, testBytes, writeLen)) {
        // Check if any byte in the write range is not 0xFF (erased state)
        for (size_t i = 0; i < writeLen; i++) {
          if (testBytes[i] != 0xFF) {
            needsErase = true;
            Serial.print("Byte at offset ");
            Serial.print(i);
            Serial.print(" (addr 0x");
            Serial.print(addr + i, HEX);
            Serial.print(") is 0x");
            Serial.print(testBytes[i], HEX);
            Serial.println(" (not erased)");
            break;
          }
        }
      } else {
        // If read failed, assume we need to erase (safe default)
        needsErase = true;
        Serial.println("Failed to read flash for erase check - erasing sector");
      }
      free(testBytes);
    } else {
      // Out of memory, assume we need to erase
      needsErase = true;
      Serial.println("Out of memory for erase check - erasing sector");
    }
    
    // Erase if at sector start OR if write range has non-erased data
    if (atSectorStart || needsErase) {
      Serial.print("Erasing sector at 0x");
      Serial.print(sectorStart, HEX);
      Serial.print(" for write at 0x");
      Serial.print(addr, HEX);
      Serial.print(" (");
      Serial.print(writeLen);
      Serial.print(" bytes)");
      if (atSectorStart) {
        Serial.print(" - sector start");
      } else {
        Serial.print(" - data detected in write range");
      }
      Serial.println();
      
      if (!flashHandler->eraseSector(sectorStart)) {
        Serial.print("ERROR: Failed to erase sector at 0x");
        Serial.println(sectorStart, HEX);
      } else {
        // Verify erase succeeded by reading back
        delay(10); // Small delay for erase to complete
        uint8_t verifyByte;
        if (flashHandler->readBytes(sectorStart, &verifyByte, 1)) {
          if (verifyByte == 0xFF) {
            Serial.println("Sector erased and verified successfully");
          } else {
            Serial.print("WARNING: Sector erase verification failed - first byte is 0x");
            Serial.println(verifyByte, HEX);
          }
        } else {
          Serial.println("WARNING: Could not verify sector erase");
        }
      }
    } else {
      Serial.print("Sector at 0x");
      Serial.print(sectorStart, HEX);
      Serial.println(" already erased (write range is 0xFF)");
    }
  }

public:
  // Constructor
  DataQueue() : initialized(false), flashHandler(nullptr), 
                queueStartAddr(0), queueEndAddr(0), queueSizeBytes(0),
                writePtr(0), readPtr(0) {
  }
  
  // Destructor
  ~DataQueue() {
    // No dynamic memory to free
  }
  
  // Initialize queue - must be called before use
  // Requires SPIFlashHandler instance
  bool begin(SPIFlashHandler* handler) {
    if (initialized) {
      return true; // Already initialized
    }

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

  // Add data to queue (append-only, circular buffer)
  bool enqueue(String csvData) {
    if (!initialized) {
      Serial.println("Queue not initialized! Call begin() first.");
      return false;
    }
    
    // Add newline if not present
    if (!csvData.endsWith("\n")) {
      csvData += "\n";
    }
    
    size_t dataLen = csvData.length();
    
    // Check if we have space (simple check: if writePtr would wrap past readPtr)
    // For now, we allow overwriting old data (circular buffer behavior)
    // More sophisticated: check if writePtr + dataLen would overwrite unread data
    
    // Check if write crosses sector boundary (4KB = 4096 bytes)
    uint32_t currentSector = (writePtr / 4096) * 4096;
    uint32_t endAddr = writePtr + dataLen;
    uint32_t endSector = (endAddr / 4096) * 4096;
    
    // Ensure current sector is erased before writing (check exact write range)
    ensureSectorErased(writePtr, dataLen);
    
    // If write crosses into next sector, erase that sector too
    if (endSector > currentSector && endSector < queueEndAddr) {
      Serial.print("Write crosses sector boundary, erasing next sector at 0x");
      Serial.println(endSector, HEX);
      if (!flashHandler->eraseSector(endSector)) {
        Serial.println("ERROR: Failed to erase next sector!");
      } else {
        delay(10); // Small delay for erase to complete
      }
    }
    
    // Diagnostic: Read target area before write to verify it's erased
    size_t checkLen = (dataLen < 16) ? dataLen : 16; // Check first 16 bytes max
    uint8_t* preWriteCheck = (uint8_t*)malloc(checkLen);
    if (preWriteCheck != nullptr) {
      if (flashHandler->readBytes(writePtr, preWriteCheck, checkLen)) {
        bool allErased = true;
        for (size_t i = 0; i < checkLen; i++) {
          if (preWriteCheck[i] != 0xFF) {
            allErased = false;
            Serial.print("WARNING: Pre-write check - byte at offset ");
            Serial.print(i);
            Serial.print(" is 0x");
            Serial.print(preWriteCheck[i], HEX);
            Serial.println(" (not 0xFF)");
            break;
          }
        }
        if (allErased) {
          Serial.print("Pre-write check: First ");
          Serial.print(checkLen);
          Serial.println(" bytes are erased (0xFF)");
        }
      }
      free(preWriteCheck);
    }
    
    // Write data at current write pointer
    const char* dataStr = csvData.c_str();
    Serial.print("Writing ");
    Serial.print(dataLen);
    Serial.print(" bytes to flash at 0x");
    Serial.println(writePtr, HEX);
    
    // Start LED blinking (cyan color) to indicate flash write
    startStatusLEDBlink(0, 255, 255); // Cyan - not currently used
    
    if (!flashHandler->writeCharArray(writePtr, dataStr, dataLen)) {
      Serial.print("ERROR: Failed to write to flash at 0x");
      Serial.print(writePtr, HEX);
      Serial.print(" (length: ");
      Serial.print(dataLen);
      Serial.println(" bytes)");
      Serial.println("Check: 1) Sector properly erased? 2) Flash initialized? 3) Address valid?");
      
      // Diagnostic: Try to read back what's at that address
      size_t readBackLen = (dataLen < 16) ? dataLen : 16;
      uint8_t readBack[16];
      if (flashHandler->readBytes(writePtr, readBack, readBackLen)) {
        Serial.print("Read back from 0x");
        Serial.print(writePtr, HEX);
        Serial.print(": ");
        for (size_t i = 0; i < readBackLen; i++) {
          Serial.print("0x");
          Serial.print(readBack[i], HEX);
          if (i < min(dataLen, (size_t)16) - 1) Serial.print(" ");
        }
        Serial.println();
      }
      
      // Stop LED blinking on error
      stopStatusLEDBlink();
      
      return false;
    }
    
    Serial.println("Flash write successful");
    
    // Stop LED blinking after successful write
    stopStatusLEDBlink();
    
    // Update write pointer
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
    
    // Always save write pointer to NVS after each write to prevent data loss on reset
    // NVS can handle millions of write cycles, so this is safe
    Serial.print("Saving writePtr to NVS: 0x");
    Serial.println(writePtr, HEX);
    
    if (!NVSConfig::setQueueWritePtr(writePtr)) {
      Serial.println("ERROR: Failed to save write pointer to NVS!");
      // Continue anyway - data is in flash, just pointer might not persist
    } else {
      Serial.println("Write pointer saved successfully");
    }
    
    return true;
  }

  // Read a batch of data for transmission (doesn't advance read pointer)
  // Returns empty string if no data available
  String readBatch() {
    if (!initialized) {
      Serial.println("ERROR: readBatch() called but queue not initialized");
      return "";
    }
    
    // Check if queue is empty (readPtr == writePtr)
    if (readPtr == writePtr) {
      Serial.println("readBatch: Queue is empty (readPtr == writePtr)");
      return ""; // Queue is empty
    }
    
    // Validate pointers are within bounds
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
    
    // Allocate buffer for batch
    char* buffer = (char*)malloc(BATCH_SIZE + 1);
    if (buffer == nullptr) {
      Serial.println("ERROR: Failed to allocate memory for read buffer");
      return ""; // Out of memory
    }
    
    size_t bytesRead = 0;
    uint32_t currentReadPtr = readPtr;
    
    Serial.print("readBatch: Starting read from 0x");
    Serial.print(readPtr, HEX);
    Serial.print(" to 0x");
    Serial.print(writePtr, HEX);
    Serial.println();
    
    // Read up to BATCH_SIZE bytes, stopping at newline boundaries
    while (bytesRead < BATCH_SIZE && currentReadPtr != writePtr) {
      // Calculate how much we can read in this iteration
      uint32_t bytesToEnd;
      if (writePtr > currentReadPtr) {
        // Normal case: writePtr is ahead
        bytesToEnd = writePtr - currentReadPtr;
      } else {
        // Wrapped case: writePtr wrapped around, read to end of buffer
        bytesToEnd = queueEndAddr - currentReadPtr;
      }
      
      size_t chunkSize = (bytesToEnd < (BATCH_SIZE - bytesRead)) ? bytesToEnd : (BATCH_SIZE - bytesRead);
      
      // Read chunk from flash
      Serial.print("  Reading ");
      Serial.print(chunkSize);
      Serial.print(" bytes from 0x");
      Serial.println(currentReadPtr, HEX);
      
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
      
      Serial.print("  Successfully read, total bytes: ");
      Serial.println(bytesRead);
      
      // Wrap around if needed
      if (currentReadPtr >= queueEndAddr) {
        currentReadPtr = queueStartAddr;
      }
      
      // Stop if we've caught up to write pointer
      if (currentReadPtr == writePtr) {
        break;
      }
    }
    
    buffer[bytesRead] = '\0';
    
    Serial.print("readBatch: Total bytes read: ");
    Serial.println(bytesRead);
    
    // Validate that we didn't just read erased flash (all 0xFF or mostly 0xFF)
    // Erased flash appears as 0xFF bytes which show as garbage characters
    if (bytesRead > 0) {
      size_t erasedByteCount = 0;
      size_t checkSize = (bytesRead < 100) ? bytesRead : 100; // Check first 100 bytes
      
      for (size_t i = 0; i < checkSize; i++) {
        if ((uint8_t)buffer[i] == 0xFF) {
          erasedByteCount++;
        }
      }
      
      // If more than 90% of checked bytes are 0xFF, this is likely erased flash
      if (erasedByteCount > (checkSize * 9 / 10)) {
        Serial.print("ERROR: Read data is erased flash (");
        Serial.print(erasedByteCount);
        Serial.print(" of ");
        Serial.print(checkSize);
        Serial.println(" bytes are 0xFF)");
        
        // Print hex dump of first 32 bytes for debugging
        Serial.println("Hex dump of first 32 bytes:");
        size_t dumpSize = (bytesRead < 32) ? bytesRead : 32;
        for (size_t i = 0; i < dumpSize; i++) {
          if (i % 16 == 0) {
            Serial.printf("\n0x%04X: ", i);
          }
          Serial.printf("%02X ", (uint8_t)buffer[i]);
        }
        Serial.println();
        
        Serial.println("ERROR: Queue pointers point to erased/invalid data");
        Serial.print("ERROR: readPtr was 0x");
        Serial.print(readPtr, HEX);
        Serial.print(", writePtr was 0x");
        Serial.println(writePtr, HEX);
        Serial.println("ERROR: Resetting queue to prevent garbage transmission");
        
        // Reset queue pointers to prevent sending garbage
        readPtr = queueStartAddr;
        writePtr = queueStartAddr;
        NVSConfig::setQueueReadPtr(readPtr);
        NVSConfig::setQueueWritePtr(writePtr);
        
        free(buffer);
        return ""; // Return empty to stop transmission
      }
      
      // Also check for printable CSV-like content (should have letters, numbers, commas)
      size_t validChars = 0;
      for (size_t i = 0; i < checkSize; i++) {
        char c = buffer[i];
        // Count printable ASCII characters typical in CSV data
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
            c == ',' || c == '.' || c == '-' || c == '\n' || c == '_' || c == ':') {
          validChars++;
        }
      }
      
      // If less than 50% valid characters, data is likely corrupted
      if (validChars < (checkSize / 2)) {
        Serial.print("ERROR: Read data contains mostly invalid characters (");
        Serial.print(validChars);
        Serial.print(" of ");
        Serial.print(checkSize);
        Serial.println(" are valid CSV chars)");
        
        // Print hex dump of first 32 bytes for debugging
        Serial.println("Hex dump of first 32 bytes:");
        size_t dumpSize = (bytesRead < 32) ? bytesRead : 32;
        for (size_t i = 0; i < dumpSize; i++) {
          if (i % 16 == 0) {
            Serial.printf("\n0x%04X: ", i);
          }
          Serial.printf("%02X ", (uint8_t)buffer[i]);
        }
        Serial.println();
        
        Serial.println("ERROR: Data appears corrupted - resetting queue");
        Serial.print("ERROR: readPtr was 0x");
        Serial.print(readPtr, HEX);
        Serial.print(", writePtr was 0x");
        Serial.println(writePtr, HEX);
        
        // Reset queue pointers
        readPtr = queueStartAddr;
        writePtr = queueStartAddr;
        NVSConfig::setQueueReadPtr(readPtr);
        NVSConfig::setQueueWritePtr(writePtr);
        
        free(buffer);
        return "";
      }
      
      Serial.print("Data validation: ");
      Serial.print(validChars);
      Serial.print(" valid chars out of ");
      Serial.print(checkSize);
      Serial.println(" checked - data appears valid");
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
      // Return complete lines only
      buffer[lastNewline + 1] = '\0';
      result = String(buffer);
      Serial.print("readBatch: Returning ");
      Serial.print(result.length());
      Serial.println(" bytes (complete lines)");
    } else if (bytesRead > 0) {
      // No newline found, return what we have (might be partial)
      result = String(buffer);
      Serial.print("readBatch: WARNING - No newline found, returning ");
      Serial.print(result.length());
      Serial.println(" bytes (partial data)");
    } else {
      Serial.println("readBatch: No data read, returning empty string");
    }
    
    free(buffer);
    return result;
  }

  // Commit read pointer (mark batch as sent)
  // Advances read pointer past the data that was successfully sent
  // Takes the string that was sent and advances by its length
  bool commitRead(const String& sentData) {
    if (!initialized || sentData.length() == 0) {
      return false;
    }
    
    size_t bytesToAdvance = sentData.length();
    
    Serial.print("commitRead: Advancing read pointer by ");
    Serial.print(bytesToAdvance);
    Serial.println(" bytes");
    
    // Advance read pointer
    uint32_t oldReadPtr = readPtr;
    readPtr += bytesToAdvance;
    
    // Wrap around if needed
    if (readPtr >= queueEndAddr) {
      readPtr = queueStartAddr + (readPtr - queueEndAddr);
      Serial.print("commitRead: Pointer wrapped from 0x");
      Serial.print(oldReadPtr, HEX);
      Serial.print(" to 0x");
      Serial.println(readPtr, HEX);
    }
    
    Serial.print("commitRead: New read pointer: 0x");
    Serial.print(readPtr, HEX);
    Serial.println(" - Saving to NVS...");
    
    // Always save to NVS immediately
    if (!NVSConfig::setQueueReadPtr(readPtr)) {
      Serial.println("commitRead: ERROR - Failed to save read pointer to NVS!");
      return false;
    }
    
    Serial.println("commitRead: Read pointer saved successfully");
    return true;
  }

  // Get current queue length (approximate, based on pointers)
  int getLength() {
    if (!initialized) return 0;
    
    if (writePtr >= readPtr) {
      return (writePtr - readPtr) / 350; // Approximate: 350 bytes per item
    } else {
      // Wrapped around
      return ((queueEndAddr - readPtr) + (writePtr - queueStartAddr)) / 350;
    }
  }

  // Check if queue is initialized
  bool isInitialized() {
    return initialized;
  }

  // Check if queue is empty
  bool isEmpty() {
    if (!initialized) return true;
    return (readPtr == writePtr);
  }

  // Get max size (in bytes)
  int getMaxSize() {
    return queueSizeBytes;
  }

  // Clear entire queue (reset pointers)
  void clear() {
    if (!initialized) return;
    
    Serial.println("Clearing queue...");
    readPtr = queueStartAddr;
    writePtr = queueStartAddr;
    
    NVSConfig::setQueueReadPtr(readPtr);
    NVSConfig::setQueueWritePtr(writePtr);
    
    // Erase first sector to mark as empty
    flashHandler->eraseSector(queueStartAddr);
    Serial.println("Queue cleared");
  }
  
  // Force save current pointers to NVS
  // Call this before deep sleep or reset to ensure data persistence
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
