// Cycle Handler - Manages data transmission cycle operations
// Handles GPS reading, BLE/WiFi transmission, and flash storage

#ifndef CYCLE_HANDLER_H
#define CYCLE_HANDLER_H

#include "imu_sensor.h"
#include "gps_sensor.h"
#include "spi_flash_handler.h"
#include "customwifi.h"
#include "time_sync.h"
#include "transmission_handler.h"
#include "nvs_config.h"
#include "battery_monitor.h"
#include "battery_indicator_led.h"
#include "ble_config.h"
#include "imu_flash_storage.h"
#include <Adafruit_NeoPixel.h>

// Forward declaration for function defined in main .ino file
extern void attemptTimeSyncIfNeeded();

// IMU data size constant (matches imu_flash_storage.h)
#define IMU_DATA_SIZE 28  // Size of TimestampedIMUData struct

// Structure to store IMU reading
struct TimestampedIMUData {
  IMUData data;
};

// Cycle result status
enum CycleResult {
  CYCLE_SUCCESS_BLE,      // Data sent via BLE
  CYCLE_SUCCESS_WIFI,     // Data sent via WiFi
  CYCLE_SUCCESS_STORED,   // Data stored to flash (no connection)
  CYCLE_FAILED            // Complete failure
};

class CycleHandler {
private:
  // Sensor instances (passed by reference)
  GPSSensor* gps;
  SPIFlashHandler* flash;
  TransmissionHandler* transmissionHandler;
  BatteryIndicatorLED* batteryLED;
  Adafruit_NeoPixel* statusLED;
  
  // Device info
  const char* deviceId;
  
  // Status LED control
  uint8_t statusLEDPixel;
  uint8_t restoreR, restoreG, restoreB;
  
  // Helper: Set status LED color
  void setStatusLED(uint8_t r, uint8_t g, uint8_t b) {
    if (statusLED) {
      statusLED->setPixelColor(statusLEDPixel, statusLED->Color(r, g, b));
      statusLED->show();
    }
  }
  
  // Helper: Turn off battery LED
  void turnOffBatteryLED() {
    if (batteryLED) {
      batteryLED->setColor(0, 0, 0);
    }
  }
  
  // Helper: Create CSV data from IMU array and GPS data
  String createSensorCSV(TimestampedIMUData* imuDataArray, int imuDataCount, GPSData gpsData, bool gpsActive) {
    // Dynamic buffer size calculation
    // Each IMU reading: {x|y|z|x|y|z|temp} = ~60-80 bytes average
    // Add 500 bytes for header and GPS data
    unsigned int estimatedSize = 500 + (imuDataCount * 80);
    
    // Check available heap and cap at reasonable limit (use 80% of free heap)
    size_t freeHeap = ESP.getFreeHeap();
    unsigned int maxSize = (freeHeap * 80) / 100;
    
    // Cap at 200KB maximum to prevent excessive memory usage
    if (maxSize > 200000) maxSize = 200000;
    
    // Use the smaller of estimated or available
    if (estimatedSize > maxSize) {
      Serial.print("WARNING: CSV buffer size capped at ");
      Serial.print(maxSize);
      Serial.print(" bytes (estimated ");
      Serial.print(estimatedSize);
      Serial.println(" bytes)");
      estimatedSize = maxSize;
    }
    
    char* csvBuffer = (char*)malloc(estimatedSize);
    if (!csvBuffer) {
      Serial.print("ERROR: Failed to allocate ");
      Serial.print(estimatedSize);
      Serial.println(" bytes for CSV buffer");
      Serial.print("Free heap: ");
      Serial.println(ESP.getFreeHeap());
      return String("");
    }
    
    unsigned long long currentMillis = TimeSync::getCurrentTimeMillis();
    float batteryVoltage = BatteryMonitor::readBatteryVoltage();
    int batteryLevel = BatteryMonitor::getBatteryPercentageV(batteryVoltage);
    
    // Get signal strength
    int signalStrength = -100;
    if (BLEConfig::isEnabled() && BLEConfig::isConnected()) {
      signalStrength = BLEConfig::getRSSI();
    } else if (CustomWiFi::isConnected()) {
      signalStrength = CustomWiFi::getRSSI();
    }
    
    // Calculate how many readings we can fit based on available buffer
    // Reserve space for header (~200 bytes) and GPS data (~200 bytes)
    unsigned int reservedSpace = 400;
    unsigned int availableForIMU = (estimatedSize > reservedSpace) ? (estimatedSize - reservedSpace) : 0;
    
    // Estimate bytes per reading (~80 bytes average)
    int maxReadings = availableForIMU / 80;
    if (maxReadings > imuDataCount) maxReadings = imuDataCount;
    
    // CSV header with device info
    // Format: device_id,battery_level,voltage,timestamp,signal_strength,imu_count,[imu_array],gps_active,{gps_data|...}
    int pos = snprintf(csvBuffer, estimatedSize,
             "%s,%d,%.3f,%llu,%d,%d,",
             deviceId,
             batteryLevel,
             batteryVoltage,
             currentMillis,
             signalStrength,
             maxReadings  // Use actual count that will fit
    );
    
    if (pos <= 0 || pos >= estimatedSize) {
      Serial.println("ERROR: Failed to write CSV header");
      free(csvBuffer);
      return String("");
    }
    
    // Add IMU data array - enclosed in square brackets
    // Add opening bracket
    if (pos < estimatedSize - 1) {
      csvBuffer[pos++] = '[';
    }
    
    // Track how many readings we actually add
    int readingsAdded = 0;
    for (int i = 0; i < maxReadings && pos < (estimatedSize - 200); i++) {
      int remaining = estimatedSize - pos;
      if (remaining < 100) break; // Need at least 100 bytes for closing bracket and GPS data
      
      int written = snprintf(csvBuffer + pos, remaining,
               "%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.2f%s",
               imuDataArray[i].data.accelerometer.x,
               imuDataArray[i].data.accelerometer.y,
               imuDataArray[i].data.accelerometer.z,
               imuDataArray[i].data.gyroscope.x,
               imuDataArray[i].data.gyroscope.y,
               imuDataArray[i].data.gyroscope.z,
               imuDataArray[i].data.temperature,
               (i < maxReadings - 1) ? ";" : ""
      );
      if (written > 0 && written < remaining) {
        pos += written;
        readingsAdded++;
      } else {
        // Buffer full or snprintf failed
        break;
      }
    }
    
    if (readingsAdded < maxReadings) {
      Serial.print("WARNING: Only ");
      Serial.print(readingsAdded);
      Serial.print(" of ");
      Serial.print(maxReadings);
      Serial.println(" IMU readings fit in buffer");
    }
    
    // Add closing bracket and comma
    if (pos < estimatedSize - 1) {
      csvBuffer[pos++] = ']';
      csvBuffer[pos++] = ',';
    }
    
    // Add GPS active flag
    if (pos < estimatedSize - 10) {
      int written = snprintf(csvBuffer + pos, estimatedSize - pos,
               "%s,",
               gpsActive ? "true" : "false"
      );
      if (written > 0) {
        pos += written;
      }
    }
    
    // Add GPS data as object with | separators
    if (pos < estimatedSize - 200) {
      int written = snprintf(csvBuffer + pos, estimatedSize - pos,
               "%s|%d|%llu|%d|%.7f|%.7f|%.2f|%.2f|%.2f|%.2f",
               gpsData.hasValidFix ? "true" : "false",
               gpsData.fixType,
               gpsData.lastFixTimeMillis,
               gpsData.satellites,
               gpsData.hasValidFix ? gpsData.latitude : 0.0,
               gpsData.hasValidFix ? gpsData.longitude : 0.0,
               gpsData.hasValidFix ? gpsData.altitude : 0.0,
               gpsData.hasValidFix ? gpsData.speed : 0.0,
               gpsData.hasValidFix ? gpsData.heading : 0.0,
               gpsData.hasValidFix ? gpsData.hdop : 0.0
      );
      if (written > 0) {
        pos += written;
      }
    }
    
    // Null terminate
    if (pos < estimatedSize) {
      csvBuffer[pos] = '\0';
    } else {
      // Buffer overflow - truncate at last safe position
      csvBuffer[estimatedSize - 1] = '\0';
      Serial.println("ERROR: CSV buffer overflow - data truncated");
    }
    
    // Verify we have valid data
    if (pos < 50) {
      Serial.print("ERROR: CSV data too short (");
      Serial.print(pos);
      Serial.println(" bytes)");
      free(csvBuffer);
      return String("");
    }
    
    String result = String(csvBuffer);
    free(csvBuffer);
    
    // Final validation
    if (result.length() == 0) {
      Serial.println("ERROR: String conversion failed - empty result");
      return String("");
    }
    
    return result;
  }
  
  // Helper: Create CSV data by reading IMU data from flash in chunks
  String createSensorCSVFromFlash(IMUFlashStorage* imuFlash, uint32_t totalReadings, GPSData gpsData, bool gpsActive) {
    if (imuFlash == nullptr || !imuFlash->isInitialized() || totalReadings == 0) {
      // Create CSV with no IMU data
      return createSensorCSV(nullptr, 0, gpsData, gpsActive);
    }
    
    // Read IMU data from flash in chunks (to avoid large RAM usage)
    const uint32_t CHUNK_SIZE = 100; // Read 100 readings at a time
    TimestampedIMUData* chunkBuffer = (TimestampedIMUData*)malloc(CHUNK_SIZE * sizeof(TimestampedIMUData));
    if (!chunkBuffer) {
      Serial.println("ERROR: Failed to allocate chunk buffer");
      return String("");
    }
    
    // Calculate buffer size for CSV (estimate)
    unsigned int estimatedSize = 500 + (totalReadings * 80);
    size_t freeHeap = ESP.getFreeHeap();
    unsigned int maxSize = (freeHeap * 80) / 100;
    if (maxSize > 200000) maxSize = 200000;
    if (estimatedSize > maxSize) estimatedSize = maxSize;
    
    char* csvBuffer = (char*)malloc(estimatedSize);
    if (!csvBuffer) {
      free(chunkBuffer);
      Serial.println("ERROR: Failed to allocate CSV buffer");
      return String("");
    }
    
    unsigned long long currentMillis = TimeSync::getCurrentTimeMillis();
    float batteryVoltage = BatteryMonitor::readBatteryVoltage();
    int batteryLevel = BatteryMonitor::getBatteryPercentageV(batteryVoltage);
    
    int signalStrength = -100;
    if (BLEConfig::isEnabled() && BLEConfig::isConnected()) {
      signalStrength = BLEConfig::getRSSI();
    } else if (CustomWiFi::isConnected()) {
      signalStrength = CustomWiFi::getRSSI();
    }
    
    // Write CSV header
    int pos = snprintf(csvBuffer, estimatedSize,
             "%s,%d,%.3f,%llu,%d,%u,",
             deviceId, batteryLevel, batteryVoltage, currentMillis, signalStrength, totalReadings);
    
    if (pos <= 0 || pos >= estimatedSize) {
      free(csvBuffer);
      free(chunkBuffer);
      return String("");
    }
    
    // Add opening bracket
    if (pos < estimatedSize - 1) {
      csvBuffer[pos++] = '[';
    }
    
    // Read and format IMU data in chunks
    uint32_t readPtr = imuFlash->getReadPtr();
    uint32_t readingsAdded = 0;
    uint32_t readingsRemaining = totalReadings;
    
    while (readingsRemaining > 0 && pos < (estimatedSize - 200)) {
      // Read chunk from flash
      uint32_t chunkSize = (readingsRemaining > CHUNK_SIZE) ? CHUNK_SIZE : readingsRemaining;
      uint32_t chunkRead = 0;
      
      for (uint32_t i = 0; i < chunkSize; i++) {
        if (imuFlash->readIMUReading(readPtr, &chunkBuffer[i])) {
          chunkRead++;
          readPtr += sizeof(TimestampedIMUData);
          // Wrap if needed (0x600000 is data queue start, 0x5FFFFF is IMU end)
          if (readPtr >= 0x600000) readPtr = 0x100000;
        } else {
          break;
        }
      }
      
      // Format chunk data into CSV
      for (uint32_t i = 0; i < chunkRead && pos < (estimatedSize - 200); i++) {
        int remaining = estimatedSize - pos;
        if (remaining < 100) break;
        
        int written = snprintf(csvBuffer + pos, remaining,
                 "{%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.2f}%s",
                 chunkBuffer[i].data.accelerometer.x,
                 chunkBuffer[i].data.accelerometer.y,
                 chunkBuffer[i].data.accelerometer.z,
                 chunkBuffer[i].data.gyroscope.x,
                 chunkBuffer[i].data.gyroscope.y,
                 chunkBuffer[i].data.gyroscope.z,
                 chunkBuffer[i].data.temperature,
                 (readingsAdded + i < totalReadings - 1) ? ";" : ""
        );
        
        if (written > 0 && written < remaining) {
          pos += written;
        } else {
          break;
        }
      }
      
      readingsAdded += chunkRead;
      readingsRemaining -= chunkRead;
      
      if (chunkRead < chunkSize) break; // Failed to read from flash
    }
    
    free(chunkBuffer);
    
    // Add closing bracket and comma
    if (pos < estimatedSize - 1) {
      csvBuffer[pos++] = ']';
      csvBuffer[pos++] = ',';
    }
    
    // Add GPS active flag
    if (pos < estimatedSize - 10) {
      int written = snprintf(csvBuffer + pos, estimatedSize - pos, "%s,", gpsActive ? "true" : "false");
      if (written > 0) pos += written;
    }
    
    // Add GPS data
    if (pos < estimatedSize - 200) {
      int written = snprintf(csvBuffer + pos, estimatedSize - pos,
               "{%s|%d|%llu|%d|%.7f|%.7f|%.2f|%.2f|%.2f|%.2f}",
               gpsData.hasValidFix ? "true" : "false",
               gpsData.fixType, gpsData.lastFixTimeMillis, gpsData.satellites,
               gpsData.hasValidFix ? gpsData.latitude : 0.0,
               gpsData.hasValidFix ? gpsData.longitude : 0.0,
               gpsData.hasValidFix ? gpsData.altitude : 0.0,
               gpsData.hasValidFix ? gpsData.speed : 0.0,
               gpsData.hasValidFix ? gpsData.heading : 0.0,
               gpsData.hasValidFix ? gpsData.hdop : 0.0);
      if (written > 0) pos += written;
    }
    
    // Null terminate
    if (pos < estimatedSize) {
      csvBuffer[pos] = '\0';
    } else {
      csvBuffer[estimatedSize - 1] = '\0';
    }
    
    String result = String(csvBuffer);
    free(csvBuffer);
    
    // Update read pointer in flash storage
    imuFlash->advanceReadPtr(readingsAdded);
    
    Serial.print("CSV created: ");
    Serial.print(readingsAdded);
    Serial.print(" of ");
    Serial.print(totalReadings);
    Serial.println(" readings included");
    
    return result;
  }
  
  // Helper: Send IMU data in multiple CSV chunks (handles large datasets)
  // Sends all readings across multiple CSV packets to avoid RAM limitations
  bool sendIMUDataInChunks(IMUFlashStorage* imuFlash, uint32_t totalReadings, GPSData gpsData, bool gpsActive, TransmissionHandler* txHandler) {
    if (imuFlash == nullptr || !imuFlash->isInitialized() || totalReadings == 0) {
      Serial.println("ERROR: Cannot send IMU data - invalid parameters");
      return false;
    }
    
    // Calculate optimal chunk size based on available RAM
    size_t freeHeap = ESP.getFreeHeap();
    Serial.print("Free heap before chunking: ");
    Serial.print(freeHeap);
    Serial.println(" bytes");
    
    // Reserve 50% of heap for system operations (more conservative due to fragmentation)
    size_t availableForCSV = (freeHeap * 50) / 100;
    // Cap at 20KB to avoid String constructor failures (matches DataQueue's 2048 byte approach)
    if (availableForCSV > 20000) availableForCSV = 20000;
    
    // Estimate: header (~200 bytes) + GPS (~200 bytes) + IMU data (~80 bytes per reading)
    // Calculate how many readings fit in available RAM
    const size_t HEADER_SIZE = 400; // Header + GPS data
    size_t availableForIMU = (availableForCSV > HEADER_SIZE) ? (availableForCSV - HEADER_SIZE) : 0;
    uint32_t readingsPerChunk = availableForIMU / 80; // ~80 bytes per reading in CSV
    
    // Limit chunk size to ~240 readings max (240 * 80 = ~19KB, safe for String)
    if (readingsPerChunk > 240) readingsPerChunk = 240;
    if (readingsPerChunk < 100) readingsPerChunk = 100; // Minimum 100 readings per chunk
    
    Serial.print("Readings per chunk: ");
    Serial.println(readingsPerChunk);
    
    // Calculate number of chunks needed
    uint32_t numChunks = (totalReadings + readingsPerChunk - 1) / readingsPerChunk; // Ceiling division
    Serial.print("Sending data in ");
    Serial.print(numChunks);
    Serial.println(" chunks");
    
    uint32_t readPtr = imuFlash->getReadPtr();
    uint32_t readingsSent = 0;
    bool allSent = true;
    
    for (uint32_t chunkNum = 0; chunkNum < numChunks; chunkNum++) {
      // Calculate readings for this chunk
      uint32_t readingsInChunk = (readingsSent + readingsPerChunk <= totalReadings) 
                                  ? readingsPerChunk 
                                  : (totalReadings - readingsSent);
      
      Serial.print("\n--- Chunk ");
      Serial.print(chunkNum + 1);
      Serial.print("/");
      Serial.print(numChunks);
      Serial.print(" (");
      Serial.print(readingsInChunk);
      Serial.println(" readings) ---");
      
      // Check RAM before creating chunk
      size_t heapBefore = ESP.getFreeHeap();
      Serial.print("Heap before chunk: ");
      Serial.println(heapBefore);
      
      // Create CSV for this chunk
      Serial.print("Creating CSV chunk...");
      String chunkCSV = createSensorCSVChunk(imuFlash, readPtr, readingsInChunk, 
                                              totalReadings, readingsSent, 
                                              gpsData, gpsActive, chunkNum, numChunks);
      
      if (chunkCSV.length() == 0) {
        Serial.print("ERROR: Failed to create chunk ");
        Serial.print(chunkNum + 1);
        Serial.print(" (readPtr=0x");
        Serial.print(readPtr, HEX);
        Serial.print(", readingsInChunk=");
        Serial.print(readingsInChunk);
        Serial.println(")");
        allSent = false;
        break;
      }
      
      Serial.print("OK (");
      Serial.print(chunkCSV.length());
      Serial.println(" bytes)");
      
      // Check RAM after creating chunk
      size_t heapAfter = ESP.getFreeHeap();
      Serial.print("Heap after chunk: ");
      Serial.print(heapAfter);
      Serial.print(" (used: ");
      Serial.print(heapBefore - heapAfter);
      Serial.println(" bytes)");
      
      Serial.print("Chunk CSV size: ");
      Serial.print(chunkCSV.length());
      Serial.println(" bytes");
      
      // Send this chunk
      bool sent = txHandler->handleDataTransmission(chunkCSV);
      
      if (!sent) {
        Serial.print("ERROR: Failed to send chunk ");
        Serial.println(chunkNum + 1);
        allSent = false;
        break;
      }
      
      // Update pointers
      readingsSent += readingsInChunk;
      readPtr += (readingsInChunk * sizeof(TimestampedIMUData));
      if (readPtr >= 0x600000) readPtr = 0x100000; // Wrap if needed
      
      Serial.print("Progress: ");
      Serial.print(readingsSent);
      Serial.print("/");
      Serial.print(totalReadings);
      Serial.print(" readings (");
      Serial.print((readingsSent * 100) / totalReadings);
      Serial.println("%)");
      
      // Small delay between chunks
      delay(100);
    }
    
    // Update read pointer in flash storage
    imuFlash->advanceReadPtr(readingsSent);
    
    if (allSent && readingsSent == totalReadings) {
      Serial.println("\n✓ All IMU data sent successfully!");
      return true;
    } else {
      Serial.print("\n⚠ Only ");
      Serial.print(readingsSent);
      Serial.print(" of ");
      Serial.print(totalReadings);
      Serial.println(" readings sent");
      return false;
    }
  }
  
  // Helper: Create CSV chunk with subset of IMU data
  String createSensorCSVChunk(IMUFlashStorage* imuFlash, uint32_t startReadPtr, 
                               uint32_t readingsInChunk, uint32_t totalReadings, 
                               uint32_t readingsSent, GPSData gpsData, bool gpsActive,
                               uint32_t chunkNum, uint32_t numChunks) {
    if (imuFlash == nullptr || !imuFlash->isInitialized()) {
      Serial.println("ERROR: IMU flash storage invalid");
      return String("");
    }
    
    // Allocate chunk buffer
    const uint32_t CHUNK_SIZE = 100;
    size_t chunkBufferSize = CHUNK_SIZE * sizeof(TimestampedIMUData);
    TimestampedIMUData* chunkBuffer = (TimestampedIMUData*)malloc(chunkBufferSize);
    if (!chunkBuffer) {
      Serial.print("ERROR: Failed to allocate chunk buffer (");
      Serial.print(chunkBufferSize);
      Serial.print(" bytes), free heap: ");
      Serial.println(ESP.getFreeHeap());
      return String("");
    }
    
    // Calculate CSV buffer size for this chunk
    // Be more conservative due to heap fragmentation
    size_t freeHeap = ESP.getFreeHeap();
    Serial.print("  Free heap: ");
    Serial.print(freeHeap);
    Serial.print(" bytes, ");
    
    // Use only 50% of free heap to account for fragmentation (was 70%)
    size_t maxSize = (freeHeap * 50) / 100;
    // Cap at 20KB to avoid String constructor failures (Arduino String has ~32KB limit)
    // DataQueue uses 2048 byte batches - we'll use similar approach
    if (maxSize > 20000) maxSize = 20000;
    
    unsigned int estimatedSize = 500 + (readingsInChunk * 80);
    if (estimatedSize > maxSize) {
      Serial.print("  Capping size from ");
      Serial.print(estimatedSize);
      Serial.print(" to ");
      Serial.print(maxSize);
      Serial.println(" bytes");
      estimatedSize = maxSize;
    }
    
    Serial.print("  Allocating CSV buffer: ");
    Serial.print(estimatedSize);
    Serial.println(" bytes");
    
    // Try allocation with retry at smaller sizes if it fails
    char* csvBuffer = nullptr;
    unsigned int trySize = estimatedSize;
    int retryCount = 0;
    
    while (csvBuffer == nullptr && retryCount < 3 && trySize >= 10000) {
      csvBuffer = (char*)malloc(trySize);
      if (!csvBuffer) {
        retryCount++;
        trySize = (trySize * 80) / 100; // Try 80% of previous size
        Serial.print("  Allocation failed, retrying with ");
        Serial.print(trySize);
        Serial.println(" bytes");
        delay(10); // Small delay between retries
      }
    }
    
    if (!csvBuffer) {
      free(chunkBuffer);
      Serial.print("ERROR: Failed to allocate CSV buffer after retries (tried ");
      Serial.print(estimatedSize);
      Serial.print(" bytes), free heap: ");
      Serial.println(ESP.getFreeHeap());
      Serial.print("Largest free block: ");
      Serial.println(ESP.getMaxAllocHeap());
      return String("");
    }
    
    // Update estimatedSize to actual allocated size
    if (trySize < estimatedSize) {
      estimatedSize = trySize;
      Serial.print("  Allocated smaller buffer: ");
      Serial.print(estimatedSize);
      Serial.println(" bytes");
    }
    
    unsigned long long currentMillis = TimeSync::getCurrentTimeMillis();
    float batteryVoltage = BatteryMonitor::readBatteryVoltage();
    int batteryLevel = BatteryMonitor::getBatteryPercentageV(batteryVoltage);
    
    int signalStrength = -100;
    if (BLEConfig::isEnabled() && BLEConfig::isConnected()) {
      signalStrength = BLEConfig::getRSSI();
    } else if (CustomWiFi::isConnected()) {
      signalStrength = CustomWiFi::getRSSI();
    }
    
    // Write CSV header with chunk info
    // Format: device_id,battery_level,voltage,timestamp,signal_strength,total_readings,chunk_num,total_chunks,
    int pos = snprintf(csvBuffer, estimatedSize,
             "%s,%d,%.3f,%llu,%d,%u,%u,%u,",
             deviceId, batteryLevel, batteryVoltage, currentMillis, signalStrength,
             totalReadings, chunkNum + 1, numChunks); // chunkNum + 1 for 1-based indexing
    
    if (pos <= 0 || pos >= estimatedSize) {
      Serial.print("ERROR: Failed to write CSV header (pos=");
      Serial.print(pos);
      Serial.print(", estimatedSize=");
      Serial.print(estimatedSize);
      Serial.println(")");
      free(csvBuffer);
      free(chunkBuffer);
      return String("");
    }
    
    Serial.print("  Header written: ");
    Serial.print(pos);
    Serial.println(" bytes");
    
    // Add opening bracket
    if (pos < estimatedSize - 1) {
      csvBuffer[pos++] = '[';
    }
    
    // Read and format IMU data
    uint32_t readPtr = startReadPtr;
    uint32_t readingsAdded = 0;
    uint32_t readingsRemaining = readingsInChunk;
    
    Serial.print("  Reading from flash at 0x");
    Serial.print(readPtr, HEX);
    Serial.print(", ");
    Serial.print(readingsInChunk);
    Serial.println(" readings");
    
    while (readingsRemaining > 0 && pos < (estimatedSize - 200)) {
      uint32_t chunkSize = (readingsRemaining > CHUNK_SIZE) ? CHUNK_SIZE : readingsRemaining;
      uint32_t chunkRead = 0;
      
      // Read chunk from flash
      for (uint32_t i = 0; i < chunkSize; i++) {
        if (imuFlash->readIMUReading(readPtr, &chunkBuffer[i])) {
          chunkRead++;
          readPtr += sizeof(TimestampedIMUData);
          if (readPtr >= 0x600000) readPtr = 0x100000;
        } else {
          Serial.print("  ERROR: Failed to read IMU at 0x");
          Serial.println(readPtr, HEX);
          break;
        }
      }
      
      if (chunkRead == 0) {
        Serial.println("  ERROR: No readings read from flash");
        break;
      }
      
      // Format chunk data into CSV
      for (uint32_t i = 0; i < chunkRead && pos < (estimatedSize - 200); i++) {
        int remaining = estimatedSize - pos;
        if (remaining < 100) {
          Serial.print("  WARNING: Buffer nearly full (");
          Serial.print(remaining);
          Serial.println(" bytes remaining)");
          break;
        }
        
        // Check if this is the last reading in the entire dataset
        bool isLastInDataset = (readingsSent + readingsAdded + i == totalReadings - 1);
        // Check if this is the last reading in this chunk
        bool isLastInChunk = (readingsAdded + i == readingsInChunk - 1);
        
        int written = snprintf(csvBuffer + pos, remaining,
                 "%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.2f%s",
                 chunkBuffer[i].data.accelerometer.x,
                 chunkBuffer[i].data.accelerometer.y,
                 chunkBuffer[i].data.accelerometer.z,
                 chunkBuffer[i].data.gyroscope.x,
                 chunkBuffer[i].data.gyroscope.y,
                 chunkBuffer[i].data.gyroscope.z,
                 chunkBuffer[i].data.temperature,
                 (!isLastInDataset && !isLastInChunk) ? ";" : ""
        );
        
        if (written > 0 && written < remaining) {
          pos += written;
        } else {
          Serial.print("  WARNING: snprintf failed or overflow (written=");
          Serial.print(written);
          Serial.print(", remaining=");
          Serial.print(remaining);
          Serial.println(")");
          break;
        }
      }
      
      readingsAdded += chunkRead;
      readingsRemaining -= chunkRead;
      
      if (chunkRead < chunkSize) break;
    }
    
    Serial.print("  Readings added to CSV: ");
    Serial.print(readingsAdded);
    Serial.print("/");
    Serial.print(readingsInChunk);
    Serial.println();
    
    free(chunkBuffer);
    chunkBuffer = nullptr; // Prevent double-free
    
    // Validate we got some readings
    if (readingsAdded == 0) {
      Serial.println("ERROR: No readings were added to CSV");
      free(csvBuffer);
      return String("");
    }
    
    // Add closing bracket and comma
    if (pos < estimatedSize - 1) {
      csvBuffer[pos++] = ']';
      csvBuffer[pos++] = ',';
    }
    
    // Add GPS active flag
    if (pos < estimatedSize - 10) {
      int written = snprintf(csvBuffer + pos, estimatedSize - pos, "%s,", gpsActive ? "true" : "false");
      if (written > 0) pos += written;
    }
    
    // Add GPS data (only in first chunk to avoid duplication)
    if (chunkNum == 0 && pos < estimatedSize - 200) {
      int written = snprintf(csvBuffer + pos, estimatedSize - pos,
               "%s|%d|%llu|%d|%.7f|%.7f|%.2f|%.2f|%.2f|%.2f",
               gpsData.hasValidFix ? "true" : "false",
               gpsData.fixType, gpsData.lastFixTimeMillis, gpsData.satellites,
               gpsData.hasValidFix ? gpsData.latitude : 0.0,
               gpsData.hasValidFix ? gpsData.longitude : 0.0,
               gpsData.hasValidFix ? gpsData.altitude : 0.0,
               gpsData.hasValidFix ? gpsData.speed : 0.0,
               gpsData.hasValidFix ? gpsData.heading : 0.0,
               gpsData.hasValidFix ? gpsData.hdop : 0.0);
      if (written > 0) pos += written;
    }
    
    // Null terminate
    if (pos < estimatedSize) {
      csvBuffer[pos] = '\0';
    } else {
      csvBuffer[estimatedSize - 1] = '\0';
    }
    
    // Validate final buffer
    if (pos < 50) {
      Serial.print("ERROR: CSV buffer too short (");
      Serial.print(pos);
      Serial.println(" bytes)");
      free(csvBuffer);
      return String("");
    }
    
    // Buffer should be <= 20KB (capped earlier) so String can handle it
    // Arduino String constructor can handle up to ~32KB, but we cap at 20KB for safety
    String result = String(csvBuffer);
    free(csvBuffer);
    
    if (result.length() == 0) {
      Serial.print("ERROR: String conversion failed (buffer size: ");
      Serial.print(pos);
      Serial.println(" bytes)");
      return String("");
    }
    
    Serial.print("  CSV created: ");
    Serial.print(result.length());
    Serial.println(" bytes");
    
    return result;
  }

public:
  // Constructor
  CycleHandler(const char* devId, GPSSensor* gpsRef, SPIFlashHandler* flashRef, 
               TransmissionHandler* txHandler, BatteryIndicatorLED* batLED, 
               Adafruit_NeoPixel* statLED, uint8_t ledPixel) {
    deviceId = devId;
    gps = gpsRef;
    flash = flashRef;
    transmissionHandler = txHandler;
    batteryLED = batLED;
    statusLED = statLED;
    statusLEDPixel = ledPixel;
    restoreR = 0;
    restoreG = 255;
    restoreB = 0;
  }
  
  // Set status LED restore color
  void setStatusLEDRestoreColor(uint8_t r, uint8_t g, uint8_t b) {
    restoreR = r;
    restoreG = g;
    restoreB = b;
  }
  
  // Main cycle execution - reads IMU data from flash in chunks
  // Parameters: IMU flash storage handler
  // Returns: CycleResult indicating transmission outcome
  CycleResult executeCycleFromFlash(IMUFlashStorage* imuFlash) {
    if (imuFlash == nullptr || !imuFlash->isInitialized()) {
      Serial.println("ERROR: IMU flash storage not available");
      return CYCLE_FAILED;
    }
    
    // Turn on LEDs when cycle starts
    setStatusLED(restoreR, restoreG, restoreB);
    if (batteryLED && batteryLED->isEnabled()) {
      batteryLED->updateBatteryLED();
    }
    
    uint32_t totalReadings = imuFlash->getTotalReadings();
    Serial.println("========== CYCLE EXECUTION START ==========");
    Serial.print("IMU readings to transmit: ");
    Serial.println(totalReadings);
    
    if (totalReadings == 0) {
      Serial.println("WARNING: No IMU readings available");
    }
    
    // ========== STEP 1: GPS DATA COLLECTION ==========
    Serial.println("Step 1: Collecting GPS data...");
    
    // Check GPS active setting (controls GPS on/off)
    uint8_t gpsActive = NVSConfig::getGPSActive(); // 1 = GPS ON, 0 = GPS OFF
    GPSData gpsData = GPSData(); // Default empty GPS data
    
    if (gpsActive == 1) {
      Serial.println("GPS active - Reading GPS data...");
      if (gps) {
        gps->sendHotStartIfAvailable();
        // Update GPS for 1 second to get fresh data
        for (int i = 0; i < 50; i++) {
          gps->update();
          delay(20);
        }
        gpsData = gps->getGPSData();
        Serial.print("GPS fix: ");
        Serial.println(gpsData.hasValidFix ? "YES" : "NO");
      } else {
        Serial.println("GPS sensor not available");
      }
    } else {
      Serial.println("GPS inactive - Skipping GPS data collection");
    }
    
    // ========== STEP 2: BLE TRANSMISSION ATTEMPT ==========
    Serial.println("\nStep 2: Attempting BLE transmission...");
    
    // Check if BLE was already on (indicates first cycle - BLE stays on during first cycle period)
    bool bleWasAlreadyOn = BLEConfig::isEnabled();
    
    // Turn on BLE if not already on
    if (!BLEConfig::isEnabled()) {
      Serial.println("Starting BLE...");
      BLEConfig::begin();
      delay(500); // Give BLE time to initialize
    }
    
    // Check for connection for 5 seconds
    Serial.println("Waiting for BLE connection (5 seconds)...");
    unsigned long bleStartTime = millis();
    bool bleConnected = false;
    while (millis() - bleStartTime < 5000) {
      BLEConfig::update();
      if (BLEConfig::isConnected()) {
        bleConnected = true;
        Serial.println("BLE connected!");
        break;
      }
      delay(100);
    }
    
    if (bleConnected) {
      Serial.println("Sending IMU data in chunks via BLE...");
      // Send all IMU data in multiple CSV chunks
      bool bleSent = sendIMUDataInChunks(imuFlash, totalReadings, gpsData, gpsActive == 1, transmissionHandler);
      
      if (bleSent) {
        Serial.println("SUCCESS: Data sent via BLE");
        setStatusLED(0, 255, 0); // Green - success
        delay(1000);
        
        // For first cycle, keep BLE on (main loop will turn it off after first cycle period)
        // For subsequent cycles, turn off BLE after successful transmission
        if (!bleWasAlreadyOn) {
          // BLE was started by cycle handler - turn it off for subsequent cycles
          Serial.println("Turning off BLE...");
          BLEConfig::stop();
        } else {
          // BLE was already on (first cycle) - keep it on, main loop will manage timing
          Serial.println("First cycle: Keeping BLE on (main loop will manage first cycle timing)...");
        }
        
        // LED already restored by stopStatusLEDBlink() in BLEConfig::sendDataViaBLE()
        
        // Turn off LEDs when cycle completes
        setStatusLED(0, 0, 0);
        turnOffBatteryLED();
        
        Serial.println("========== CYCLE EXECUTION END (BLE) ==========\n");
        return CYCLE_SUCCESS_BLE;
      } else {
        Serial.println("FAILED: BLE transmission failed");
      }
    } else {
      Serial.println("No BLE connection within 5 seconds");
    }
    
    // For first cycle, keep BLE on even if disconnected (allows reconnection during wait period)
    // For subsequent cycles, turn off BLE if no connection
    if (!bleWasAlreadyOn) {
      // BLE was started by cycle handler - turn it off for subsequent cycles
      Serial.println("Turning off BLE...");
      BLEConfig::stop();
    } else {
      // BLE was already on (first cycle) - keep it on for potential reconnection
      Serial.println("First cycle: Keeping BLE on (waiting for potential reconnection)...");
    }
    
    // ========== STEP 3: WIFI TRANSMISSION ATTEMPT ==========
    Serial.println("\nStep 3: Attempting WiFi transmission...");
    
    // Check if WiFi credentials available
    String ssid = NVSConfig::getWiFiSSID();
    String password = NVSConfig::getWiFiPassword();
    
    if (ssid.length() > 0 && password.length() > 0) {
      Serial.println("WiFi credentials found");
      
      // Connect to WiFi
      Serial.println("Connecting to WiFi...");
      bool wifiConnected = CustomWiFi::connectWiFi();
      
      if (wifiConnected) {
        Serial.println("WiFi connected!");
        
        delay(1000); // Wait for WiFi to stabilize
        
        // Verify WiFi is still connected before transmission
        if (!CustomWiFi::isConnected()) {
          Serial.println("ERROR: WiFi disconnected after connection - reconnecting...");
          wifiConnected = CustomWiFi::connectWiFi();
          if (!wifiConnected) {
            Serial.println("ERROR: Failed to reconnect WiFi");
          } else {
            delay(500); // Brief delay after reconnect
          }
        }
        
        // Attempt time sync if needed
        attemptTimeSyncIfNeeded();
        
        // Verify WiFi connection one more time before sending
        if (CustomWiFi::isConnected()) {
          Serial.println("WiFi verified connected - proceeding with transmission");
        } else {
          Serial.println("ERROR: WiFi not connected before transmission attempt");
        }
        
        // Send data
        Serial.println("Sending IMU data in chunks via WiFi...");
        // Send all IMU data in multiple CSV chunks
        bool wifiSent = sendIMUDataInChunks(imuFlash, totalReadings, gpsData, gpsActive == 1, transmissionHandler);
        
        // Turn off WiFi
        Serial.println("Turning off WiFi...");
        CustomWiFi::disconnectWiFi();
        
        if (wifiSent) {
          Serial.println("SUCCESS: Data sent via WiFi");
          setStatusLED(0, 255, 0); // Green - success
          delay(1000);
          
          // LED already restored by stopStatusLEDBlink() in CustomWiFi::sendSensorData()
          
          // Turn off LEDs when cycle completes
          setStatusLED(0, 0, 0);
          turnOffBatteryLED();
          
          Serial.println("========== CYCLE EXECUTION END (WiFi) ==========\n");
          return CYCLE_SUCCESS_WIFI;
        } else {
          Serial.println("FAILED: WiFi transmission failed");
        }
      } else {
        Serial.println("WiFi connection failed");
      }
    } else {
      Serial.println("No WiFi credentials found");
    }
    
    // ========== STEP 4: FLASH STORAGE (FALLBACK) ==========
    Serial.println("\nStep 4: Storing data to flash in chunks (no connection)...");
    
    if (flash && flash->isInitialized()) {
      // Store all IMU data in chunks to flash
      // Each chunk is stored separately and will be sent when connection is available
      bool stored = sendIMUDataInChunks(imuFlash, totalReadings, gpsData, gpsActive == 1, transmissionHandler);
      transmissionHandler->saveQueueState();
      
      if (stored) {
        Serial.println("WARNING: All data chunks queued to flash (will retry in next cycle)");
      } else {
        Serial.println("ERROR: Failed to queue some data chunks to flash");
        setStatusLED(255, 0, 0); // Red - error
        delay(1000);
      }
      
      // Turn off LEDs when cycle completes
      setStatusLED(0, 0, 0);
      turnOffBatteryLED();
      
      Serial.println("========== CYCLE EXECUTION END (Flash) ==========\n");
      return stored ? CYCLE_SUCCESS_STORED : CYCLE_FAILED;
    } else {
      Serial.println("ERROR: Flash not available");
      setStatusLED(255, 0, 0); // Red - error
      delay(2000);
      
      // Turn off LEDs when cycle completes (even on failure)
      setStatusLED(0, 0, 0);
      turnOffBatteryLED();
      
      Serial.println("========== CYCLE EXECUTION END (Failed) ==========\n");
      return CYCLE_FAILED;
    }
  }
  
  // Update battery LED (call periodically in main loop)
  void updateBatteryLED() {
    if (batteryLED && batteryLED->isEnabled()) {
      batteryLED->updateBatteryLED();
      
      // Double blink if battery low
      int batteryPercent = BatteryMonitor::getBatteryPercentage();
      if (batteryPercent < 10) {
        batteryLED->doubleBlink();
      }
    }
  }
};

#endif

