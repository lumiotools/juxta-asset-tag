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
#include "unified_csv_storage.h"
#include "flash_reader.h"
#include "gps_scenario_handler.h"
#include "customwifi.h"
#include <Adafruit_NeoPixel.h>

// Forward declaration for function defined in main .ino file
extern void attemptTimeSyncIfNeeded();

// LEGACY CODE - COMMENTED OUT (replaced by unified CSV storage)
/*
// IMU data size constant (matches imu_flash_storage.h)
#define IMU_DATA_SIZE 28  // Size of TimestampedIMUData struct

// Structure to store IMU reading
struct TimestampedIMUData {
  IMUData data;
};
*/

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
  GPSScenarioHandler* gpsScenarioHandler;
  
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
  
  // LEGACY CODE - COMMENTED OUT (replaced by unified CSV storage)
  /*
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
  
  // LEGACY CODE - COMMENTED OUT (replaced by unified CSV storage)
  /*
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
  */
  
  // LEGACY CODE - COMMENTED OUT (replaced by unified CSV storage)
  /*
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
  */
  
  // LEGACY CODE - COMMENTED OUT (replaced by unified CSV storage)
  /*
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
  */

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
    gpsScenarioHandler = nullptr;
    restoreR = 0;
    restoreG = 255;
    restoreB = 0;
  }
  
  // Set GPS scenario handler
  void setGPSScenarioHandler(GPSScenarioHandler* scenarioHandler) {
    gpsScenarioHandler = scenarioHandler;
  }
  
  // Set status LED restore color
  void setStatusLEDRestoreColor(uint8_t r, uint8_t g, uint8_t b) {
    restoreR = r;
    restoreG = g;
    restoreB = b;
  }
  
  // LEGACY CODE - COMMENTED OUT (replaced by executeCycleFromUnifiedCSV)
  /*
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
  */
  
  // Helper: Send IMU batch to model server with lat/long prefix
  // Format: (lat, long), imuObj1, imuObj2, ...
  bool sendBatchToModelServer(const String& imuData, double currentLat, double currentLon, double& deltaLat, double& deltaLon) {
    // Create prefixed data: (lat, long), imuData
    String prefixedData = "(";
    prefixedData += String(currentLat, 7);
    prefixedData += ",";
    prefixedData += String(currentLon, 7);
    prefixedData += "),";
    prefixedData += imuData;
    
    Serial.print("Sending batch to model server with position (");
    Serial.print(currentLat, 7);
    Serial.print(", ");
    Serial.print(currentLon, 7);
    Serial.print("), IMU data length: ");
    Serial.print(imuData.length());
    Serial.println(" bytes");
    
    return CustomWiFi::sendToModelServer(prefixedData, deltaLat, deltaLon);
  }
  
  // Helper: Send position to backend server (BLE or WiFi)
  bool sendPositionToBackend(double lat, double lon, GPSScenario scenario) {
    // Get device ID, battery percentage, and timestamp
    const char* devId = (deviceId != nullptr) ? deviceId : "Unknown";
    int batteryPercent = BatteryMonitor::getBatteryPercentage();
    unsigned long long timestamp = TimeSync::getCurrentTimeMillis();
    
    // Create position data string with all required fields
    // Format: device_id,battery%,timestamp,scenario,lat,lon
    char positionData[200];
    snprintf(positionData, sizeof(positionData), "%s,%d,%llu,%d,%.7f,%.7f", 
             devId, batteryPercent, timestamp, scenario, lat, lon);
    
    Serial.print("Sending position to backend server: device_id=");
    Serial.print(devId);
    Serial.print(", battery=");
    Serial.print(batteryPercent);
    Serial.print("%, timestamp=");
    Serial.print(timestamp);
    Serial.print(", scenario=");
    Serial.print(scenario);
    Serial.print(", location=(");
    Serial.print(lat, 7);
    Serial.print(", ");
    Serial.print(lon, 7);
    Serial.println(")");
    
    return transmissionHandler->handleDataTransmission(String(positionData));
  }
  
  // Main cycle execution - reads CSV entries from unified storage and sends directly
  // Parameters: Unified CSV storage handler
  // Returns: CycleResult indicating transmission outcome
  CycleResult executeCycleFromUnifiedCSV(UnifiedCSVStorage* csvStorage) {
    if (csvStorage == nullptr || !csvStorage->isInitialized()) {
      Serial.println("ERROR: Unified CSV storage not available");
      return CYCLE_FAILED;
    }
    
    // Turn on LEDs when cycle starts
    setStatusLED(restoreR, restoreG, restoreB);
    if (batteryLED && batteryLED->isEnabled()) {
      batteryLED->updateBatteryLED();
    }
    
    Serial.println("========== CYCLE EXECUTION START ==========");
    
    // Get current scenario and check accuracy at transmission cycle time
    GPSScenario currentScenario = SCENARIO_NONE;
    if (gpsScenarioHandler != nullptr) {
      // Check GPS fix lost during Scenario 1 (only at transmission time)
      if (gpsScenarioHandler->getCurrentScenario() == SCENARIO_1_HIGH_ACCURACY) {
        gpsScenarioHandler->checkGPSFixLost();
      }
      
      // Determine scenario based on current GPS status (only at transmission time)
      currentScenario = gpsScenarioHandler->determineScenario();
      Serial.print("Current GPS Scenario: ");
      Serial.println(currentScenario);
    }
    
    // Get current storage state
    uint32_t readPtr = csvStorage->getReadPtr();
    uint32_t writePtr = csvStorage->getWritePtr();
    Serial.print("Storage state - ReadPtr: 0x");
    Serial.print(readPtr, HEX);
    Serial.print(", WritePtr: 0x");
    Serial.print(writePtr, HEX);
    Serial.println();
    
    if (!csvStorage->hasDataToRead()) {
      Serial.println("WARNING: No CSV entries available (readPtr == writePtr)");
      setStatusLED(0, 0, 0);
      turnOffBatteryLED();
      return CYCLE_SUCCESS_STORED; // No data to send is not a failure
    }
    
    // Scenario-based transmission logic
    if (currentScenario == SCENARIO_1_HIGH_ACCURACY) {
      // Scenario 1: Send high accuracy GPS to backend server
      Serial.println("\n========== SCENARIO 1: HIGH ACCURACY GPS ==========");
      
      if (gps != nullptr && gpsScenarioHandler != nullptr) {
        GPSData gpsData = gps->getGPSData();
        if (gpsData.hasValidFix) {
          // Send high accuracy GPS position to backend server
          bool sent = sendPositionToBackend(gpsData.latitude, gpsData.longitude, currentScenario);
          
          if (sent) {
            // Update reference position to current high accuracy GPS
            gpsScenarioHandler->updatePositionAfterTransmission(gpsData.latitude, gpsData.longitude);
            Serial.println("High accuracy GPS position sent to backend server");
            setStatusLED(0, 255, 0);
            delay(1000);
            setStatusLED(0, 0, 0);
            turnOffBatteryLED();
            return CYCLE_SUCCESS_WIFI; // Or BLE depending on what was used
          }
        }
      }
    } else if (currentScenario == SCENARIO_2_LOW_ACCURACY || currentScenario == SCENARIO_4_UI_POSITION) {
      // Scenario 2 or 4: Send IMU data to model server with lat/long prefix
      Serial.println("\n========== SCENARIO 2/4: MODEL SERVER TRANSMISSION ==========");
      
      if (gpsScenarioHandler == nullptr || !gpsScenarioHandler->hasReferencePosition()) {
        Serial.println("ERROR: No reference position available for Scenario 2/4");
        setStatusLED(0, 0, 0);
        turnOffBatteryLED();
        return CYCLE_FAILED;
      }
      
      // Get reference position from NVS (last known position)
      double currentLat, currentLon;
      gpsScenarioHandler->getReferencePosition(currentLat, currentLon);
      
      Serial.print("Starting position from NVS: (");
      Serial.print(currentLat, 7);
      Serial.print(", ");
      Serial.print(currentLon, 7);
      Serial.println(")");
      
      // Connect to WiFi for model server (WiFi only for model server)
      String ssid = NVSConfig::getWiFiSSID();
      String password = NVSConfig::getWiFiPassword();
      
      if (ssid.length() == 0 || password.length() == 0) {
        Serial.println("ERROR: WiFi credentials not available for model server");
        setStatusLED(0, 0, 0);
        turnOffBatteryLED();
        return CYCLE_FAILED;
      }
      
      // Double-check credentials are still available in flash before connecting
      String verifySSID = NVSConfig::getWiFiSSID();
      String verifyPassword = NVSConfig::getWiFiPassword();
      if (verifySSID.length() == 0 || verifyPassword.length() == 0) {
        Serial.println("ERROR: WiFi credentials not found in flash before connection attempt");
        setStatusLED(0, 0, 0);
        turnOffBatteryLED();
        return CYCLE_FAILED;
      }
      
      Serial.println("Connecting to WiFi for model server...");
      bool wifiConnected = CustomWiFi::connectWiFi();
      
      if (!wifiConnected) {
        Serial.println("ERROR: WiFi connection failed for model server");
        setStatusLED(0, 0, 0);
        turnOffBatteryLED();
        return CYCLE_FAILED;
      }
      
      attemptTimeSyncIfNeeded();
      
      // Read IMU data in batches and send to model server with iterative position updates
      // Calculate safe batch size
      size_t freeHeap = ESP.getFreeHeap();
      size_t maxChunkBytes = (freeHeap * 25) / 100; // Use 25% of free heap
      if (maxChunkBytes > 10000) maxChunkBytes = 10000;
      if (maxChunkBytes < 4000) maxChunkBytes = 4000;
      
      uint32_t batchNumber = 0;
      bool allBatchesSent = true;
      double finalLat = currentLat;
      double finalLon = currentLon;
      
      while (csvStorage->hasDataToRead()) {
        batchNumber++;
        
        // Build batch of IMU entries
        String batch = "";
        batch.reserve(maxChunkBytes);
        uint32_t entriesInBatch = 0;
        size_t batchBytes = 0;
        uint32_t startReadPtr = csvStorage->getReadPtr();
        uint32_t batchEndPtr = startReadPtr;
        
        while (csvStorage->hasDataToRead() && entriesInBatch < 10 && batchBytes < maxChunkBytes) {
          String entry = csvStorage->readNextCSVEntry();
          if (entry.length() == 0) break;
          
          size_t entrySize = entry.length() + (batch.length() > 0 ? 1 : 0);
          if (batchBytes + entrySize > maxChunkBytes && entriesInBatch > 0) break;
          
          if (batch.length() > 0) batch += "\n";
          batch += entry;
          batchBytes += entry.length() + (entriesInBatch > 0 ? 1 : 0);
          entriesInBatch++;
          batchEndPtr = csvStorage->getLastEntryEndPtr();
        }
        
        if (entriesInBatch == 0) break;
        
        Serial.print("\n[Batch ");
        Serial.print(batchNumber);
        Serial.print("] Sending ");
        Serial.print(entriesInBatch);
        Serial.print(" entries with position (");
        Serial.print(finalLat, 7);
        Serial.print(", ");
        Serial.print(finalLon, 7);
        Serial.println(")");
        
        // Send batch to model server with current position
        double deltaLat = 0.0, deltaLon = 0.0;
        bool sent = sendBatchToModelServer(batch, finalLat, finalLon, deltaLat, deltaLon);
        
        if (sent) {
          // Update position: new = old + delta
          finalLat += deltaLat;
          finalLon += deltaLon;
          
          Serial.print("Batch sent successfully. Delta: (");
          Serial.print(deltaLat, 7);
          Serial.print(", ");
          Serial.print(deltaLon, 7);
          Serial.print("), New position: (");
          Serial.print(finalLat, 7);
          Serial.print(", ");
          Serial.print(finalLon, 7);
          Serial.println(")");
          
          // Mark entries as sent
          csvStorage->setReadPtr(batchEndPtr);
        } else {
          Serial.println("Batch transmission failed");
          allBatchesSent = false;
          csvStorage->markAsFailed();
          break;
        }
        
        delay(500); // Delay between batches
        yield();
      }
      
      // Disconnect WiFi after model server transmission
      CustomWiFi::disconnectWiFi();
      
      if (allBatchesSent) {
        // Send final computed position to backend server (BLE or WiFi)
        Serial.println("\nSending final computed position to backend server...");
        
        // Try BLE first, then WiFi
        bool bleWasAlreadyOn = BLEConfig::isEnabled();
        if (!BLEConfig::isEnabled()) {
          BLEConfig::begin();
          delay(500);
        }
        
        bool positionSent = false;
        unsigned long bleStartTime = millis();
        while (millis() - bleStartTime < 5000) {
          BLEConfig::update();
          if (BLEConfig::isConnected()) {
            positionSent = sendPositionToBackend(finalLat, finalLon, currentScenario);
            if (positionSent) {
              Serial.println("Position sent via BLE");
              break;
            }
          }
          delay(100);
        }
        
        if (!positionSent) {
          // Try WiFi
          if (CustomWiFi::connectWiFi()) {
            positionSent = sendPositionToBackend(finalLat, finalLon, currentScenario);
            if (positionSent) {
              Serial.println("Position sent via WiFi");
            }
            CustomWiFi::disconnectWiFi();
          }
        }
        
        if (!bleWasAlreadyOn && BLEConfig::isEnabled()) {
          BLEConfig::stop();
        }
        
        // Update reference position to processed value from last delta
        if (gpsScenarioHandler != nullptr) {
          gpsScenarioHandler->updatePositionAfterTransmission(finalLat, finalLon);
        }
        
        // After transmission, start GPS fix attempt (Scenario 2)
        if (currentScenario == SCENARIO_2_LOW_ACCURACY && gpsScenarioHandler != nullptr) {
          gpsScenarioHandler->startGPSFixAttempt();
          Serial.println("GPS fix attempt started (1 minute)");
        }
        
        setStatusLED(0, 255, 0);
        delay(1000);
        setStatusLED(0, 0, 0);
        turnOffBatteryLED();
        return positionSent ? CYCLE_SUCCESS_WIFI : CYCLE_SUCCESS_STORED;
      } else {
        setStatusLED(0, 0, 0);
        turnOffBatteryLED();
        csvStorage->saveState();
        return CYCLE_SUCCESS_STORED;
      }
    }
    
    // Default: Use existing FlashReader for other scenarios or fallback
    // Create FlashReader instance for modular data transmission
    FlashReader flashReader(csvStorage, transmissionHandler);
    
    // ========== STEP 1: BLE TRANSMISSION ATTEMPT ==========
    Serial.println("\n========== STEP 1: BLE TRANSMISSION ==========");
    
    // Check heap before BLE initialization
    size_t freeHeap = ESP.getFreeHeap();
    size_t maxAlloc = ESP.getMaxAllocHeap();
    Serial.print("Heap status before BLE: Free: ");
    Serial.print(freeHeap);
    Serial.print(" bytes, Largest block: ");
    Serial.print(maxAlloc);
    Serial.println(" bytes");
    
    // BLE needs at least 20KB contiguous memory
    if (maxAlloc < 20000) {
      Serial.print("WARNING: Largest free block (");
      Serial.print(maxAlloc);
      Serial.println(" bytes) may be too small for BLE initialization");
      Serial.println("Heap may be fragmented - BLE initialization might fail");
    }
    
    bool bleWasAlreadyOn = BLEConfig::isEnabled();
    
    if (!BLEConfig::isEnabled()) {
      Serial.println("Starting BLE...");
      
      // Try to free memory before BLE start
      yield(); // Allow garbage collection
      delay(100); // Give time for cleanup
      
      BLEConfig::begin();
      delay(500);
      
      // Check if BLE initialized successfully
      if (!BLEConfig::isEnabled()) {
        Serial.println("WARNING: BLE initialization may have failed - heap may be too fragmented");
      }
    }
    
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
      bool allSent = flashReader.sendViaBLE();
      
      if (allSent) {
        Serial.println("\n✓✓✓ SUCCESS: All CSV entries sent via BLE ✓✓✓");
        setStatusLED(0, 255, 0); // Green
        delay(1000);
        
        if (!bleWasAlreadyOn) {
          Serial.println("Turning off BLE...");
          BLEConfig::stop();
        }
        
        setStatusLED(0, 0, 0);
        turnOffBatteryLED();
        Serial.println("========== CYCLE EXECUTION END (BLE) ==========\n");
        return CYCLE_SUCCESS_BLE;
      } else {
        Serial.println("\n✗✗✗ FAILED: Some CSV entries failed to send via BLE ✗✗✗");
        Serial.println("Will attempt WiFi transmission...");
      }
    } else {
      Serial.println("No BLE connection within 5 seconds");
      Serial.println("Will attempt WiFi transmission...");
    }
    
    if (!bleWasAlreadyOn) {
      Serial.println("Turning off BLE...");
      BLEConfig::stop();
    }
    
    // ========== STEP 2: WIFI TRANSMISSION ATTEMPT (Fallback for other scenarios) ==========
    Serial.println("\n========== STEP 2: WIFI TRANSMISSION ==========");
    
    String ssid = NVSConfig::getWiFiSSID();
    String password = NVSConfig::getWiFiPassword();
    
    if (ssid.length() > 0 && password.length() > 0) {
      Serial.println("WiFi credentials found");
      Serial.println("Connecting to WiFi...");
      bool wifiConnected = CustomWiFi::connectWiFi();
      
      if (wifiConnected) {
        Serial.println("WiFi connected!");
        delay(1000);
        
        if (!CustomWiFi::isConnected()) {
          Serial.println("ERROR: WiFi disconnected - reconnecting...");
          wifiConnected = CustomWiFi::connectWiFi();
          if (wifiConnected) delay(500);
        }
        
        attemptTimeSyncIfNeeded();
        
        if (CustomWiFi::isConnected()) {
          bool allSent = flashReader.sendViaWiFi();
          
          Serial.println("Turning off WiFi...");
          CustomWiFi::disconnectWiFi();
          
          if (allSent) {
            Serial.println("\n✓✓✓ SUCCESS: All CSV entries sent via WiFi ✓✓✓");
            setStatusLED(0, 255, 0); // Green
            delay(1000);
            setStatusLED(0, 0, 0);
            turnOffBatteryLED();
            Serial.println("========== CYCLE EXECUTION END (WiFi) ==========\n");
            return CYCLE_SUCCESS_WIFI;
          } else {
            Serial.println("\n✗✗✗ FAILED: Some CSV entries failed to send via WiFi ✗✗✗");
            Serial.println("Data remains in flash for retry in next cycle");
          }
        }
      } else {
        Serial.println("WiFi connection failed");
      }
    } else {
      Serial.println("No WiFi credentials found");
    }
    
    // ========== STEP 3: FLASH STORAGE (FALLBACK) ==========
    Serial.println("\n========== STEP 3: NO CONNECTION - DATA PRESERVED ==========");
    Serial.print("Final state - ReadPtr: 0x");
    Serial.print(csvStorage->getReadPtr(), HEX);
    Serial.print(", WritePtr: 0x");
    Serial.println(csvStorage->getWritePtr(), HEX);
    Serial.println("Data will be retried in next cycle");
    csvStorage->saveState();
    
    setStatusLED(0, 0, 0);
    turnOffBatteryLED();
    Serial.println("========== CYCLE EXECUTION END (No Connection) ==========\n");
    return CYCLE_SUCCESS_STORED;
  }
  
  // LEGACY CODE - COMMENTED OUT (replaced by FlashReader)
  /*
  // Helper: Send CSV entries from unified storage
  // For WiFi: Batches multiple entries together (reduces HTTP requests)
  // For BLE: Sends one entry at a time (BLE has smaller MTU)
  bool sendCSVEntries(UnifiedCSVStorage* csvStorage, TransmissionHandler* txHandler, bool isBLE) {
    if (csvStorage == nullptr || !csvStorage->isInitialized() || txHandler == nullptr) {
      Serial.println("ERROR: Invalid parameters for sendCSVEntries");
      return false;
    }
    
    const char* method = isBLE ? "BLE" : "WiFi";
    Serial.print("\n--- Starting CSV transmission via ");
    Serial.print(method);
    Serial.println(" ---");
    
    // WiFi batching configuration
    const uint32_t WIFI_BATCH_SIZE = 10; // Send 10 entries per batch for WiFi
    const size_t WIFI_MAX_BATCH_SIZE = 50000; // Max 50KB per batch (to avoid memory issues)
    
    uint32_t entriesSent = 0;
    uint32_t entriesFailed = 0;
    uint32_t entryCounter = 0; // Total entries processed (sent + failed)
    
    // Get initial state
    uint32_t initialReadPtr = csvStorage->getReadPtr();
    Serial.print("Initial read pointer: 0x");
    Serial.println(initialReadPtr, HEX);
    
    // Count total entries that will be sent (for progress tracking)
    uint32_t savedReadPtr = csvStorage->getReadPtr();
    uint32_t maxEntries = 0;
    while (csvStorage->hasDataToRead()) {
      String entry = csvStorage->readNextCSVEntry();
      if (entry.length() > 0) {
        maxEntries++;
        csvStorage->markAsSent(); // Advance for counting
      } else {
        break;
      }
    }
    csvStorage->setReadPtr(savedReadPtr); // Restore position
    
    Serial.print("Maximum entries to send: ");
    Serial.println(maxEntries);
    if (!isBLE) {
      Serial.print("WiFi batching: ");
      Serial.print(WIFI_BATCH_SIZE);
      Serial.print(" entries per batch (max ");
      Serial.print(WIFI_MAX_BATCH_SIZE / 1024);
      Serial.println(" KB per batch)");
    }
    Serial.print("Starting transmission of ");
    Serial.print(maxEntries);
    Serial.println(" entries...");
    Serial.println();
    
    // WiFi: Batch multiple entries together
    if (!isBLE) {
      while (csvStorage->hasDataToRead()) {
        // Build batch of entries
        // First, estimate batch size and pre-allocate String capacity to avoid fragmentation
        uint32_t entriesInBatch = 0;
        size_t estimatedBatchSize = 0;
        uint32_t batchStartEntry = entryCounter + 1;
        
        // First pass: estimate size by reading entries (without building String yet)
        uint32_t savedReadPtr = csvStorage->getReadPtr();
        uint32_t tempEntryCounter = entryCounter;
        while (csvStorage->hasDataToRead() && entriesInBatch < WIFI_BATCH_SIZE) {
          String tempEntry = csvStorage->readNextCSVEntry();
          if (tempEntry.length() == 0) break;
          
          if (estimatedBatchSize + tempEntry.length() + 1 > WIFI_MAX_BATCH_SIZE && entriesInBatch > 0) {
            csvStorage->setReadPtr(savedReadPtr); // Restore for actual read
            break;
          }
          
          estimatedBatchSize += tempEntry.length() + (entriesInBatch > 0 ? 1 : 0); // +1 for newline
          entriesInBatch++;
          tempEntryCounter++;
        }
        csvStorage->setReadPtr(savedReadPtr); // Restore to start
        
        if (entriesInBatch == 0) {
          break; // No more data
        }
        
        // Pre-allocate String with estimated capacity to avoid fragmentation
        String batch = "";
        batch.reserve(estimatedBatchSize + 100); // Add 100 bytes buffer for safety
        
        // Second pass: actually build the batch
        size_t batchSize = 0;
        entryCounter = batchStartEntry - 1; // Reset counter
        while (csvStorage->hasDataToRead() && entriesInBatch > 0) {
          entryCounter++;
          
          // Read next CSV entry (does NOT advance readPtr yet)
          String entry = csvStorage->readNextCSVEntry();
          
          if (entry.length() == 0) {
            Serial.println("No more entries to read");
            break; // No more data
          }
          
          // Add entry to batch (separate with newline for multiple entries)
          if (batch.length() > 0) {
            batch += "\n";
            batchSize += 1;
          }
          batch += entry;
          batchSize += entry.length();
          entriesInBatch--;
        }
        
        if (entriesInBatch == 0) {
          break; // No more data
        }
        
        // Debug: Show batch info
        Serial.print("\n[Batch: Entries ");
        Serial.print(batchStartEntry);
        Serial.print("-");
        Serial.print(batchStartEntry + entriesInBatch - 1);
        Serial.print("] Total length: ");
        Serial.print(batchSize);
        Serial.print(" bytes, Entries in batch: ");
        Serial.print(entriesInBatch);
        Serial.print(", ReadPtr: 0x");
        Serial.print(csvStorage->getReadPtr(), HEX);
        Serial.println();
        
        // Verify WiFi connection before sending batch
        if (!CustomWiFi::isConnected()) {
          Serial.println("ERROR: WiFi disconnected before batch transmission");
          csvStorage->markAsFailed();
          entriesFailed += entriesInBatch;
          Serial.println("  Stopping transmission - WiFi connection lost");
          break;
        }
        
        // Verify batch before sending
        if (batch.length() == 0) {
          Serial.println("ERROR: Batch is empty (0 bytes)! Cannot send.");
          csvStorage->markAsFailed();
          entriesFailed += entriesInBatch;
          Serial.println("  Stopping transmission - empty batch");
          break;
        }
        
        if (batch.length() != batchSize) {
          Serial.print("WARNING: Batch size mismatch! Expected: ");
          Serial.print(batchSize);
          Serial.print(", Actual: ");
          Serial.println(batch.length());
          // Continue anyway - use actual length
        }
        
        // Verify batch is valid before transmission
        if (batch.length() == 0) {
          Serial.println("ERROR: Batch is empty! Cannot send.");
          csvStorage->markAsFailed();
          entriesFailed += entriesInBatch;
          break;
        }
        
        // Attempt transmission of batch
        Serial.print("  Sending batch via WiFi (");
        Serial.print(entriesInBatch);
        Serial.print(" entries, ");
        Serial.print(batch.length());
        Serial.print(" bytes)... ");
        Serial.print("(Free heap: ");
        Serial.print(ESP.getFreeHeap());
        Serial.println(" bytes)");
        
        // Pass batch - String is passed by const reference to avoid copying large batches
        // But verify it's not empty right before passing
        if (batch.length() == 0) {
          Serial.println("ERROR: Batch became empty right before transmission!");
          csvStorage->markAsFailed();
          entriesFailed += entriesInBatch;
          break;
        }
        
        bool success = txHandler->handleDataTransmission(batch);
        
        // Verify WiFi connection after sending
        if (success && !CustomWiFi::isConnected()) {
          Serial.println("WARNING: WiFi disconnected after batch transmission (but transmission reported success)");
        }
        
        if (success) {
          // SUCCESS: Mark all entries in batch as sent
          for (uint32_t i = 0; i < entriesInBatch; i++) {
            csvStorage->markAsSent();
            entriesSent++;
          }
          
          Serial.print("SUCCESS");
          Serial.print(" (Total sent: ");
          Serial.print(entriesSent);
          Serial.print("/");
          Serial.print(maxEntries);
          Serial.println(")");
          
          // Show progress every batch
          Serial.print("  Progress: ");
          Serial.print(entriesSent);
          Serial.print(" entries sent successfully");
          Serial.print(", ReadPtr now: 0x");
          Serial.println(csvStorage->getReadPtr(), HEX);
        } else {
          // FAILURE: Don't advance readPtr (entries remain for retry)
          csvStorage->markAsFailed();
          entriesFailed += entriesInBatch;
          
          Serial.print("FAILED");
          Serial.print(" (Batch ");
          Serial.print(batchStartEntry);
          Serial.print("-");
          Serial.print(batchStartEntry + entriesInBatch - 1);
          Serial.print(" failed, Total failed: ");
          Serial.print(entriesFailed);
          Serial.println(")");
          
          // Stop on first failure (data remains for retry)
          Serial.println("  Stopping transmission - batch will be retried in next cycle");
          break;
        }
        
        // Small delay between batches to avoid overwhelming the server
        if (csvStorage->hasDataToRead()) {
          delay(100);
        }
      }
    } else {
      // BLE: Send one entry at a time (BLE has smaller MTU)
      while (csvStorage->hasDataToRead()) {
        entryCounter++;
        
        // Read next CSV entry (does NOT advance readPtr yet)
        String entry = csvStorage->readNextCSVEntry();
        
        if (entry.length() == 0) {
          Serial.println("No more entries to read");
          break; // No more data
        }
        
        // Debug: Show entry info
        Serial.print("\n[Entry ");
        Serial.print(entryCounter);
        Serial.print("] Length: ");
        Serial.print(entry.length());
        Serial.print(" bytes, ReadPtr: 0x");
        Serial.print(csvStorage->getReadPtr(), HEX);
        
        // Count objects in this entry (for debugging)
        int objectCount = 0;
        for (int i = 0; i < entry.length(); i++) {
          if (entry.charAt(i) == ',') objectCount++;
        }
        objectCount++; // Last object doesn't have trailing comma
        Serial.print(", Objects: ");
        Serial.print(objectCount);
        Serial.println();
        
        // Attempt transmission
        Serial.print("  Sending via BLE... ");
        bool success = txHandler->handleDataTransmission(entry);
        
        if (success) {
          // SUCCESS: Mark as sent (advance readPtr past this entry)
          csvStorage->markAsSent();
          entriesSent++;
          
          Serial.print("SUCCESS");
          Serial.print(" (Total sent: ");
          Serial.print(entriesSent);
          Serial.println(")");
          
          // Show progress every 10 entries
          if (entriesSent % 10 == 0) {
            Serial.print("  Progress: ");
            Serial.print(entriesSent);
            Serial.print(" entries sent successfully");
            Serial.print(", ReadPtr now: 0x");
            Serial.println(csvStorage->getReadPtr(), HEX);
          }
        } else {
          // FAILURE: Don't advance readPtr (entry remains for retry)
          csvStorage->markAsFailed();
          entriesFailed++;
          
          Serial.print("FAILED");
          Serial.print(" (Entry ");
          Serial.print(entryCounter);
          Serial.print(" failed, Total failed: ");
          Serial.print(entriesFailed);
          Serial.println(")");
          
          // Stop on first failure (data remains for retry)
          Serial.println("  Stopping transmission - entry will be retried in next cycle");
          break;
        }
      }
    }
    
    // Save state
    csvStorage->saveState();
    
    // Final summary
    Serial.print("\n--- Transmission Summary (");
    Serial.print(method);
    Serial.print(") ---\n");
    Serial.print("  Total entries processed: ");
    Serial.println(entryCounter);
    Serial.print("  Entries sent successfully: ");
    Serial.println(entriesSent);
    Serial.print("  Entries failed: ");
    Serial.println(entriesFailed);
    Serial.print("  Final read pointer: 0x");
    Serial.println(csvStorage->getReadPtr(), HEX);
    Serial.print("  Write pointer: 0x");
    Serial.println(csvStorage->getWritePtr(), HEX);
    
    if (entriesFailed == 0 && entriesSent > 0) {
      Serial.println("  Status: ALL ENTRIES SENT SUCCESSFULLY");
    } else if (entriesFailed > 0) {
      Serial.print("  Status: PARTIAL - ");
      Serial.print(entriesFailed);
      Serial.println(" entries failed (will retry)");
    } else {
      Serial.println("  Status: NO ENTRIES TO SEND");
    }
    Serial.println();
    
    return (entriesFailed == 0);
  }
  */
  
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

