#ifndef TRANSMISSION_HANDLER_H
#define TRANSMISSION_HANDLER_H

#include "data_queue.h"
#include "customwifi.h"
#include "ble_config.h"

class TransmissionHandler {
private:
  DataQueue dataQueue;

  // Unified send function - tries both WiFi and BLE
  // Only attempts transmission if WiFi is connected or BLE is connected
  bool sendData(const String& data) {
    bool wifiSuccess = false;
    bool bleSuccess = false;
    
    // Try BLE transmission if connected
    if (BLEConfig::isConnected()) {
      bleSuccess = BLEConfig::sendDataViaBLE(data);
    }

    // Only try WiFi if BLE didn't succeed and WiFi is connected
    if (!bleSuccess && CustomWiFi::isConnected()) {
      wifiSuccess = CustomWiFi::sendSensorData(data);
    }
    
    // Consider successful if either method worked
    return (wifiSuccess || bleSuccess);
  }

public:
  // Initialize the data queue (must be called before use)
  bool begin(SPIFlashHandler* flashHandler) {
    return dataQueue.begin(flashHandler);
  }

  // Check if transmission handler is initialized
  bool isInitialized() {
    return dataQueue.isInitialized();
  }

  // Check transmission status and handle accordingly
  // Uses batch reading to send data in chunks
  // Returns: true if data sent successfully (via WiFi or BLE), false if saved to queue or failed
  bool handleDataTransmission(String currentCSV) {
    // Check if WiFi or BLE is connected before attempting transmission
    bool wifiConnected = CustomWiFi::isConnected();
    bool bleConnected = BLEConfig::isConnected();
    
    // First, read and send any existing queued data (prioritize old data)
    bool allQueuedSent = true;
    if (isInitialized() && !dataQueue.isEmpty() && (wifiConnected || bleConnected)) {
      Serial.println("TransmissionHandler: Reading and sending queued data first...");
      
      // Safety counter to prevent infinite loops
      int maxAttempts = 100; // Limit to 100 batches per session
      int attemptCount = 0;
      int consecutiveEmptyReads = 0; // Track consecutive empty reads
      
      while (!dataQueue.isEmpty() && attemptCount < maxAttempts) {
        String batchData = dataQueue.readBatch();
        attemptCount++;
        
        if (batchData.length() == 0) {
          consecutiveEmptyReads++;
          Serial.print("TransmissionHandler: Empty read (");
          Serial.print(consecutiveEmptyReads);
          Serial.println(" consecutive)");
          
          // If we get 3 consecutive empty reads despite queue not being empty, something is wrong
          if (consecutiveEmptyReads >= 3) {
            Serial.println("TransmissionHandler: ERROR - Multiple empty reads but queue reports not empty!");
            Serial.println("TransmissionHandler: This indicates a read failure or corrupted queue state");
            allQueuedSent = false;
            break;
          }
          break; // No more data
        }
        
        // Reset consecutive empty counter on successful read
        consecutiveEmptyReads = 0;
        
        Serial.print("TransmissionHandler: Sending queued batch (");
        Serial.print(batchData.length());
        Serial.println(" bytes)");
        
        // Print first 200 chars for verification (not the full garbage)
        if (batchData.length() <= 200) {
          Serial.println(batchData);
        } else {
          Serial.print("First 200 chars: ");
          Serial.println(batchData.substring(0, 200));
        }
        
        bool success = sendData(batchData);
        
        if (success) {
          dataQueue.commitRead(batchData);
          Serial.println("TransmissionHandler: Queued batch sent successfully");
        } else {
          Serial.println("TransmissionHandler: Queued batch transmission failed - stopping");
          allQueuedSent = false;
          break;
        }
      }
      
      // Warn if we hit the max attempts limit
      if (attemptCount >= maxAttempts) {
        Serial.println("TransmissionHandler: WARNING - Reached maximum transmission attempts limit");
        allQueuedSent = false;
      }
    }
    
    // Now try to send current data directly (without writing to flash)
    bool currentDataSent = false;
    if (wifiConnected || bleConnected) {
      Serial.println("TransmissionHandler: Attempting direct transmission of current data (no flash write)...");
      Serial.println(currentCSV);
      currentDataSent = sendData(currentCSV);
      
      if (currentDataSent) {
        Serial.println("TransmissionHandler: Current data sent successfully - skipping flash write");
        return allQueuedSent; // Return true if all queued data was also sent
      } else {
        Serial.println("TransmissionHandler: Direct transmission failed - will queue current data");
      }
    } else {
      Serial.println("TransmissionHandler: No WiFi/BLE connection - will queue current data");
    }
    
    // Current data transmission failed or no connection - need to queue it
    // Check if queue is initialized before using it
    if (!isInitialized()) {
      Serial.println("TransmissionHandler: Queue not initialized (SPI Flash may have failed). Current data will be lost.");
      return false; // Can't queue, data lost
    }
    
    // Add current data to queue (this writes to flash)
    Serial.println("TransmissionHandler: Adding current data to queue (writing to flash)...");
    if (!dataQueue.enqueue(currentCSV)) {
      Serial.println("TransmissionHandler: Failed to enqueue current data!");
      return false;
    }
    
    // Return false since current data had to be queued (not sent)
    return false;
  }

  // Get reference to data queue for external monitoring
  DataQueue* getDataQueue() {
    return &dataQueue;
  }
  
  // Force save queue pointers to NVS (call before deep sleep or reset)
  void saveQueueState() {
    if (isInitialized()) {
      dataQueue.forceSavePointers();
    } else {
      Serial.println("TransmissionHandler: Cannot save queue state - not initialized");
    }
  }

};

#endif
