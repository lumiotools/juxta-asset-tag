#ifndef UNIFIED_CSV_STORAGE_H
#define UNIFIED_CSV_STORAGE_H

#include "spi_flash_handler.h"
#include "nvs_config.h"

// Flash memory: Use entire external flash as circular buffer
// NOTE: Now using BINARY storage despite the filename/defines
// Operations are now aligned to 32-byte blocks (sizeof(TimestampedIMUReading))
#define BINARY_FLASH_START_ADDR 0x000000
#define FLASH_SECTOR_SIZE 4096 

// Structure for IMU reading with timestamp
// PACKED to ensure it's exactly 32 bytes (6 floats = 24 bytes + 8 bytes timestamp)
// This allows for direct binary block transfer.
struct __attribute__((packed)) TimestampedIMUReading {
  float accX, accY, accZ;
  float gyrX, gyrY, gyrZ;
  unsigned long long timestamp;
};

// Class name kept as UnifiedCSVStorage for compatibility with existing code
// but internally uses Binary storage.
class UnifiedCSVStorage {
private:
  SPIFlashHandler* flashHandler;
  bool initialized;
 
  // Circular buffer pointers (stored in NVS)
  uint32_t writePtr;      // Where to write next batch
  uint32_t readPtr;       // Where to read from (for transmission)
  uint32_t flashCapacity; // Total flash capacity in bytes
  uint32_t lastErasedSector;
 
  // Ensure sector is erased before writing
  void ensureSectorErased(uint32_t addr) {
    uint32_t sectorStart = (addr / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
    
    if (sectorStart != lastErasedSector) {
      // Only erase if within capacity
      if (sectorStart < flashCapacity) {
        if (!flashHandler->eraseSector(sectorStart)) {
          Serial.print("ERROR: Failed to erase sector at 0x");
          Serial.println(sectorStart, HEX);
          return;
        }
        // Metadata tracking
        lastErasedSector = sectorStart;
      }
    }
  }
 
  // Wrap address for circular buffer
  uint32_t wrapAddress(uint32_t addr) {
    if (addr >= flashCapacity) {
      return BINARY_FLASH_START_ADDR + (addr - flashCapacity);
    }
    return addr;
  }

public:
  UnifiedCSVStorage() : flashHandler(nullptr), initialized(false),
                       writePtr(0), readPtr(0), flashCapacity(0),
                       lastErasedSector(0xFFFFFFFF) {}
 
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
   
    // Load pointers from NVS
    writePtr = NVSConfig::getCSVWritePtr();
    readPtr = NVSConfig::getCSVReadPtr();
   
    // Validate pointers
    if (writePtr >= flashCapacity) {
      Serial.print("WARNING: Invalid writePtr from NVS (0x");
      Serial.print(writePtr, HEX);
      Serial.println(") - resetting to start");
      writePtr = BINARY_FLASH_START_ADDR;
      NVSConfig::setCSVWritePtr(writePtr);
    }
    if (readPtr >= flashCapacity) {
      Serial.print("WARNING: Invalid readPtr from NVS (0x");
      Serial.print(readPtr, HEX);
      Serial.println(") - resetting to start");
      readPtr = BINARY_FLASH_START_ADDR;
      NVSConfig::setCSVReadPtr(readPtr);
    }
   
    Serial.println("========== Unified Binary Storage (via CSV Class) ==========");
    Serial.print("Flash Capacity: ");
    Serial.print(flashCapacity / 1024 / 1024);
    Serial.println(" MB");
    Serial.print("Write Pointer: 0x");
    Serial.println(writePtr, HEX);
    Serial.print("Read Pointer: 0x");
    Serial.println(readPtr, HEX);
    Serial.println("Mode: BINARY (High Speed)");
   
    initialized = true;
    return true;
  }
 
  // Write IMU readings in BINARY format
  // Replaces CSV writing with direct memory dump
  bool writeIMUReadings(TimestampedIMUReading* readings, int count) {
    if (!initialized || flashHandler == nullptr || count <= 0) {
      return false;
    }
   
    size_t size = count * sizeof(TimestampedIMUReading);
   
    // Check if wrap-around is needed
    if (writePtr + size > flashCapacity) {
      // For simplicity in binary block mode, we just wrap to start if it doesn't fit at the end
      // This wastes a tiny bit of space at the end of flash but simplifies logic immensely
      writePtr = BINARY_FLASH_START_ADDR;
    }
   
    // Ensure sectors are erased
    // We erase sector by sector as needed
    uint32_t startAddr = writePtr;
    uint32_t endAddr = writePtr + size;
    
    // Erase all sectors covered by this write
    for (uint32_t addr = startAddr; addr < endAddr; addr += FLASH_SECTOR_SIZE) {
         ensureSectorErased(addr);
    }
    // Also check the end boundary
    ensureSectorErased(endAddr - 1);
   
    // Write binary data
    // Cast struct pointer to const char* for the writeCharArray function
    if (!flashHandler->writeCharArray(writePtr, (const char*)readings, size)) {
      Serial.print("ERROR: Failed to write binary data to flash at 0x");
      Serial.println(writePtr, HEX);
      return false;
    }
   
    // Update write pointer
    writePtr = wrapAddress(writePtr + size);
   
    // Save to NVS
    // In high freq version, we write this every time because accuracy of pointers is critical
    NVSConfig::setCSVWritePtr(writePtr);
   
    return true;
  }

  // HIGH SPEED READ: Pulls binary data directly into buffer
  // Returns number of readings actually read into the buffer
  // This performs a PEEK (does not advance NVS pointer until markAsSent is called)
  // BUT it DOES update the internal readPtr to allow subsequent reads in same session if desired?
  // Current logic: readBatch reads FROM readPtr. It returns data.
  // It does NOT update readPtr.
  // To advance, you must call markAsSent(count).
  size_t readBatch(TimestampedIMUReading* outputBuffer, size_t maxReadings) {
    if (!initialized || readPtr == writePtr) return 0;

    // Calculate how much we can read
    uint32_t bytesAvailable = 0;
    if (writePtr > readPtr) {
        bytesAvailable = writePtr - readPtr;
    } else {
        // Wrapped around
        bytesAvailable = flashCapacity - readPtr;
    }
    
    size_t readingsAvailable = bytesAvailable / sizeof(TimestampedIMUReading);
    size_t toRead = min((size_t)maxReadings, readingsAvailable); // Cast to size_t 

    if (toRead == 0) return 0;

    // Single SPI Read - Reads 'toRead' blocks directly into buffer
    if (flashHandler->readBytes(readPtr, (uint8_t*)outputBuffer, toRead * sizeof(TimestampedIMUReading))) {
      return toRead; 
    }

    return 0; // Read failed
  }
 
  // Check if data is available to read
  bool hasDataToRead() {
    if (!initialized) return false;
    return (readPtr != writePtr);
  }
 
  // Mark entry as successfully sent (Update NVS and next read pointer)
  void markAsSent(size_t readingsCount) {
    size_t bytesProcessed = readingsCount * sizeof(TimestampedIMUReading);
    readPtr = wrapAddress(readPtr + bytesProcessed);
    NVSConfig::setCSVReadPtr(readPtr);
  }
  
  // Legacy support for loop/clear/logic
  void clear() {
    writePtr = BINARY_FLASH_START_ADDR;
    readPtr = BINARY_FLASH_START_ADDR;
    lastErasedSector = 0xFFFFFFFF;
    NVSConfig::setCSVWritePtr(writePtr);
    NVSConfig::setCSVReadPtr(readPtr);
    Serial.println("Unified Storage cleared (Binary Mode)");
  }
 
  bool isInitialized() const { return initialized; }
  
  // Getters for debugging
  uint32_t getReadPtr() const { return readPtr; }
  uint32_t getWritePtr() const { return writePtr; }
  
  // DEPRECATED: Do not use. Preserved to allow compilation if called, but returns empty.
  String readNextCSVEntry() {
      return "";
  }
};

#endif

