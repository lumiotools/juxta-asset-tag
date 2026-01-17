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
  
private:
  // Helper: Read data from flash with automatic wraparound handling
  bool readBytesWithWrap(uint32_t startAddr, uint8_t* buffer, size_t length) {
    if (length == 0) return true;
    
    size_t bytesRead = 0;
    uint32_t addr = startAddr;
    
    while (bytesRead < length) {
      // Calculate how many bytes we can read before hitting the end
      size_t remainingInFlash = flashEndAddr - addr;
      size_t toRead = min(length - bytesRead, remainingInFlash);
      
      // Read chunk
      if (!flashHandler->readBytes(addr, buffer + bytesRead, toRead)) {
        Serial.print("ERROR: Failed to read bytes at 0x");
        Serial.println(addr, HEX);
        return false;
      }
      
      bytesRead += toRead;
      addr += toRead;
      
      // Wrap to start if we hit the end
      if (addr >= flashEndAddr) {
        addr = CSV_FLASH_START_ADDR;
      }
    }
    
    return true;
  }

public:
  // Read next CSV entry (does NOT advance readPtr - call markAsSent() after successful transmission)
  // Returns the CSV entry without the newline character
  String readNextCSVEntry() {
    if (!initialized || flashHandler == nullptr) {
      return String("");
    }
    
    // Check if we have data to read
    if (readPtr == writePtr) {
      return String(""); // No data available
    }
    
    // Step 1: Find the newline character and calculate entry length
    uint32_t scanPtr = readPtr;
    size_t entryLength = 0;
    bool foundNewline = false;
    uint32_t newlinePtr = 0;
    const size_t MAX_ENTRY_SIZE = 200; // Safety limit
    
    while (entryLength < MAX_ENTRY_SIZE) {
      // Read one byte
      uint8_t byte;
      if (!flashHandler->readBytes(scanPtr, &byte, 1)) {
        Serial.print("ERROR: Failed to read byte at 0x");
        Serial.println(scanPtr, HEX);
        return String("");
      }
      
      // Check if it's a newline
      if (byte == '\n') {
        foundNewline = true;
        newlinePtr = scanPtr;
        break;
      }
      
      // Move to next position with wraparound
      entryLength++;
      scanPtr++;
      if (scanPtr >= flashEndAddr) {
        scanPtr = CSV_FLASH_START_ADDR;
      }
      
      // Check if we've caught up with write pointer (no complete entry available)
      if (scanPtr == writePtr) {
        return String(""); // No complete entry found
      }
    }
    
    // Check for errors
    if (!foundNewline) {
      Serial.println("ERROR: Entry too large or no newline found");
      return String("");
    }
    
    if (entryLength == 0) {
      // Empty entry (just a newline) - skip it
      lastEntryEndPtr = wrapAddress(scanPtr + 1);
      return String("");
    }
    else if (entryLength > 100){
      Serial.println("ERROR: Entry too large - skipping");
      lastEntryEndPtr = wrapAddress(scanPtr + newlinePtr);
      return String("");
    }
    
    // Step 2: Allocate buffer and read the complete entry
    char* buffer = (char*)malloc(entryLength + 1);
    if (!buffer) {
      Serial.println("ERROR: Failed to allocate memory for CSV entry");
      return String("");
    }
    
    // Read the entry data (excluding the newline)
    if (!readBytesWithWrap(readPtr, (uint8_t*)buffer, entryLength)) {
      free(buffer);
      return String("");
    }
    
    // Null-terminate the string
    buffer[entryLength] = '\0';
    
    // Step 3: Update lastEntryEndPtr to point after the newline
    lastEntryEndPtr = wrapAddress(scanPtr + 1);
    readPtr = lastEntryEndPtr;
    
    // Step 4: Create result string and cleanup
    String result = String(buffer);
    free(buffer);
    
    return result;
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
    // readPtr = lastEntryEndPtr;
    NVSConfig::setCSVReadPtr(readPtr);
  }
  
  // Get last entry end pointer (set by last readNextCSVEntry())
  uint32_t getLastEntryEndPtr() const { return lastEntryEndPtr; }
  
  // Mark entry as failed (don't advance readPtr - entry remains for retry)
  void markAsFailed() {
    // readPtr stays at current position (entry not consumed)
    // No action needed - entry will be retried on next cycle
    readPtr = NVSConfig::getCSVReadPtr();
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

