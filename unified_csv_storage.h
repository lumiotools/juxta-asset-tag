// Unified CSV Storage - Uses entire external flash as circular buffer
// Stores IMU data as CSV strings: obj1,obj2,obj3,...\n
// Format: accx|accy|accz|gyrx|gyry|gyrz|timestamp

#ifndef UNIFIED_CSV_STORAGE_H
#define UNIFIED_CSV_STORAGE_H

#include "spi_flash_handler.h"
#include "nvs_config.h"
#include "time_sync.h"
#include "imu_sensor.h"

// Flash memory: Use entire external flash as circular buffer
// Start from address 0, use full capacity
#define CSV_FLASH_START_ADDR 0x000000
#define CSV_SECTOR_SIZE 4096  // 4KB sectors

// Structure for IMU reading with timestamp
struct TimestampedIMUReading {
  float accX, accY, accZ;
  float gyrX, gyrY, gyrZ;
  unsigned long long timestamp;
};

class UnifiedCSVStorage {
private:
  SPIFlashHandler* flashHandler;
  bool initialized;
  
  // Circular buffer pointers (stored in NVS)
  uint32_t writePtr;      // Where to write next CSV entry
  uint32_t readPtr;       // Where to read from (for transmission)
  uint32_t flashCapacity; // Total flash capacity in bytes
  uint32_t flashEndAddr;  // End address (capacity - 1)
  uint32_t lastEntryEndPtr; // Store where last read entry ended (for markAsSent)
  
  // Sector management
  uint32_t lastErasedSector;
  
  // Ensure sector is erased before writing
  void ensureSectorErased(uint32_t addr) {
    if (addr >= flashEndAddr) return;
    
    uint32_t sectorStart = (addr / CSV_SECTOR_SIZE) * CSV_SECTOR_SIZE;
    
    if (sectorStart != lastErasedSector) {
      if (sectorStart < flashEndAddr) {
        if (!flashHandler->eraseSector(sectorStart)) {
          Serial.print("ERROR: Failed to erase sector at 0x");
          Serial.println(sectorStart, HEX);
          return;
        }
        delay(50); // Sector erase takes time
        lastErasedSector = sectorStart;
      }
    }
  }
  
  // Wrap address for circular buffer
  uint32_t wrapAddress(uint32_t addr) {
    if (addr >= flashEndAddr) {
      return CSV_FLASH_START_ADDR + (addr - flashEndAddr);
    }
    return addr;
  }
  
  // Format single IMU object: accx|accy|accz|gyrx|gyry|gyrz|timestamp
  String formatIMUObject(float accX, float accY, float accZ, 
                        float gyrX, float gyrY, float gyrZ, 
                        unsigned long long timestamp) {
    char buffer[100];
    snprintf(buffer, sizeof(buffer), 
             "%.1f|%.1f|%.1f|%.1f|%.1f|%.1f|%llu",
             accX, accY, accZ, gyrX, gyrY, gyrZ, timestamp);
    return String(buffer);
  }

public:
  UnifiedCSVStorage() : flashHandler(nullptr), initialized(false),
                       writePtr(0), readPtr(0), flashCapacity(0), 
                       flashEndAddr(0), lastEntryEndPtr(0), lastErasedSector(0xFFFFFFFF) {}
  
  bool begin(SPIFlashHandler* handler) {
    if (initialized) return true;
    
    if (handler == nullptr || !handler->isInitialized()) {
      Serial.println("UnifiedCSVStorage: Invalid flash handler");
      return false;
    }
    
    flashHandler = handler;
    
    // Get flash capacity
    flashCapacity = flashHandler->getCapacity();
    if (flashCapacity == 0) {
      Serial.println("ERROR: Invalid flash capacity");
      return false;
    }
    flashEndAddr = flashCapacity;
    
    // Load pointers from NVS
    writePtr = NVSConfig::getCSVWritePtr();
    readPtr = NVSConfig::getCSVReadPtr();
    
    // Validate pointers
    if (writePtr >= flashEndAddr) {
      Serial.print("WARNING: Invalid writePtr from NVS (0x");
      Serial.print(writePtr, HEX);
      Serial.println(") - resetting to start");
      writePtr = CSV_FLASH_START_ADDR;
      NVSConfig::setCSVWritePtr(writePtr);
    }
    if (readPtr >= flashEndAddr) {
      Serial.print("WARNING: Invalid readPtr from NVS (0x");
      Serial.print(readPtr, HEX);
      Serial.println(") - resetting to start");
      readPtr = CSV_FLASH_START_ADDR;
      NVSConfig::setCSVReadPtr(readPtr);
    }
    
    Serial.println("========== Unified CSV Storage ==========");
    Serial.print("Flash Capacity: ");
    Serial.print(flashCapacity / 1024 / 1024);
    Serial.println(" MB");
    Serial.print("Flash Range: 0x");
    Serial.print(CSV_FLASH_START_ADDR, HEX);
    Serial.print(" - 0x");
    Serial.print(flashEndAddr, HEX);
    Serial.println();
    Serial.print("Write Pointer: 0x");
    Serial.println(writePtr, HEX);
    Serial.print("Read Pointer: 0x");
    Serial.println(readPtr, HEX);
    
    initialized = true;
    return true;
  }
  
  // Write CSV entry (obj1,obj2,obj3,...,objN\n)
  bool writeCSVEntry(const String& csvData) {
    if (!initialized || flashHandler == nullptr) {
      Serial.println("ERROR: UnifiedCSVStorage not initialized");
      return false;
    }
    
    // Calculate entry size (CSV string + newline)
    size_t entrySize = csvData.length() + 1; // +1 for \n
    
    // Check if entry fits in remaining space
    uint32_t writeEnd = writePtr + entrySize;
    
    // Handle wrap-around: if entry would exceed flash end, wrap to start
    if (writeEnd > flashEndAddr) {
      // Need to wrap - erase start sector
      writePtr = CSV_FLASH_START_ADDR;
      ensureSectorErased(writePtr);
      writeEnd = writePtr + entrySize;
      
      // If entry is larger than flash capacity, it won't fit
      if (entrySize > flashCapacity) {
        Serial.print("ERROR: CSV entry too large (");
        Serial.print(entrySize);
        Serial.print(" bytes) for flash capacity (");
        Serial.print(flashCapacity);
        Serial.println(" bytes)");
        return false;
      }
      
      // Update readPtr if it's in the area we're about to overwrite
      if (readPtr >= CSV_FLASH_START_ADDR && readPtr < writeEnd) {
        readPtr = writeEnd; // Skip overwritten data
        NVSConfig::setCSVReadPtr(readPtr);
      }
    }
    
    // Ensure sectors are erased
    ensureSectorErased(writePtr);
    if (writeEnd > ((writePtr / CSV_SECTOR_SIZE + 1) * CSV_SECTOR_SIZE)) {
      // Entry crosses sector boundary - erase next sector too
      uint32_t nextSector = ((writePtr / CSV_SECTOR_SIZE + 1) * CSV_SECTOR_SIZE);
      if (nextSector < flashEndAddr) {
        ensureSectorErased(nextSector);
      }
    }
    
    // Write CSV string
    if (!flashHandler->writeCharArray(writePtr, csvData.c_str(), csvData.length())) {
      Serial.print("ERROR: Failed to write CSV data to flash at 0x");
      Serial.println(writePtr, HEX);
      return false;
    }
    
    // Write newline delimiter
    const char newline = '\n';
    if (!flashHandler->writeCharArray(writePtr + csvData.length(), &newline, 1)) {
      Serial.println("ERROR: Failed to write newline delimiter");
      return false;
    }
    
    // Update write pointer
    writePtr = writeEnd;
    writePtr = wrapAddress(writePtr);
    
    // Save write pointer to NVS periodically (every 10 entries to reduce wear)
    static uint32_t writeCount = 0;
    writeCount++;
    if (writeCount % 10 == 0) {
      NVSConfig::setCSVWritePtr(writePtr);
    }
    
    return true;
  }
  
  // Read next CSV entry (does NOT advance readPtr - call markAsSent() after successful transmission)
  // Implemented as a 2-pass approach: 1) scan to find newline and determine length, 2) bulk read into buffer
  // This reduces intermediate reallocations and heap pressure from char-by-char appends
  String readNextCSVEntry() {
    if (!initialized || flashHandler == nullptr) {
      return String("");
    }
    
    // Check if we have data to read
    if (readPtr == writePtr) {
      return String(""); // No data available
    }
    
    uint32_t currentPtr = readPtr;
    uint32_t startPtr = readPtr;
    size_t entryLength = 0;
    size_t bytesScanned = 0; // Track total bytes scanned from start

    // First pass: scan ahead for newline to compute entry length and lastEntryEndPtr
    while (true) {
      // Check if we've wrapped around and caught up to writePtr
      if (currentPtr == writePtr) {
        if (entryLength == 0) return String("");
        break; // Partial entry case - no newline found
      }

      // Wrap currentPtr if needed
      if (currentPtr >= flashEndAddr) {
        currentPtr = wrapAddress(currentPtr);
        if (currentPtr == writePtr) break;
      }

      // Calculate how many bytes to read in this chunk
      uint8_t peekBuf[64];
      size_t toRead = 64;
      
      // Don't read past writePtr
      if (writePtr > currentPtr && writePtr < flashEndAddr) {
        size_t remaining = (size_t)(writePtr - currentPtr);
        if (remaining < toRead) toRead = remaining;
      } else if (currentPtr >= writePtr && currentPtr < flashEndAddr) {
        // We've wrapped, read until end of flash or 64 bytes
        size_t remaining = (size_t)(flashEndAddr - currentPtr);
        if (remaining < toRead) toRead = remaining;
      }

      if (toRead == 0) break;

      if (!flashHandler->readBytes(currentPtr, peekBuf, toRead)) {
        Serial.print("ERROR: Failed to peek bytes at 0x");
        Serial.println(currentPtr, HEX);
        return String("");
      }

      // Scan this chunk for newline
      for (size_t i = 0; i < toRead; i++) {
        char c = (char)peekBuf[i];
        if (c == '\n') {
          // Found newline - calculate absolute end position
          // endPtr = currentPtr + i + 1 (to include the newline)
          uint32_t endPtr = currentPtr + (uint32_t)i + 1;
          lastEntryEndPtr = wrapAddress(endPtr);

          if (entryLength == 0) return String(""); // Empty entry

          // Read the full entry into a temporary buffer (bulk read)
          char* buf = (char*)malloc(entryLength + 1);
          if (!buf) {
            Serial.println("ERROR: Failed to allocate buffer for CSV entry");
            return String("");
          }

          // Bulk read with proper wrapping
          size_t copied = 0;
          uint32_t readBlockPtr = startPtr;
          size_t remainingToFill = entryLength;
          while (remainingToFill > 0) {
            size_t chunk = (remainingToFill > 256) ? 256 : remainingToFill;
            
            // Check if we need to wrap during read
            if (readBlockPtr >= flashEndAddr) {
              readBlockPtr = wrapAddress(readBlockPtr);
            }
            
            // Don't read past flash end in a single chunk
            if (readBlockPtr + chunk > flashEndAddr) {
              chunk = flashEndAddr - readBlockPtr;
            }
            
            if (!flashHandler->readBytes(readBlockPtr, (uint8_t*)(buf + copied), chunk)) {
              Serial.print("ERROR: Failed to read CSV entry data at 0x");
              Serial.println(readBlockPtr, HEX);
              free(buf);
              return String("");
            }
            copied += chunk;
            remainingToFill -= chunk;
            readBlockPtr += chunk;
          }

          buf[entryLength] = '\0';
          String result = String(buf);
          free(buf);
          return result;
        }
        entryLength++; // Count this byte
      }

      // Move to next chunk
      bytesScanned += toRead;
      currentPtr += (uint32_t)toRead;

      // Safety: prevent very large entries
      if (entryLength > 10000) {
        Serial.println("ERROR: Entry too large, possible corruption");
        lastEntryEndPtr = wrapAddress(currentPtr);
        return String("");
      }
    }

    // If we exited the loop due to reaching writePtr but found some bytes (partial entry),
    // read what we have and return it (no newline but valid data).
    if (entryLength > 0) {
      // lastEntryEndPtr should be currentPtr (where we stopped)
      lastEntryEndPtr = wrapAddress(currentPtr);

      char* buf = (char*)malloc(entryLength + 1);
      if (!buf) {
        Serial.println("ERROR: Failed to allocate buffer for CSV entry");
        return String("");
      }

      // Bulk read with proper wrapping
      size_t copied = 0;
      uint32_t readBlockPtr = startPtr;
      size_t remainingToFill = entryLength;
      while (remainingToFill > 0) {
        size_t chunk = (remainingToFill > 256) ? 256 : remainingToFill;
        
        // Check if we need to wrap during read
        if (readBlockPtr >= flashEndAddr) {
          readBlockPtr = wrapAddress(readBlockPtr);
        }
        
        // Don't read past flash end in a single chunk
        if (readBlockPtr + chunk > flashEndAddr) {
          chunk = flashEndAddr - readBlockPtr;
        }
        
        if (!flashHandler->readBytes(readBlockPtr, (uint8_t*)(buf + copied), chunk)) {
          Serial.print("ERROR: Failed to read CSV entry data at 0x");
          Serial.println(readBlockPtr, HEX);
          free(buf);
          return String("");
        }
        copied += chunk;
        remainingToFill -= chunk;
        readBlockPtr += chunk;
      }
      
      buf[entryLength] = '\0';
      String result = String(buf);
      free(buf);
      return result;
    }

    return String("");
  }
  
  // Check if data is available to read
  bool hasDataToRead() {
    if (!initialized) return false;
    return (readPtr != writePtr);
  }
  
  // Get current read pointer
  uint32_t getReadPtr() const { return readPtr; }
  
  // Get current write pointer
  uint32_t getWritePtr() const { return writePtr; }
  
  // Reset read pointer (for new transmission cycle - start from oldest unsent data)
  void resetReadPtr() {
    // Start from current readPtr (which points to oldest unsent data)
    // Don't change it - it's already correct
    // Just save to NVS
    NVSConfig::setCSVReadPtr(readPtr);
  }
  
  // Set read pointer (for restoring position after counting)
  void setReadPtr(uint32_t ptr) {
    readPtr = ptr;
    readPtr = wrapAddress(readPtr);
    NVSConfig::setCSVReadPtr(readPtr);
  }
  
  // Mark entry as successfully sent (advance readPtr past the entry we just read)
  void markAsSent() {
    // Use the stored end position from last readNextCSVEntry() call
    readPtr = lastEntryEndPtr;
    NVSConfig::setCSVReadPtr(readPtr);
  }
  
  // Get last entry end pointer (set by last readNextCSVEntry())
  uint32_t getLastEntryEndPtr() const { return lastEntryEndPtr; }
  
  // Mark entry as failed (don't advance readPtr - entry remains for retry)
  void markAsFailed() {
    // readPtr stays at current position (entry not consumed)
    // No action needed - entry will be retried on next cycle
  }
  
  // Save state to NVS
  void saveState() {
    NVSConfig::setCSVWritePtr(writePtr);
    NVSConfig::setCSVReadPtr(readPtr);
  }
  
  // Clear all data (reset pointers)
  void clear() {
    writePtr = CSV_FLASH_START_ADDR;
    readPtr = CSV_FLASH_START_ADDR;
    lastErasedSector = 0xFFFFFFFF;
    saveState();
    Serial.println("Unified CSV storage cleared");
  }
  
  bool isInitialized() const { return initialized; }
  
  // Format and write IMU reading buffer as CSV entry
  // Buffer contains multiple readings, format as: obj1,obj2,obj3,...,objN
  bool writeIMUReadings(TimestampedIMUReading* readings, int count) {
    if (count == 0) return false;
    
    String csv = "";
    
    for (int i = 0; i < count; i++) {
      // Format single object
      String obj = formatIMUObject(
        readings[i].accX, readings[i].accY, readings[i].accZ,
        readings[i].gyrX, readings[i].gyrY, readings[i].gyrZ,
        readings[i].timestamp
      );
      
      // Append to CSV string
      if (csv.length() > 0) {
        csv += ","; // Add comma separator
      }
      csv += obj;
    }
    
    // Write CSV entry to flash
    return writeCSVEntry(csv);
  }
};

#endif

