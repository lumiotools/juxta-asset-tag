#ifndef MODEL_SERVER_TRANSMISSION_H
#define MODEL_SERVER_TRANSMISSION_H

#include <HTTPClient.h>
#include <WiFi.h>
#include "unified_csv_storage.h"
#include "customwifi.h"
#include "nvs_config.h"
#include "time_sync.h"

// Server URL and configuration
const char* MODEL_SERVER_URL = "https://908ebcc61c17.ngrok-free.app/api/model"; // Model server endpoint
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
  static const uint32_t MAX_BATCH_SIZE = 1200;        // Maximum number of CSV entries per batch
  static const uint32_t MAX_BATCH_BYTES = 51200;     // Maximum 8KB per batch (to fit in memory)
  
  // Response structure for delta position from model server
  struct DeltaPosition {
    double deltaLat;
    double deltaLon;
    bool valid;
  };

public:
  ModelServerTransmissionHandler() : csvStorage(nullptr), initialized(false), 
                                      currentLat(0.0), currentLon(0.0), currentHdop(-1.0),
                                      positionInitialized(false) {}
  
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
    Serial.println("Batch: " + String(MAX_BATCH_SIZE) + " entries or " + String(MAX_BATCH_BYTES) + " bytes max");
    Serial.println("=======================================================");
    
    return initialized;
  }
  
  // Check if handler is initialized
  bool isInitialized() const {
    return initialized;
  }
  
  // Read batch of CSV data from flash storage
  // Returns: CSV string with multiple entries (obj1,obj2,...\nobj1,obj2,...\n)
  String readBatchFromFlash(uint32_t& entriesRead) {
    if (!initialized || csvStorage == nullptr) {
      Serial.println("ModelServerTransmissionHandler: Not initialized");
      entriesRead = 0;
      return String("");
    }
    
    String batchData = "";
    entriesRead = 0;
    uint32_t bytesAccumulated = 0;
    
    // Read multiple CSV entries until batch size limit
    while (entriesRead < MAX_BATCH_SIZE && bytesAccumulated < MAX_BATCH_BYTES) {
      // Check if data available
      if (!csvStorage->hasDataToRead()) {
        break; // No more data
      }
      
      // Read next entry
      Serial.print("ModelServerTransmissionHandler: Read pointer: 0x");
      Serial.print(csvStorage->getReadPtr(), HEX);
      Serial.print(", Write pointer: 0x");
      Serial.println(csvStorage->getWritePtr(), HEX);
      
      String entry = csvStorage->readNextCSVEntry();
      if (entry.length() == 0) {
        break; // No more entries or read error
      }
      
      Serial.print("ModelServerTransmissionHandler: After read - Read pointer: 0x");
      Serial.print(csvStorage->getReadPtr(), HEX);
      Serial.print(", Write pointer: 0x");
      Serial.println(csvStorage->getWritePtr(), HEX);
      
      // Add to batch (with newline separator)
      if (batchData.length() > 0) {
        batchData += "\n";
      }
      batchData += entry;
      
      entriesRead++;
      bytesAccumulated += entry.length() + 1; // +1 for newline
      
      // Yield to prevent watchdog
      if (entriesRead % 10 == 0) {
        yield();
      }
    }
    
    Serial.print("ModelServerTransmissionHandler: Read ");
    Serial.print(entriesRead);
    Serial.print(" entries (");
    Serial.print(bytesAccumulated);
    Serial.println(" bytes)");
    
    return batchData;
  }
  
  // Send batch to model server with GPS coordinates
  // Format: (latitude,longitude,hdop),imuObj1,imuObj2,...
  // Returns: DeltaPosition with delta_lat, delta_lon from server response
  DeltaPosition sendBatchToModelServer(double latitude, double longitude, double hdop, const String& imuBatchData) {
    DeltaPosition result = {0.0, 0.0, false};
    
    if (!initialized) {
      Serial.println("ModelServerTransmissionHandler: Not initialized");
      return result;
    }
    
    // Check WiFi connection (MODEL SERVER REQUIRES WIFI ONLY)
    if (!CustomWiFi::isConnected()) {
      Serial.println("ModelServerTransmissionHandler: WiFi not connected - cannot send to model server");
      return result;
    }
    
    // Construct payload: GPS coordinates + IMU data
    // Format: (lat,lon,hdop),obj1,obj2,...
    String payload = "(" + String(latitude, 7) + "," + String(longitude, 7) + "," + String(hdop, 2) + "),";
    payload += imuBatchData;
    
    Serial.println("ModelServerTransmissionHandler: Sending to model server...");
    Serial.print("  GPS: (");
    Serial.print(latitude, 7);
    Serial.print(", ");
    Serial.print(longitude, 7);
    Serial.print("), HDOP: ");
    Serial.println(hdop, 2);
    Serial.print("  Payload size: ");
    Serial.print(payload.length());
    Serial.println(" bytes");
    
    // Send via HTTP POST
    HTTPClient http;
    
    // Use direct begin() like working example - simpler approach
    http.begin(MODEL_SERVER_URL);
    
    // Set timeout AFTER begin() (recommended approach)
    http.setTimeout(MODEL_TRANSMISSION_TIMEOUT);
    
    // Only set Content-Type header (like working example)
    http.addHeader("Content-Type", "text/csv");
    
    Serial.println("ModelServerTransmissionHandler: Sending POST request...");
    int httpResponseCode = http.POST(payload);
    
    Serial.print("ModelServerTransmissionHandler: Response code: ");
    Serial.println(httpResponseCode);
    
    if (httpResponseCode == 200) {
      // Parse response: expected format "delta_lat,delta_lon"
      String response = http.getString();
      Serial.print("ModelServerTransmissionHandler: Response: ");
      Serial.println(response);
      
      int commaPos = response.indexOf(',');
      if (commaPos > 0) {
        result.deltaLat = response.substring(0, commaPos).toDouble();
        result.deltaLon = response.substring(commaPos + 1).toDouble();
        result.valid = true;
        
        Serial.print("ModelServerTransmissionHandler: Delta position: (");
        Serial.print(result.deltaLat, 7);
        Serial.print(", ");
        Serial.print(result.deltaLon, 7);
        Serial.println(")");
      } else {
        Serial.println("ModelServerTransmissionHandler: Failed to parse delta position from response");
      }
    } else {
      Serial.print("ModelServerTransmissionHandler: HTTP error - code ");
      Serial.println(httpResponseCode);
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
    } else {
      Serial.println("ModelServerTransmissionHandler: WiFi connected - waiting for TCPIP stack...");
      delay(500); // Give TCPIP stack time to initialize
    }
    
    Serial.println("ModelServerTransmissionHandler: WiFi ready - proceeding with transmission");
    
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
    
    uint32_t batchesSent = 0;
    uint32_t totalEntriesSent = 0;
    
    // Process all available batches
    while (csvStorage->hasDataToRead()) {
      yield(); // Prevent watchdog on long transmission cycles
      
      // Read batch from flash
      uint32_t entriesRead = 0;
      String batchData = readBatchFromFlash(entriesRead);
      
      if (batchData.length() == 0 || entriesRead == 0) {
        Serial.println("ModelServerTransmissionHandler: No data read from flash");
        break;
      }
      
      // Send batch to model server with current position
      DeltaPosition deltaPos = sendBatchToModelServer(currentLat, currentLon, currentHdop, batchData);
      
      if (deltaPos.valid) {
        // Success - mark entries as sent in flash
        csvStorage->markAsSent();
        
        batchesSent++;
        totalEntriesSent += entriesRead;
        
        Serial.print("ModelServerTransmissionHandler: Batch sent successfully (");
        Serial.print(entriesRead);
        Serial.print(" entries, total: ");
        Serial.print(totalEntriesSent);
        Serial.println(")");
        
        // Update current position with delta from model server
        currentLat += deltaPos.deltaLat;
        currentLon += deltaPos.deltaLon;
        currentHdop = -1.0; // Mark as calculated position (not from real GPS)
        
        Serial.print("ModelServerTransmissionHandler: Updated position: (");
        Serial.print(currentLat, 7);
        Serial.print(", ");
        Serial.print(currentLon, 7);
        Serial.print("), Delta: (");
        Serial.print(deltaPos.deltaLat, 7);
        Serial.print(", ");
        Serial.print(deltaPos.deltaLon, 7);
        Serial.println(")");
        
        // Save updated position to NVS after every batch
        NVSConfig::saveLastKnownPosition(currentLat, currentLon, currentHdop);
        Serial.println("ModelServerTransmissionHandler: Saved updated position to NVS");
        
        yield(); // Prevent watchdog
      } else {
        // Transmission failed - mark as failed (will retry on next cycle)
        csvStorage->markAsFailed();
        
        Serial.println("ModelServerTransmissionHandler: Batch transmission failed - stopping");
        break;
      }
    }
    
    // Final confirmation save (position already saved after each batch)
    if (batchesSent > 0) {
      NVSConfig::saveLastKnownPosition(currentLat, currentLon, currentHdop);
      Serial.println("ModelServerTransmissionHandler: Final position confirmed in NVS");
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
    
    // ========== WIFI AUTO DISCONNECT LOGIC ==========
    // Turn off WiFi if it wasn't connected before transmission
    if (!wifiWasConnected) {
      Serial.println("ModelServerTransmissionHandler: Turning WiFi off (was not connected before)");
      CustomWiFi::disconnectWiFi();
    } else {
      Serial.println("ModelServerTransmissionHandler: WiFi remains on (was connected before)");
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
