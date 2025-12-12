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
      Serial.print("TransmissionHandler::sendData: Attempting WiFi transmission (");
      Serial.print(data.length());
      Serial.println(" bytes)...");
      wifiSuccess = CustomWiFi::sendSensorData(data);
      if (wifiSuccess) {
        Serial.println("TransmissionHandler::sendData: WiFi transmission successful");
      } else {
        Serial.println("TransmissionHandler::sendData: WiFi transmission failed");
      }
    } else if (!bleSuccess) {
      Serial.println("TransmissionHandler::sendData: WiFi not connected, skipping WiFi attempt");
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
    // IMPORTANT: Save currentCSV length immediately (before any operations that might affect memory)
    size_t currentCSVLength = currentCSV.length();
    
    // Check if WiFi or BLE is connected before attempting transmission
    bool wifiConnected = CustomWiFi::isConnected();
    bool bleConnected = BLEConfig::isConnected();
    
    Serial.print("TransmissionHandler: Connection status - WiFi: ");
    Serial.print(wifiConnected ? "CONNECTED" : "DISCONNECTED");
    Serial.print(", BLE: ");
    Serial.println(bleConnected ? "CONNECTED" : "DISCONNECTED");
    
    Serial.print("TransmissionHandler: Received data length: ");
    Serial.print(currentCSVLength);
    Serial.println(" bytes");
    
    // Verify currentCSV is still valid after initial checks
    if (currentCSV.length() != currentCSVLength) {
      Serial.print("TransmissionHandler: ERROR - Data length changed! Expected: ");
      Serial.print(currentCSVLength);
      Serial.print(", Actual: ");
      Serial.println(currentCSV.length());
    }
    
    // First, read and send any existing queued data (prioritize old data)
    // Limit to 3 packets to avoid long transmission times
    // BUT: Skip queued data if currentCSV is large (prioritize current batch)
    bool allQueuedSent = true;
    bool skipQueuedData = (currentCSVLength > 10000); // Skip queued data if current batch is > 10KB
    
    if (skipQueuedData) {
      Serial.println("TransmissionHandler: Large batch detected - skipping queued data processing to preserve memory");
    } else if (isInitialized() && !dataQueue.isEmpty() && (wifiConnected || bleConnected)) {
      Serial.println("TransmissionHandler: Reading and sending queued data first...");
      
      // Limit to 3 packets maximum per transmission cycle
      const int MAX_PACKETS_PER_CYCLE = 3;
      int packetsSent = 0;
      int consecutiveEmptyReads = 0; // Track consecutive empty reads
      
      while (!dataQueue.isEmpty() && packetsSent < MAX_PACKETS_PER_CYCLE) {
        String batchData = dataQueue.readBatch();
        
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
        
        // Yield before sending to prevent watchdog reset during large transmissions
        yield();
        
        bool success = sendData(batchData);
        
        if (success) {
          dataQueue.commitRead(batchData);
          packetsSent++;
          Serial.print("TransmissionHandler: Queued batch sent successfully (");
          Serial.print(packetsSent);
          Serial.print("/");
          Serial.print(MAX_PACKETS_PER_CYCLE);
          Serial.println(" packets)");
          
          // Yield after successful transmission to prevent loop freezing
          yield();
        } else {
          Serial.println("TransmissionHandler: Queued batch transmission failed - stopping");
          allQueuedSent = false;
          break;
        }
      }
      
      // Check if there's more data remaining in queue
      if (!dataQueue.isEmpty() && packetsSent >= MAX_PACKETS_PER_CYCLE) {
        Serial.print("TransmissionHandler: Reached packet limit (");
        Serial.print(MAX_PACKETS_PER_CYCLE);
        Serial.println(" packets sent). Remaining data will be sent in next cycle.");
        allQueuedSent = false;
      }
    }
    
    // Verify currentCSV is still valid before sending
    if (currentCSV.length() != currentCSVLength) {
      Serial.print("TransmissionHandler: ERROR - Data corrupted during queued processing! Expected: ");
      Serial.print(currentCSVLength);
      Serial.print(", Actual: ");
      Serial.println(currentCSV.length());
      return false;
    }
    
    // Now try to send current data directly (without writing to flash)
    bool currentDataSent = false;
    if (wifiConnected || bleConnected) {
      Serial.println("TransmissionHandler: Attempting direct transmission of current data (no flash write)...");
      Serial.print("TransmissionHandler: Current data length: ");
      Serial.print(currentCSV.length());
      Serial.print(" bytes (expected: ");
      Serial.print(currentCSVLength);
      Serial.println(" bytes)");
      
      if (currentCSV.length() == 0) {
        Serial.println("TransmissionHandler: ERROR - Current data is empty (0 bytes)! Skipping transmission.");
        Serial.print("TransmissionHandler: This should not happen - original length was: ");
        Serial.println(currentCSVLength);
        return false;
      }
      
      if (currentCSV.length() != currentCSVLength) {
        Serial.print("TransmissionHandler: ERROR - Data length mismatch! Expected: ");
        Serial.print(currentCSVLength);
        Serial.print(", Actual: ");
        Serial.println(currentCSV.length());
        return false;
      }
      
      // Print first 100 chars for debugging (not full data)
      if (currentCSV.length() <= 100) {
        Serial.print("TransmissionHandler: Data: ");
        Serial.println(currentCSV);
      } else {
        Serial.print("TransmissionHandler: First 100 chars: ");
        Serial.println(currentCSV.substring(0, 100));
      }
      
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
