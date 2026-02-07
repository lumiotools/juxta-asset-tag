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

  // Scan forward from current writePtr to find the actual end of data
  void recoverWritePointer() {
    if (flashHandler == nullptr) return;

    const uint32_t recordSize = sizeof(TimestampedIMUReading);
    const uint32_t recordsPerSector = FLASH_SECTOR_SIZE / recordSize;
    if (recordsPerSector == 0) return;

    auto isErasedRecord = [&](uint32_t addr) -> bool {
      TimestampedIMUReading buffer;
      if (!flashHandler->readBytes(addr, (uint8_t*)&buffer, recordSize)) {
        return false;
      }
      const uint8_t* p = (const uint8_t*)&buffer;
      for (uint32_t i = 0; i < recordSize; i++) {
        if (p[i] != 0xFF) return false;
      }
      return true;
    };

    uint32_t currentAddr = writePtr;
    // Scan up to full capacity if needed (safe due to sector skipping optimization)
    const uint32_t scanLimitBytes = flashCapacity; 
    uint32_t scannedBytes = 0;
    bool advanced = false;

    Serial.print("Recovering writePtr from: 0x");
    Serial.print(currentAddr, HEX);

    // Fast path: if currentAddr is already erased, no recovery needed
    if (isErasedRecord(currentAddr)) {
      Serial.println(" -> No recovery needed.");
      return;
    }

    // Sector-wise probing: skip fully-written sectors by checking the last slot in the sector.
    while (scannedBytes < scanLimitBytes) {
      uint32_t sectorStart = (currentAddr / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
      uint32_t sectorEnd = sectorStart + FLASH_SECTOR_SIZE;
      uint32_t lastSlotAddr = sectorEnd - recordSize;

      // Handle wrap-around for the last slot address
      if (lastSlotAddr >= flashCapacity) {
        lastSlotAddr = wrapAddress(lastSlotAddr);
      }

      // If the last slot is NOT erased, sector is full (or at least written to the end).
      // Jump to next sector.
      if (!isErasedRecord(lastSlotAddr)) {
        uint32_t nextSector = sectorStart + FLASH_SECTOR_SIZE;
        if (nextSector >= flashCapacity) nextSector = BINARY_FLASH_START_ADDR;

        uint32_t delta;
        if (nextSector >= currentAddr) {
          delta = nextSector - currentAddr;
        } else {
          // Wrapped
          delta = (flashCapacity - currentAddr) + nextSector;
        }
        scannedBytes += delta;
        currentAddr = nextSector;
        advanced = true;

        // Watchdog-friendly
        if ((scannedBytes & 0xFFFF) == 0) {
          yield();
        }
        continue;
      }

      // Boundary is inside this sector: find the first erased record starting from currentAddr.
      uint32_t offsetInSector = currentAddr - sectorStart;
      uint32_t startIndex = offsetInSector / recordSize;
      if (startIndex >= recordsPerSector) startIndex = 0;

      for (uint32_t i = startIndex; i < recordsPerSector; i++) {
        uint32_t addr = sectorStart + (i * recordSize);
        if (addr >= flashCapacity) addr = wrapAddress(addr);

        if (isErasedRecord(addr)) {
          currentAddr = addr;
          Serial.print(" -> Recovered to: 0x");
          Serial.println(currentAddr, HEX);
          writePtr = currentAddr;
          NVSConfig::setCSVWritePtr(writePtr);
          return;
        }
        advanced = true;
        scannedBytes += recordSize;
      }

      // If we didn't find an erased record inside the sector (should be rare), advance to next sector.
      uint32_t nextSector = sectorStart + FLASH_SECTOR_SIZE;
      if (nextSector >= flashCapacity) nextSector = BINARY_FLASH_START_ADDR;
      uint32_t delta;
      if (nextSector >= currentAddr) {
        delta = nextSector - currentAddr;
      } else {
        delta = (flashCapacity - currentAddr) + nextSector;
      }
      scannedBytes += delta;
      currentAddr = nextSector;
      yield();
    }

    // Scan limit hit: fall back to keeping existing pointer.
    if (advanced) {
      Serial.println(" -> Recovery scan limit hit; keeping NVS writePtr.");
    } else {
      Serial.println(" -> No recovery needed.");
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

    // RECOVERY SCAN: Check if we have valid data after the NVS write pointer
    // This handles cases where the device crashed before saving the pointer to NVS
    Serial.println("Checking for unsaved data after write pointer...");
    recoverWritePointer();
   
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
    // REMOVED: Writing to NVS every 10ms destroys the internal flash (16MB external flash is fine).
    // Now relying on explicit calls to savePointers() at appropriate intervals (e.g. before sleep).
    // NVSConfig::setCSVWritePtr(writePtr);
   
    return true;
  }

  // Save current pointers to NVS (Call this before sleep or periodically)
  void savePointers() {
    if (initialized) {
        NVSConfig::setCSVWritePtr(writePtr);
        NVSConfig::setCSVReadPtr(readPtr);
        // Serial.println("Storage pointers saved to NVS");
    }
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
  // recovery: Update internal pointer immediately. Only update NVS if saveToNVS is true.
  // Set saveToNVS=false for batch operations, then call savePointers() at the end.
  void markAsSent(size_t readingsCount, bool saveToNVS = true) {
    size_t bytesProcessed = readingsCount * sizeof(TimestampedIMUReading);
    readPtr = wrapAddress(readPtr + bytesProcessed);
    if (saveToNVS) {
      NVSConfig::setCSVReadPtr(readPtr);
    }
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

