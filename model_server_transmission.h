#ifndef MODEL_SERVER_TRANSMISSION_H
#define MODEL_SERVER_TRANSMISSION_H

#include <HTTPClient.h>
#include <WiFi.h>
#include "unified_csv_storage.h"
#include "customwifi.h"
#include "nvs_config.h"
#include "time_sync.h"
#include "device_id.h"

// Server URL and configuration
const char* MODEL_SERVER_URL = "https://assetserver.usejuxta.org/infer"; // Model server endpoint
const int MODEL_TRANSMISSION_TIMEOUT = 60000; // 60 seconds timeout

// ============================================================================
// MODEL SERVER TRANSMISSION HANDLER
// ============================================================================
// Purpose: Send IMU data with GPS coordinates to ML model server
// Protocol: WiFi ONLY
// Data Flow: Read from external flash -> Send to model server -> Receive delta position
// Parameters: latitude, longitude, hdop
// ============================================================================

class ModelServerTransmissionHandler {
private:
  UnifiedCSVStorage* csvStorage;
  bool initialized;
  
  // Current position tracking (updated from GPS or calculated from delta)
  double currentLat;
  double currentLon;
  double currentHdop;
  bool positionInitialized;
  
  // Batch reading configuration
  static const uint32_t CSV_HEADER_SIZE = 40;          // 16 bytes (DeviceID) + 3 doubles (Lat, Lon, Hdop)
  static const uint32_t RECORD_SIZE = sizeof(TimestampedIMUReading); // 32 bytes
  static const uint32_t MAX_BATCH_READINGS = 1984;     // 64KB (1984 * 32 = 63,488 bytes)
  
  // Single Zero-Copy Buffer (Holds Header + Data)
  uint8_t* zeroCopyBuffer;
  
  // Response structure for delta position from model server
  struct DeltaPosition {
    double deltaLat;
    double deltaLon;
    bool valid;
  };

public:
  ModelServerTransmissionHandler() : csvStorage(nullptr), initialized(false), 
                                      currentLat(0.0), currentLon(0.0), currentHdop(-1.0),
                                      positionInitialized(false), zeroCopyBuffer(nullptr) {}
  
  // Initialize with CSV storage handler
  bool begin(UnifiedCSVStorage* storage) {
    if (storage == nullptr || !storage->isInitialized()) {
      Serial.println("ModelServerTransmissionHandler: Invalid CSV storage");
      return false;
    }
    
    csvStorage = storage;
    
    // Load last known GPS position from NVS
    if (NVSConfig::getLastKnownPosition(currentLat, currentLon, currentHdop)) {
      positionInitialized = true;
      Serial.print("ModelServerTransmissionHandler: Loaded last known position: (");
      Serial.print(currentLat, 7);
      Serial.print(", ");
      Serial.print(currentLon, 7);
      Serial.print("), HDOP: ");
      Serial.println(currentHdop, 2);
    } else {
      Serial.println("ModelServerTransmissionHandler: WARNING - No last known position in NVS");
      positionInitialized = false;
    }
    
    initialized = true;
    
    Serial.println("========== Model Server Transmission Handler ==========");
    Serial.println("Protocol: WiFi ONLY");
    Serial.println("Server: " + String(MODEL_SERVER_URL));
    Serial.println("Batch: " + String(MAX_BATCH_READINGS) + " entries max");
    Serial.println("=======================================================");
    
    return initialized;
  }
  
  // Check if handler is initialized
  bool isInitialized() const {
    return initialized;
  }
  
  // Read batch of Binary data from flash storage
  // Returns: number of readings read
  size_t readBatchFromFlash() {
    if (!initialized || csvStorage == nullptr || zeroCopyBuffer == nullptr) {
      Serial.println("ModelServerTransmissionHandler: Buffer or Storage not initialized");
      return 0;
    }
    
    // ZERO COPY OPTIMIZATION:
    // Read directly into buffer at offset CSV_HEADER_SIZE (Leave space for Header)
    // Cast the byte pointer + offset to the struct pointer
    TimestampedIMUReading* writeLocation = (TimestampedIMUReading*)(zeroCopyBuffer + CSV_HEADER_SIZE);

    size_t count = csvStorage->readBatch(writeLocation, MAX_BATCH_READINGS);
    
    if (count > 0) {
        Serial.print("ModelServerTransmissionHandler: Buffered ");
        Serial.print(count);
        Serial.print(" readings (");
        Serial.print(count * RECORD_SIZE);
        Serial.println(" bytes)");
    }
    
    return count;
  }
  
  // Send BINARY batch to model server with GPS coordinates
  // Payload Format: [DEVICE_ID(16)][LAT(8)][LON(8)][HDOP(8)][IMU_DATA(N*32)]
  // Returns: DeltaPosition with delta_lat, delta_lon from server response
  DeltaPosition sendBatchToModelServer(double latitude, double longitude, double hdop, size_t readingCount) {
    DeltaPosition result = {0.0, 0.0, false};
    
    if (!initialized || readingCount == 0 || zeroCopyBuffer == nullptr) {
      return result;
    }
    
    // Check WiFi connection (MODEL SERVER REQUIRES WIFI ONLY)
    if (!CustomWiFi::isConnected()) {
      Serial.println("ModelServerTransmissionHandler: WiFi not connected - cannot send to model server");
      return result;
    }
    
    // Additional WiFi stability check - ensure we have an IP address
    if (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
      Serial.println("ModelServerTransmissionHandler: WiFi connected but no IP address");
      return result;
    }
    
    delay(10);
    
    // ZERO COPY PAYLOAD CONSTRUCTION
    // 1. Write Header to execution buffer (first 40 bytes)
    
    // 1a. Device ID (First 16 bytes)
    char deviceId[20]; // Buffer for getting ID
    DeviceID::getDeviceId(deviceId, sizeof(deviceId));
    
    // Clear first 16 bytes
    memset(zeroCopyBuffer, 0, 16);
    // Copy ID (up to 16 bytes or null terminator)
    strncpy((char*)zeroCopyBuffer, deviceId, 16);
    
    // 1b. GPS Data (Offsets 16, 24, 32)
    memcpy(zeroCopyBuffer + 16, &latitude, sizeof(double));
    memcpy(zeroCopyBuffer + 24, &longitude, sizeof(double));
    memcpy(zeroCopyBuffer + 32, &hdop, sizeof(double));
    
    // 2. Data is already there (from readBatchFromFlash) at offset 40
    
    // Calculate total size
    size_t dataSize = readingCount * RECORD_SIZE;
    size_t totalSize = CSV_HEADER_SIZE + dataSize;
    
    Serial.println("ModelServerTransmissionHandler: Sending BINARY to model server...");
    Serial.print("  Device: ");
    Serial.println(deviceId);
    Serial.print("  GPS: (");
    Serial.print(latitude, 7);
    Serial.print(", ");
    Serial.print(longitude, 7);
    Serial.print(")");
    Serial.print("  Size: ");
    Serial.print(totalSize);
    Serial.println(" bytes");
    
    // Send via HTTP POST
    HTTPClient http;
    int httpResponseCode = -1;

    try {
        if (http.begin(MODEL_SERVER_URL)) {
            http.setTimeout(MODEL_TRANSMISSION_TIMEOUT);
            http.addHeader("Content-Type", "application/octet-stream");
            // Send the zeroCopyBuffer directly
            httpResponseCode = http.POST(zeroCopyBuffer, totalSize);
        } else {
            Serial.println("ModelServerTransmissionHandler: server connection failed");
        }
    } catch (...) {
        Serial.println("ModelServerTransmissionHandler: EXCEPTION during HTTP transmission");
        httpResponseCode = -1;
    }
    
    // NO FREEING HERE - Buffer is reused for next batch
    
    Serial.print("ModelServerTransmissionHandler: Response code: ");
    Serial.println(httpResponseCode);
    
    if (httpResponseCode == 200) {
      // Parse response: expected format "delta_lat,delta_lon"
      String response = http.getString();
      Serial.print("ModelServerTransmissionHandler: Response: ");
      Serial.println(response);
      
      int commaPos = response.indexOf(',');
      if (commaPos > 0) {
        double dLat = response.substring(0, commaPos).toDouble();
        double dLon = response.substring(commaPos + 1).toDouble();
        
        // CHECK FOR NaN: If server sends "NaN,NaN" or invalid float, toDouble() might process it blindly
        // or we need to check isnan() explicitly.
        // Also check if response actually contains "NaN" string to be safe.
        if (isnan(dLat) || isnan(dLon) || response.indexOf("NaN") >= 0 || response.indexOf("nan") >= 0) {
            Serial.print("ModelServerTransmissionHandler: NaN response received, treating as 0.0, 0.0: ");
            Serial.println(response);
            result.deltaLat = 0.0;
            result.deltaLon = 0.0;
            result.valid = true;
        } else {
            result.deltaLat = dLat;
            result.deltaLon = dLon;
            result.valid = true;
        }
      }
    }
    
    http.end();
    return result;
  }
  
  // Main transmission handler for model server
  // Reads batches from flash, sends to model server, processes response
  // Initial GPS coordinates can be provided, or uses last known position
  // WiFi Auto Control: Turns on WiFi if needed, sends data, then turns off if it wasn't on before
  // Returns: Number of batches successfully transmitted
  uint32_t handleModelServerTransmission(double initialLat = 0.0, double initialLon = 0.0, double initialHdop = -1.0) {
    if (!initialized) {
      Serial.println("ModelServerTransmissionHandler: Not initialized");
      return 0;
    }
    
    // ========== WIFI AUTO CONNECT LOGIC ==========
    bool wifiWasConnected = CustomWiFi::isConnected();
    bool wifiConnectionAchieved = false;
    
    // Turn on WiFi if not already connected
    if (!wifiWasConnected) {
      Serial.println("ModelServerTransmissionHandler: WiFi disconnected - connecting...");
      CustomWiFi::connectWiFi();
      
      // Wait for WiFi connection (with timeout)
      unsigned long long wifiStartTime = TimeSync::getCurrentTimeMillis();
      const unsigned long long WIFI_CONNECTION_TIMEOUT = 10000; // 10 seconds
      
      while (!CustomWiFi::isConnected() && 
             (TimeSync::getCurrentTimeMillis() - wifiStartTime) < WIFI_CONNECTION_TIMEOUT) {
        delay(100);
      }
    }
    
    wifiConnectionAchieved = CustomWiFi::isConnected();
    
    if (!wifiConnectionAchieved) {
      Serial.println("ModelServerTransmissionHandler: WiFi connection failed - cannot transmit");
      return 0;
    }
    
    Serial.println("ModelServerTransmissionHandler: WiFi connected - proceeding with transmission");
    
    // Use provided GPS coordinates if valid, otherwise use stored position
    if (initialLat != 0.0 && initialLon != 0.0 && initialHdop > 0.0) {
      // Real GPS reading provided - update current position
      currentLat = initialLat;
      currentLon = initialLon;
      currentHdop = initialHdop;
      positionInitialized = true;
      
      // Save to NVS as last known position
      NVSConfig::saveLastKnownPosition(currentLat, currentLon, currentHdop);
      
      Serial.println("ModelServerTransmissionHandler: Using provided GPS coordinates");
    } else if (!positionInitialized) {
      Serial.println("ModelServerTransmissionHandler: ERROR - No position available (no GPS and no stored position)");
      return 0;
    } else {
      Serial.println("ModelServerTransmissionHandler: Using stored position (calculated)");
    }
    
    Serial.println("========== Model Server Transmission Cycle ==========");
    Serial.print("Starting GPS: (");
    Serial.print(currentLat, 7);
    Serial.print(", ");
    Serial.print(currentLon, 7);
    Serial.print("), HDOP: ");
    Serial.println(currentHdop, 2);
    
    Serial.print("ModelServerTransmissionHandler: Free RAM before buffer alloc: ");
    Serial.println(ESP.getFreeHeap());

    // ALLOCATE BUFFER DYNAMICALLY (Zero Copy Sized)
    if (zeroCopyBuffer == nullptr) {
        // Size = Header (24) + Max Data (4000 * 32)
        size_t allocSize = CSV_HEADER_SIZE + (MAX_BATCH_READINGS * RECORD_SIZE);
        zeroCopyBuffer = (uint8_t*) malloc(allocSize);
        if (zeroCopyBuffer == nullptr) {
            Serial.println("ModelServerTransmissionHandler: Failed to allocate zero-copy buffer!");
            return 0;
        }
    }
    
    Serial.print("ModelServerTransmissionHandler: Free RAM after buffer alloc: ");
    Serial.println(ESP.getFreeHeap());

    uint32_t batchesSent = 0;
    uint32_t totalEntriesSent = 0;
    
    // Process all available batches
    while (csvStorage->hasDataToRead()) {
      yield(); // Prevent watchdog on long transmission cycles
      
      // Read batch from flash (Now returns count of items, not a string)
      size_t itemsRead = readBatchFromFlash();
      
      if (itemsRead == 0) {
        Serial.println("ModelServerTransmissionHandler: No data read from flash");
        break;
      }
      
      // Send batch to model server with current position
      DeltaPosition deltaPos = sendBatchToModelServer(currentLat, currentLon, currentHdop, itemsRead);
      
      if (deltaPos.valid) {
        // Success - mark entries as sent in flash
        // OPTIMIZATION: Do NOT save to NVS yet (false). We save once at the end.
        csvStorage->markAsSent(itemsRead, false);
        
        batchesSent++;
        totalEntriesSent += itemsRead;
        
        Serial.print("ModelServerTransmissionHandler: Batch sent successfully (");
        Serial.print(itemsRead);
        Serial.print(" entries, total: ");
        Serial.print(totalEntriesSent);
        Serial.println(")");
        
        // Update current position with delta from model server
        currentLat += deltaPos.deltaLat;
        currentLon += deltaPos.deltaLon;
        currentHdop = -1.0; // Mark as calculated position (not from real GPS)
        
        // 1. Handle latitude crossing the poles (reflect + flip longitude)
        while (currentLat > 90.0 || currentLat < -90.0) {
          if (currentLat > 90.0) {
            currentLat = 180.0 - currentLat;
            currentLon += 180.0;
          } else if (currentLat < -90.0) {
            currentLat = -180.0 - currentLat;
            currentLon += 180.0;
          }
        }
        
        // 2. Wrap Longitude to -180 to 180 (Efficiently handles large values)
        // Logic: (((Lon + 180) % 360) - 180)
        currentLon = fmod(currentLon + 180.0, 360.0);
        if (currentLon < 0) currentLon += 360.0;
        currentLon -= 180.0;

        Serial.print("ModelServerTransmissionHandler: Updated position: (");
        Serial.print(currentLat, 7);
        Serial.print(", ");
        Serial.print(currentLon, 7);
        Serial.println(")");
        
        // OPTIMIZATION: Do not save GPS to NVS after every batch. 
        // We will save valid position at the end of the cycle.
        
        yield(); // Prevent watchdog
      } else {
        // Transmission failed - do NOT mark as sent
        // Pointer remains at current position for retry on next cycle
        Serial.println("ModelServerTransmissionHandler: Batch transmission failed - stopping");
        break;
      }
    }
    
    // Final confirmation save (position already saved after each batch)
    if (batchesSent > 0) {
      // SAVE CHECKPOINT: Save updated GPS position AND Flash pointers
      // This is the single commit point for the entire transmission cycle.
      NVSConfig::saveLastKnownPosition(currentLat, currentLon, currentHdop);
      csvStorage->savePointers(); 
      Serial.println("ModelServerTransmissionHandler: Final position and pointers saved to NVS");
    }
    
    Serial.println("========== Transmission Summary ==========");
    Serial.print("Batches sent: ");
    Serial.println(batchesSent);
    Serial.print("Total entries sent: ");
    Serial.println(totalEntriesSent);
    Serial.print("Final position: (");
    Serial.print(currentLat, 7);
    Serial.print(", ");
    Serial.print(currentLon, 7);
    Serial.println(")");
    Serial.println("==========================================");

    // FREE BUFFER
    if (zeroCopyBuffer != nullptr) {
        free(zeroCopyBuffer);
        zeroCopyBuffer = nullptr;
    }
    
    Serial.print("ModelServerTransmissionHandler: Free RAM after buffer release: ");
    Serial.println(ESP.getFreeHeap());
    
    // ========== WIFI AUTO DISCONNECT LOGIC ==========
    // OPTIMIZATION: Only disconnect if transmission FAILED entirely (batchesSent == 0).
    // If successful, keep WiFi on for Position server transmission to reuse.
    
    if (batchesSent == 0 && !wifiWasConnected) {
      Serial.println("ModelServerTransmissionHandler: Transmission failed - turning WiFi off (restoring state)");
      CustomWiFi::disconnectWiFi();
    } else {
      Serial.println("ModelServerTransmissionHandler: WiFi remains on (success or was already connected)");
    }
    
    return batchesSent;
  }
  
  // Simplified method: Send IMU data to model server
  // This is the main method for sending IMU data with GPS coordinates
  // Parameters: latitude, longitude, hdop from current GPS reading
  // Returns: true if at least one batch was sent successfully
  bool sendData(double latitude, double longitude, double hdop) {
    if (!initialized) {
      Serial.println("ModelServerTransmissionHandler: Not initialized");
      return false;
    }
    
    uint32_t batchesSent = handleModelServerTransmission(latitude, longitude, hdop);
    return (batchesSent > 0);
  }
};

#endif // MODEL_SERVER_TRANSMISSION_H
