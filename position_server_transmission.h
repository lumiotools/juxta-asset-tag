#ifndef POSITION_SERVER_TRANSMISSION_H
#define POSITION_SERVER_TRANSMISSION_H

#include <HTTPClient.h>
#include <WiFi.h>
#include "customwifi.h"
#include "ble_config.h"
#include "nvs_config.h"
#include "device_id.h"
#include "battery_monitor.h"
#include "time_sync.h"

// Server URL and configuration
const char* POSITION_SERVER_URL = "https://tags.juxta.com/api/record/wifi/v2.0.0?source=wifi"; // Position database server endpoint
const int POSITION_TRANSMISSION_TIMEOUT = 60000; // 60 seconds timeout

// FIFO Queue configuration (using blob arrays)
const char* POSITION_QUEUE_HEAD_KEY = "pos_q_head";      // Head pointer (write position)
const char* POSITION_QUEUE_TAIL_KEY = "pos_q_tail";      // Tail pointer (read position)
const char* POSITION_QUEUE_COUNT_KEY = "pos_q_count";    // Number of entries in queue
const char* POSITION_QUEUE_MAX_KEY = "pos_q_max";        // Max queue size
const int POSITION_MAX_ENTRY_SIZE = 200;                 // Max size per entry (bytes)
const int POSITION_BATCH_READ_SIZE = 10;                 // Default batch size for reading
const int POSITION_CHUNK_SIZE = 5;                       // Max entries per transmission (RAM safety)

// ============================================================================
// Position SERVER TRANSMISSION HANDLER
// ============================================================================
// Purpose: Send sensor data to Position database server
// Protocol: BLE (primary) -> WiFi (fallback) -> Internal Flash Storage
// Data Flow: 
//   1. Try BLE (4-sec timeout with auto on/off)
//   2. If BLE fails, try WiFi (10-sec timeout with auto on/off)
//   3. If both fail, save to NVS FIFO queue (blob array storage)
// Format: device_id,battery%,voltage,timestamp,scenario,lat,lon,hdop
// 
// Storage:
//   - Uses 50% of available NVS space
//   - FIFO queue with blob arrays
//   - Failed transmissions preserved for manual retrieval
//   - No automatic retry mechanism
// ============================================================================

class PositionServerTransmissionHandler {
private:
  bool initialized;
  uint32_t maxQueueSize;    // Maximum queue entries (calculated from 50% NVS capacity)
  uint32_t maxCapacityBytes; // 50% of available NVS space
  
  // Response tracking
  struct TransmissionResult {
    bool success;
    String method; // "BLE" or "WiFi"
    int responseCode;
  };
  
  // Get blob key name for queue index
  String getQueueBlobKey(uint32_t index) {
    char key[16];
    snprintf(key, sizeof(key), "pos_b%lu", (unsigned long)index);
    return String(key);
  }
  
  // Internal: Common transmission logic with BLE/WiFi auto on/off control
  // Used by both handlePositionServerTransmission and retryPendingTransmissions
  // Takes pre-formatted payload and attempts transmission via BLE then WiFi
  TransmissionResult sendPayloadWithAutoControl(const String& payload) {
    TransmissionResult result = {false, "None", 0};
    
    // // ========== BLE TRANSMISSION ATTEMPT ==========
    // // Turn on BLE for 4 seconds and wait for connection
    // bool bleWasEnabled = BLEConfig::isEnabled();
    // bool bleConnectionAchieved = false;
    
    // Serial.println("PositionServerTransmissionHandler: Starting BLE transmission attempt...");
    
    // // Turn on BLE if not already enabled
    // if (!bleWasEnabled) {
    //   Serial.println("PositionServerTransmissionHandler: BLE disabled - turning on...");
    //   BLEConfig::begin();
    //   delay(100); // Give BLE time to initialize
    // }
    
    // // Wait up to 4 seconds for BLE connection
    // Serial.println("PositionServerTransmissionHandler: Waiting for BLE connection (4 sec timeout)...");
    // unsigned long long bleStartTime = TimeSync::getCurrentTimeMillis();
    // const unsigned long long BLE_CONNECTION_TIMEOUT = 4000; // 4 seconds
    
    // while (!BLEConfig::isConnected() && 
    //        (TimeSync::getCurrentTimeMillis() - bleStartTime) < BLE_CONNECTION_TIMEOUT) {
    //   BLEConfig::update(); // Process BLE events
    //   delay(100); // Small delay to prevent tight loop
    // }
    
    // bleConnectionAchieved = BLEConfig::isConnected();
    
    // if (bleConnectionAchieved) {
    //   Serial.println("PositionServerTransmissionHandler: BLE connection achieved - sending data...");
    //   delay(6000);
    //   result = sendViaBLE(payload);
      
    //   if (result.success) {
    //     Serial.println("PositionServerTransmissionHandler: BLE transmission successful");
        
    //     // Turn off BLE if it was disabled before
    //     if (!bleWasEnabled) {
    //       Serial.println("PositionServerTransmissionHandler: Turning BLE off");
    //       delay(1000);
    //       BLEConfig::stop();
    //     }
        
    //     return result;
    //   } else {
    //     Serial.println("PositionServerTransmissionHandler: BLE transmission failed");
    //   }
    // } else {
    //   Serial.println("PositionServerTransmissionHandler: BLE connection timeout (4 sec) - no connection");
    // }
    
    // // Turn off BLE after attempt (success or failure)
    // if (!bleWasEnabled) {
    //   Serial.println("PositionServerTransmissionHandler: Turning BLE off");
    //   BLEConfig::stop();
    // }
    
    // ========== WIFI TRANSMISSION ATTEMPT ==========
    Serial.println("PositionServerTransmissionHandler: BLE failed/unavailable - trying WiFi...");
    
    bool wifiWasConnected = CustomWiFi::isConnected();
    bool wifiConnectionAchieved = false;
    
    // Turn on WiFi if not already connected
    if (!wifiWasConnected) {
      Serial.println("PositionServerTransmissionHandler: WiFi disconnected - connecting...");
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
    
    if (wifiConnectionAchieved) {
      Serial.println("PositionServerTransmissionHandler: WiFi connected - sending data...");
      result = sendViaWiFi(payload);
      
      if (result.success) {
        Serial.println("PositionServerTransmissionHandler: WiFi transmission successful");
        
        // Turn off WiFi if it wasn't connected before
        if (!wifiWasConnected) {
          Serial.println("PositionServerTransmissionHandler: Turning WiFi off");
          CustomWiFi::disconnectWiFi();
        }
        
        return result;
      } else {
        Serial.println("PositionServerTransmissionHandler: WiFi transmission failed");
      }
    } else {
      Serial.println("PositionServerTransmissionHandler: WiFi connection failed");
    }
    
    // Turn off WiFi after attempt if it wasn't connected before
    if (!wifiWasConnected) {
      Serial.println("PositionServerTransmissionHandler: Turning WiFi off");
      CustomWiFi::disconnectWiFi();
    }
    
    // Both BLE and WiFi failed
    return result;
  }
  
  // Get queue head (write position)
  uint32_t getQueueHead() {
    return NVSConfig::readU32(POSITION_QUEUE_HEAD_KEY, 0);
  }
  
  // Set queue head
  bool setQueueHead(uint32_t head) {
    return NVSConfig::writeU32(POSITION_QUEUE_HEAD_KEY, head);
  }
  
  // Get queue tail (read position)
  uint32_t getQueueTail() {
    return NVSConfig::readU32(POSITION_QUEUE_TAIL_KEY, 0);
  }
  
  // Set queue tail
  bool setQueueTail(uint32_t tail) {
    return NVSConfig::writeU32(POSITION_QUEUE_TAIL_KEY, tail);
  }
  
  // Get queue count
  uint32_t getQueueCount() {
    return NVSConfig::readU32(POSITION_QUEUE_COUNT_KEY, 0);
  }
  
  // Set queue count
  bool setQueueCount(uint32_t count) {
    return NVSConfig::writeU32(POSITION_QUEUE_COUNT_KEY, count);
  }

public:
  PositionServerTransmissionHandler() : initialized(false), maxQueueSize(0), maxCapacityBytes(0) {}
  
  // Initialize handler
  bool begin() {
    initialized = true;
    
    // Get available NVS space
    int availableSpace = NVSConfig::getAvailableNVSSpace();
    
    // Use only 50% of available space for queue
    maxCapacityBytes = availableSpace / 2;
    
    // Calculate max queue size based on entry size
    maxQueueSize = maxCapacityBytes / POSITION_MAX_ENTRY_SIZE;
    
    // Store max queue size
    NVSConfig::writeU32(POSITION_QUEUE_MAX_KEY, maxQueueSize);
    
    Serial.println("========== Position Server Transmission Handler ==========");
    Serial.println("Protocol: BLE (primary) -> WiFi (fallback) -> Internal Flash (fallback)");
    Serial.println("Server: " + String(POSITION_SERVER_URL));
    Serial.print("Available NVS Space: ");
    Serial.print(availableSpace);
    Serial.println(" bytes");
    Serial.print("Queue Capacity (50%): ");
    Serial.print(maxCapacityBytes);
    Serial.println(" bytes");
    Serial.print("Max Queue Entries: ");
    Serial.println(maxQueueSize);
    Serial.print("Current Queue Count: ");
    Serial.println(getQueueCount());
    Serial.println("Storage Mode: FIFO Blob Array");
    Serial.println("=====================================================");
    
    return true;
  }
  
  // Check if handler is initialized
  bool isInitialized() const {
    return initialized;
  }
  
  // Format sensor data with GPS coordinates for Position server
  // Format: device_id,battery%,voltage,timestamp,scenario,lat,lon,hdop
  String formatPositionData(const char* deviceId, int batteryPercent, float batteryVoltage, 
                       unsigned long long timestamp, int scenario, double latitude, double longitude, double hdop) {
    char payload[200];
    snprintf(payload, sizeof(payload), "%s,%d,%.2f,%llu,%d,%.7f,%.7f,%.2f", 
             deviceId, batteryPercent, batteryVoltage, timestamp, scenario, latitude, longitude, hdop);
    
    return String(payload);
  }
  
  // Save failed transmission to internal flash (NVS) using FIFO blob array
  bool saveToInternalFlash(const String& data) {
    Serial.println("PositionServerTransmissionHandler: Saving failed transmission to NVS FIFO queue...");
    
    if (data.length() > POSITION_MAX_ENTRY_SIZE) {
      Serial.println("PositionServerTransmissionHandler: ERROR - Data exceeds max entry size");
      return false;
    }
    
    uint32_t head = getQueueHead();
    uint32_t count = getQueueCount();
    uint32_t tail = getQueueTail();
    
    // Check if queue is full (FIFO mode - remove oldest)
    if (count >= maxQueueSize) {
      Serial.println("PositionServerTransmissionHandler: Queue full - removing oldest entry (FIFO)");
      
      // Remove oldest entry (at tail)
      String oldKey = getQueueBlobKey(tail);
      NVSConfig::eraseBlob(oldKey.c_str());
      
      // Move tail forward
      tail = (tail + 1) % maxQueueSize;
      setQueueTail(tail);
      count--;
    }
    
    // Write new entry at head
    String blobKey = getQueueBlobKey(head);
    bool success = NVSConfig::writeBlob(blobKey.c_str(), data.c_str(), data.length() + 1); // +1 for null terminator
    
    if (success) {
      // Move head forward
      head = (head + 1) % maxQueueSize;
      setQueueHead(head);
      setQueueCount(count + 1);
      
      Serial.print("PositionServerTransmissionHandler: Saved to blob '");
      Serial.print(blobKey);
      Serial.print("' (");
      Serial.print(data.length());
      Serial.print(" bytes). Queue count: ");
      Serial.println(count + 1);
    } else {
      Serial.println("PositionServerTransmissionHandler: Failed to save blob to NVS");
    }
    
    return success;
  }
  
  // Read batch of entries from queue
  // Returns array of strings (up to batchSize entries)
  bool readBatchFromQueue(String* batch, uint32_t batchSize, uint32_t& actualCount) {
    actualCount = 0;
    uint32_t count = getQueueCount();
    uint32_t tail = getQueueTail();
    
    if (count == 0) {
      Serial.println("PositionServerTransmissionHandler: Queue is empty");
      return false;
    }
    
    // Read up to batchSize entries
    uint32_t entriesToRead = min(batchSize, count);
    
    Serial.print("PositionServerTransmissionHandler: Reading ");
    Serial.print(entriesToRead);
    Serial.println(" entries from queue");
    
    for (uint32_t i = 0; i < entriesToRead; i++) {
      uint32_t index = (tail + i) % maxQueueSize;
      String blobKey = getQueueBlobKey(index);
      
      // Get blob size first
      size_t blobSize = NVSConfig::getBlobSize(blobKey.c_str());
      if (blobSize == 0 || blobSize > POSITION_MAX_ENTRY_SIZE) {
        Serial.print("PositionServerTransmissionHandler: Invalid blob size for key ");
        Serial.println(blobKey);
        continue;
      }
      
      // Read blob
      char buffer[POSITION_MAX_ENTRY_SIZE];
      size_t readSize = NVSConfig::readBlob(blobKey.c_str(), buffer, sizeof(buffer));
      
      if (readSize > 0) {
        buffer[readSize - 1] = '\0'; // Ensure null termination
        batch[actualCount] = String(buffer);
        actualCount++;
      }
    }
    
    return (actualCount > 0);
  }
  
  // Remove batch of entries from queue (after successful transmission)
  bool removeBatchFromQueue(uint32_t count) {
    if (count == 0) return true;
    
    uint32_t queueCount = getQueueCount();
    uint32_t tail = getQueueTail();
    
    if (count > queueCount) {
      Serial.println("PositionServerTransmissionHandler: ERROR - Trying to remove more entries than available");
      return false;
    }
    
    // Remove entries from tail
    for (uint32_t i = 0; i < count; i++) {
      String blobKey = getQueueBlobKey(tail);
      NVSConfig::eraseBlob(blobKey.c_str());
      tail = (tail + 1) % maxQueueSize;
    }
    
    // Update tail and count
    setQueueTail(tail);
    setQueueCount(queueCount - count);
    
    Serial.print("PositionServerTransmissionHandler: Removed ");
    Serial.print(count);
    Serial.print(" entries. Remaining: ");
    Serial.println(queueCount - count);
    
    return true;
  }
  
  // Send data via BLE
  // Returns: TransmissionResult with success status and method
  TransmissionResult sendViaBLE(const String& data) {
    TransmissionResult result = {false, "BLE", 0};
    
    if (!initialized) {
      Serial.println("PositionServerTransmissionHandler: Not initialized");
      return result;
    }
    
    // Check BLE connection
    if (!BLEConfig::isConnected()) {
      Serial.println("PositionServerTransmissionHandler: BLE not connected");
      return result;
    }
    
    Serial.println("PositionServerTransmissionHandler: Sending via BLE...");
    Serial.print("  Data size: ");
    Serial.print(data.length());
    Serial.println(" bytes");
    
    // Send via BLE
    bool bleSuccess = BLEConfig::sendDataViaBLE(data);
    
    if (bleSuccess) {
      Serial.println("PositionServerTransmissionHandler: BLE transmission successful");
      result.success = true;
      result.responseCode = 200; // Assume success (BLE doesn't return HTTP codes)
    } else {
      Serial.println("PositionServerTransmissionHandler: BLE transmission failed");
    }
    
    return result;
  }
  
  // Send data via WiFi to Position server
  // Returns: TransmissionResult with success status and method
  TransmissionResult sendViaWiFi(const String& data) {
    TransmissionResult result = {false, "WiFi", 0};
    
    if (!initialized) {
      Serial.println("PositionServerTransmissionHandler: Not initialized");
      return result;
    }
    
    // Check WiFi connection with multiple verifications
    if (!CustomWiFi::isConnected()) {
      Serial.println("PositionServerTransmissionHandler: WiFi not connected");
      return result;
    }
    
    // Additional WiFi stability check - ensure we have an IP address
    if (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
      Serial.println("PositionServerTransmissionHandler: WiFi connected but no IP address");
      return result;
    }
    
    // Small delay to ensure TCP/IP stack is fully initialized
    // This helps avoid UDP socket locking issues during DNS resolution
    delay(100);
    
    Serial.println("PositionServerTransmissionHandler: Sending via WiFi...");
    Serial.print("  Data size: ");
    Serial.print(data.length());
    Serial.println(" bytes");
    Serial.print("  IP address: ");
    Serial.println(WiFi.localIP());
    
    // Send via HTTP POST
    HTTPClient http;
    
    // Use direct begin() - HTTPClient handles HTTPS automatically
    // The delay above helps ensure TCP/IP stack is ready for DNS resolution
    http.begin(POSITION_SERVER_URL);
    
    // Set timeout AFTER begin() (recommended approach)
    http.setTimeout(POSITION_TRANSMISSION_TIMEOUT);
    
    // Only set Content-Type header (like working example)
    http.addHeader("Content-Type", "text/csv");
    
    int httpResponseCode = http.POST(data);
    
    Serial.print("PositionServerTransmissionHandler: Response code: ");
    Serial.println(httpResponseCode);
    
    result.responseCode = httpResponseCode;
    
    if (httpResponseCode == 200) {
      Serial.println("PositionServerTransmissionHandler: WiFi transmission successful");
      result.success = true;
    } else {
      Serial.print("PositionServerTransmissionHandler: WiFi transmission failed - code ");
      Serial.println(httpResponseCode);
    }
    
    http.end();
    return result;
  }
  
  // Main transmission handler for Position server
  // Try BLE first, then WiFi if BLE fails
  // Automatically gathers device ID, battery info, and timestamp from device at current time
  // Parameters: scenario, GPS coordinates, HDOP
  // Returns: TransmissionResult with success status and method used
  TransmissionResult handlePositionServerTransmission(int scenario, double latitude, double longitude, double hdop) {
    if (!initialized) {
      Serial.println("PositionServerTransmissionHandler: Not initialized");
      return {false, "None", 0};
    }
    
    // Automatically gather current device values
    char deviceIdBuffer[32];
    DeviceID::getDeviceId(deviceIdBuffer, sizeof(deviceIdBuffer));
    const char* deviceId = deviceIdBuffer;
    int batteryPercent = BatteryMonitor::getBatteryPercentage();
    float batteryVoltage = BatteryMonitor::readBatteryVoltage();
    unsigned long long timestamp = TimeSync::getCurrentTimeMillis();
    
    Serial.println("========== Position Server Transmission ==========");
    Serial.print("Device ID: ");
    Serial.println(deviceId);
    Serial.print("Battery: ");
    Serial.print(batteryPercent);
    Serial.print("% (");
    Serial.print(batteryVoltage, 2);
    Serial.println("V)");
    Serial.print("Timestamp: ");
    Serial.println(timestamp);
    Serial.print("Scenario: ");
    Serial.println(scenario);
    Serial.print("GPS: (");
    Serial.print(latitude, 7);
    Serial.print(", ");
    Serial.print(longitude, 7);
    Serial.print("), HDOP: ");
    Serial.println(hdop, 2);
    
    // Format current data for Position server
    String currentData = formatPositionData(deviceId, batteryPercent, batteryVoltage, timestamp, scenario, latitude, longitude, hdop);
    
    // Save current data to queue FIRST (ensures FIFO order: earliest first)
    Serial.println("PositionServerTransmissionHandler: Adding current data to queue");
    bool savedCurrent = saveToInternalFlash(currentData);
    if (!savedCurrent) {
      Serial.println("PositionServerTransmissionHandler: CRITICAL - Failed to save current data to queue");
      TransmissionResult failResult = {false, "None", 0};
      return failResult;
    }
    
    // CHUNKED TRANSMISSION LOOP: Send ALL blobs from earliest first (FIFO)
    uint32_t totalInQueue = getQueueCount();
    uint32_t totalSent = 0;
    uint32_t chunksProcessed = 0;
    TransmissionResult result = {false, "None", 0};
    
    Serial.print("PositionServerTransmissionHandler: Starting FIFO chunked transmission (");
    Serial.print(totalInQueue);
    Serial.println(" entries in queue)");
    
    // Loop: Send chunks from earliest (tail) until queue is empty or transmission fails
    while (getQueueCount() > 0) {
      uint32_t pendingCount = getQueueCount();
      uint32_t chunkSize = min((uint32_t)POSITION_CHUNK_SIZE, pendingCount);
      
      Serial.print("PositionServerTransmissionHandler: Chunk ");
      Serial.print(chunksProcessed + 1);
      Serial.print(" - Reading ");
      Serial.print(chunkSize);
      Serial.print(" entries from queue (earliest first)");
      Serial.println();
      
      // Read chunk from queue (starting from tail = oldest/earliest)
      String* batch = new String[chunkSize];
      uint32_t actualCount = 0;
      
      if (!readBatchFromQueue(batch, chunkSize, actualCount)) {
        delete[] batch;
        Serial.println("PositionServerTransmissionHandler: Failed to read from queue");
        break;
      }
      
      // Build payload from batch
      String payload = "";
      for (uint32_t i = 0; i < actualCount; i++) {
        if (payload.length() > 0) {
          payload += "\n";
        }
        payload += batch[i];
      }
      
      delete[] batch;
      
      Serial.print("Payload size: ");
      Serial.print(payload.length());
      Serial.print(" bytes (");
      Serial.print(actualCount);
      Serial.println(" entries)");
      
      // Send chunk
      result = sendPayloadWithAutoControl(payload);
      
      if (result.success) {
        // Remove sent entries from queue
        removeBatchFromQueue(actualCount);
        totalSent += actualCount;
        chunksProcessed++;
        
        Serial.print("PositionServerTransmissionHandler: Chunk ");
        Serial.print(chunksProcessed);
        Serial.print(" sent successfully (");
        Serial.print(actualCount);
        Serial.print(" entries). Total sent: ");
        Serial.print(totalSent);
        Serial.print(", Remaining: ");
        Serial.println(getQueueCount());
        
        // Continue to next chunk if queue not empty
        if (getQueueCount() == 0) {
          Serial.println("PositionServerTransmissionHandler: All data sent successfully!");
          break;
        }
        
        yield(); // Prevent watchdog
      } else {
        // Transmission failed - data remains in queue for next attempt
        Serial.print("PositionServerTransmissionHandler: Chunk ");
        Serial.print(chunksProcessed + 1);
        Serial.println(" transmission failed - stopping loop");
        Serial.print("PositionServerTransmissionHandler: ");
        Serial.print(getQueueCount());
        Serial.println(" entries remain in queue for next attempt");
        break;
      }
    }
    
    // Summary
    Serial.println("========== Transmission Summary ==========");
    Serial.print("Chunks processed: ");
    Serial.println(chunksProcessed);
    Serial.print("Total entries sent: ");
    Serial.println(totalSent);
    Serial.print("Remaining in queue: ");
    Serial.println(getQueueCount());
    Serial.println("==========================================");
    
    // Mark as success if data is safely stored (even if not transmitted)
    if (!result.success && getQueueCount() > 0) {
      result.success = true;
      result.method = "Flash";
      result.responseCode = 0;
    }
    
    Serial.println("=============================================");
    
    return result;
  }
  
  // Send current data to Position server (automatically gathers all device values)
  // This is the main method for sending real-time data to Position database server
  // All device values (ID, battery, voltage, timestamp) are gathered automatically at current time
  bool sendData(int scenario, double latitude, double longitude, double hdop) {
    if (!initialized) {
      Serial.println("PositionServerTransmissionHandler: Not initialized");
      return false;
    }
    
    TransmissionResult result = handlePositionServerTransmission(scenario, latitude, longitude, hdop);
    return result.success;
  }
  
};

#endif // POSITION_SERVER_TRANSMISSION_H
