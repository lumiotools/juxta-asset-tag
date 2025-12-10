// IMU Flash Storage - Manages IMU data storage in external flash
// Partition: 5MB (0x100000 - 0x5FFFFF) for IMU readings

#ifndef IMU_FLASH_STORAGE_H
#define IMU_FLASH_STORAGE_H

#include "spi_flash_handler.h"
#include "imu_sensor.h"
#include "nvs_config.h"

// Forward declaration - struct defined in cycle_handler.h
struct TimestampedIMUData;

// Flash memory partitioning:
// 0x00000 - 0xFFFFF (1MB): Buffer/General use
// 0x100000 - 0x5FFFFF (5MB): IMU readings storage
// 0x600000 - 0xFFFFFF (10MB): Data queue storage (failed transmissions)

#define IMU_FLASH_START_ADDR 0x100000  // 1MB offset
#define IMU_FLASH_SIZE 0x500000        // 5MB = 5,242,880 bytes
#define IMU_FLASH_END_ADDR (IMU_FLASH_START_ADDR + IMU_FLASH_SIZE)

// TimestampedIMUData structure: IMUData (7 floats = 28 bytes) + no timestamp field currently
// IMUData: 3 floats (accel) + 3 floats (gyro) + 1 float (temp) = 7 * 4 = 28 bytes
#define IMU_DATA_SIZE 28  // Size of TimestampedIMUData struct
#define MAX_IMU_READINGS (IMU_FLASH_SIZE / IMU_DATA_SIZE)  // ~187,245 readings

class IMUFlashStorage {
private:
  SPIFlashHandler* flashHandler;
  bool initialized;
  
  // Circular buffer pointers
  uint32_t writePtr;      // Where to write next IMU reading
  uint32_t readPtr;       // Where to read from (for transmission)
  uint32_t totalReadings; // Total readings stored (wraps at max)
  
  // Sector management (4KB sectors)
  uint32_t lastErasedSector;
  
  // Ensure sector is erased before writing
  void ensureSectorErased(uint32_t addr) {
    if (addr < IMU_FLASH_START_ADDR || addr >= IMU_FLASH_END_ADDR) {
      return; // Invalid address
    }
    
    uint32_t sectorStart = (addr / 4096) * 4096;
    
    // Check if we need to erase this sector
    if (sectorStart != lastErasedSector) {
      if (sectorStart >= IMU_FLASH_START_ADDR && sectorStart < IMU_FLASH_END_ADDR) {
        if (!flashHandler->eraseSector(sectorStart)) {
          Serial.print("ERROR: Failed to erase sector at 0x");
          Serial.println(sectorStart, HEX);
          return;
        }
        delay(50); // Longer delay for erase to complete (sector erase takes time)
        lastErasedSector = sectorStart;
      }
    }
  }
  
  // Wrap address for circular buffer
  uint32_t wrapAddress(uint32_t addr) {
    if (addr >= IMU_FLASH_END_ADDR) {
      return IMU_FLASH_START_ADDR + (addr - IMU_FLASH_END_ADDR);
    }
    return addr;
  }

public:
  IMUFlashStorage() : flashHandler(nullptr), initialized(false),
                      writePtr(IMU_FLASH_START_ADDR), readPtr(IMU_FLASH_START_ADDR),
                      totalReadings(0), lastErasedSector(0) {}
  
  bool begin(SPIFlashHandler* handler) {
    if (initialized) return true;
    
    if (handler == nullptr || !handler->isInitialized()) {
      Serial.println("IMUFlashStorage: Invalid flash handler");
      return false;
    }
    
    flashHandler = handler;
    
    // Load pointers from NVS
    writePtr = NVSConfig::getIMUWritePtr();
    readPtr = NVSConfig::getIMUReadPtr();
    totalReadings = NVSConfig::getIMUTotalReadings();
    
    // Validate pointers
    if (writePtr < IMU_FLASH_START_ADDR || writePtr >= IMU_FLASH_END_ADDR) {
      Serial.print("WARNING: Invalid writePtr from NVS (0x");
      Serial.print(writePtr, HEX);
      Serial.println(") - resetting to start");
      writePtr = IMU_FLASH_START_ADDR;
      NVSConfig::setIMUWritePtr(writePtr);
    }
    if (readPtr < IMU_FLASH_START_ADDR || readPtr >= IMU_FLASH_END_ADDR) {
      Serial.print("WARNING: Invalid readPtr from NVS (0x");
      Serial.print(readPtr, HEX);
      Serial.println(") - resetting to start");
      readPtr = IMU_FLASH_START_ADDR;
      NVSConfig::setIMUReadPtr(readPtr);
    }
    
    Serial.println("========== IMU Flash Storage ==========");
    Serial.print("IMU Flash Region: 0x");
    Serial.print(IMU_FLASH_START_ADDR, HEX);
    Serial.print(" - 0x");
    Serial.print(IMU_FLASH_END_ADDR, HEX);
    Serial.print(" (");
    Serial.print(IMU_FLASH_SIZE / 1024 / 1024);
    Serial.println("MB)");
    Serial.print("Write Pointer: 0x");
    Serial.println(writePtr, HEX);
    Serial.print("Read Pointer: 0x");
    Serial.println(readPtr, HEX);
    Serial.print("Total Readings: ");
    Serial.println(totalReadings);
    Serial.print("Capacity: ~");
    Serial.print(MAX_IMU_READINGS);
    Serial.println(" readings");
    
    initialized = true;
    return true;
  }
  
  // Write IMU reading to flash (circular buffer)
  // Note: imuData must point to a TimestampedIMUData struct
  bool writeIMUReading(const void* imuData) {
    if (!initialized || flashHandler == nullptr) {
      Serial.println("ERROR: IMU flash storage not initialized");
      return false;
    }
    
    // Validate write pointer
    if (writePtr < IMU_FLASH_START_ADDR || writePtr >= IMU_FLASH_END_ADDR) {
      Serial.print("ERROR: Invalid write pointer: 0x");
      Serial.println(writePtr, HEX);
      writePtr = IMU_FLASH_START_ADDR; // Reset to start
      return false;
    }
    
    // Ensure sector is erased
    ensureSectorErased(writePtr);
    
    // Check if write will cross sector boundary
    uint32_t writeEnd = writePtr + IMU_DATA_SIZE;
    uint32_t currentSector = (writePtr / 4096) * 4096;
    uint32_t endSector = (writeEnd / 4096) * 4096;
    
    // If crossing sector boundary, erase next sector too
    if (endSector > currentSector && endSector < IMU_FLASH_END_ADDR) {
      if (endSector != lastErasedSector) {
        if (!flashHandler->eraseSector(endSector)) {
          Serial.print("ERROR: Failed to erase next sector at 0x");
          Serial.println(endSector, HEX);
        } else {
          delay(50);
          lastErasedSector = endSector;
        }
      }
    }
    
    // Write IMU data
    const uint8_t* dataPtr = (const uint8_t*)imuData;
    if (!flashHandler->writeCharArray(writePtr, (const char*)dataPtr, IMU_DATA_SIZE)) {
      Serial.print("ERROR: Failed to write IMU data to flash at 0x");
      Serial.print(writePtr, HEX);
      Serial.print(" (size: ");
      Serial.print(IMU_DATA_SIZE);
      Serial.println(" bytes)");
      return false;
    }
    
    // Update pointers
    writePtr += IMU_DATA_SIZE;
    writePtr = wrapAddress(writePtr);
    
    totalReadings++;
    if (totalReadings > MAX_IMU_READINGS) {
      totalReadings = MAX_IMU_READINGS; // Cap at max
      // Update read pointer if we've wrapped (oldest data overwritten)
      if (readPtr < writePtr || (writePtr < readPtr && totalReadings == MAX_IMU_READINGS)) {
        readPtr = writePtr; // Oldest data is at writePtr now
      }
    }
    
    // Save pointers to NVS periodically (every 100 readings to reduce wear)
    if (totalReadings % 100 == 0) {
      NVSConfig::setIMUWritePtr(writePtr);
      NVSConfig::setIMUReadPtr(readPtr);
      NVSConfig::setIMUTotalReadings(totalReadings);
    }
    
    return true;
  }
  
  // Read IMU reading from flash
  // Note: imuData must point to a TimestampedIMUData struct
  bool readIMUReading(uint32_t addr, void* imuData) {
    if (!initialized || flashHandler == nullptr) return false;
    if (addr < IMU_FLASH_START_ADDR || addr >= IMU_FLASH_END_ADDR) return false;
    
    uint8_t* dataPtr = (uint8_t*)imuData;
    return flashHandler->readCharArray(addr, (char*)dataPtr, IMU_DATA_SIZE);
  }
  
  // Get current read pointer (for transmission)
  uint32_t getReadPtr() const { return readPtr; }
  
  // Get write pointer
  uint32_t getWritePtr() const { return writePtr; }
  
  // Get total readings count
  uint32_t getTotalReadings() const { return totalReadings; }
  
  // Reset read pointer to start of valid data (for new cycle)
  void resetReadPtr() {
    if (totalReadings < MAX_IMU_READINGS) {
      // Not full yet - start from beginning
      readPtr = IMU_FLASH_START_ADDR;
    } else {
      // Full - start from writePtr (oldest data)
      readPtr = writePtr;
    }
    NVSConfig::setIMUReadPtr(readPtr);
  }
  
  // Clear all IMU data (reset pointers)
  void clear() {
    writePtr = IMU_FLASH_START_ADDR;
    readPtr = IMU_FLASH_START_ADDR;
    totalReadings = 0;
    lastErasedSector = 0;
    
    NVSConfig::setIMUWritePtr(writePtr);
    NVSConfig::setIMUReadPtr(readPtr);
    NVSConfig::setIMUTotalReadings(totalReadings);
    
    Serial.println("IMU flash storage cleared");
  }
  
  // Advance read pointer (after reading data for transmission)
  void advanceReadPtr(uint32_t count) {
    readPtr += (count * IMU_DATA_SIZE);
    readPtr = wrapAddress(readPtr);
    NVSConfig::setIMUReadPtr(readPtr);
  }
  
  bool isInitialized() const { return initialized; }
};

#endif

