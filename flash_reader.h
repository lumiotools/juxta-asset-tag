// flash_reader.h - Simple modular flash data reader with dynamic chunk sizing
// Reads CSV entries from external flash and sends via BLE or WiFi

#ifndef FLASH_READER_H
#define FLASH_READER_H

#include "unified_csv_storage.h"
#include "transmission_handler.h"
#include "customwifi.h"

class FlashReader {
private:
  UnifiedCSVStorage* storage;
  TransmissionHandler* transmitter;
  
  // Statistics
  uint32_t entriesRead;
  uint32_t entriesSent;
  uint32_t entriesFailed;
  
  // Calculate safe chunk size based on available RAM
  // Returns: number of entries that can fit safely in RAM
  uint32_t calculateSafeChunkSize(size_t& maxChunkBytes) {
    size_t freeHeap = ESP.getFreeHeap();
    size_t maxAlloc = ESP.getMaxAllocHeap();
    
    Serial.println("\n--- RAM Analysis ---");
    Serial.print("Free Heap: ");
    Serial.print(freeHeap);
    Serial.println(" bytes");
    Serial.print("Largest Block: ");
    Serial.print(maxAlloc);
    Serial.println(" bytes");
    
    // Use only 25% of free heap (more conservative to reduce peak memory usage)
    size_t safeHeap = (freeHeap * 25) / 100;
    
    // Cap at a fraction of the largest contiguous block
    size_t safeLargestBlock = (maxAlloc * 50) / 100;
    
    // Use smaller of the two
    size_t availableForBatch = (safeHeap < safeLargestBlock) ? safeHeap : safeLargestBlock;
    
    // Absolute maximum: 10KB (reduce peak allocations)
    if (availableForBatch > 10000) {
      availableForBatch = 10000;
    }
    
    // Absolute minimum: 4KB
    if (availableForBatch < 4000) {
      Serial.println("WARNING: Very low memory available!");
      availableForBatch = 4000;
    }
    
    maxChunkBytes = availableForBatch;
    
    // Estimate entries per chunk
    // Average CSV entry: ~4000 bytes (100 IMU readings × ~40 bytes per reading)
    const size_t AVG_ENTRY_SIZE = 4000;
    const size_t HEADER_OVERHEAD = 500; // Reserve for batch overhead
    
    size_t availableForEntries = (availableForBatch > HEADER_OVERHEAD) ? 
                                  (availableForBatch - HEADER_OVERHEAD) : 0;
    
    uint32_t entriesPerChunk = availableForEntries / AVG_ENTRY_SIZE;
    
    // Minimum 5 entries, maximum 100 entries per chunk
    if (entriesPerChunk < 5) entriesPerChunk = 5;
    if (entriesPerChunk > 100) entriesPerChunk = 100;
    
    Serial.print("Safe Batch Size: ");
    Serial.print(maxChunkBytes);
    Serial.print(" bytes (");
    Serial.print(entriesPerChunk);
    Serial.println(" entries)");
    Serial.println("--------------------\n");
    
    return entriesPerChunk;
  }
  
public:
  // Constructor
  FlashReader(UnifiedCSVStorage* csvStorage, TransmissionHandler* txHandler) {
    storage = csvStorage;
    transmitter = txHandler;
    resetStats();
  }
  
  // Reset statistics
  void resetStats() {
    entriesRead = 0;
    entriesSent = 0;
    entriesFailed = 0;
  }
  
  // Send all data via BLE (one entry at a time)
  // BLE has small MTU (~512 bytes) so we send individual entries
  bool sendViaBLE() {
    if (!storage || !storage->isInitialized()) {
      Serial.println("ERROR: Storage not initialized");
      return false;
    }
    
    if (!transmitter) {
      Serial.println("ERROR: Transmitter not available");
      return false;
    }
    
    resetStats();
    
    Serial.println("=== Flash Reader: BLE Mode ===");
    Serial.println("Sending: One entry at a time (BLE MTU limit)");
    printStorageState();
    
    if (!storage->hasDataToRead()) {
      Serial.println("No data to send");
      return true;
    }
    
    // Check RAM before starting
    Serial.print("Free heap before BLE transmission: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
    
    // Send individual entries
    while (storage->hasDataToRead()) {
      entriesRead++;
      
      // Read entry (does NOT advance read pointer)
      String entry = storage->readNextCSVEntry();
      
      if (entry.length() == 0) {
        Serial.println("No more data");
        break;
      }
      
      // Show progress
      Serial.print("[Entry ");
      Serial.print(entriesRead);
      Serial.print("] ");
      Serial.print(entry.length());
      Serial.print(" bytes... ");
      
      // Send via BLE
      bool sent = transmitter->handleDataTransmission(entry);
      
      if (sent) {
        // Success - mark as sent (advances read pointer)
        storage->markAsSent();
        entriesSent++;
        Serial.println("✓ OK");
        
        // Show progress every 10 entries
        if (entriesSent % 10 == 0) {
          Serial.print("  Progress: ");
          Serial.print(entriesSent);
          Serial.print(" entries sent (heap: ");
          Serial.print(ESP.getFreeHeap());
          Serial.println(" bytes)");
        }
      } else {
        // Failed - don't advance pointer
        storage->markAsFailed();
        entriesFailed++;
        Serial.println("✗ FAILED");
        
        // Stop on first failure
        Serial.println("Stopping - entry remains for retry");
        storage->saveState();
        printSummary("BLE");
        return false;
      }
      
      delay(10); // Small delay between BLE sends
    }
    
    // Save final state
    storage->saveState();
    printSummary("BLE");
    
    return (entriesFailed == 0);
  }
  
  // Send all data via WiFi (batched for efficiency)
  // WiFi can handle larger payloads, so we batch multiple entries
  bool sendViaWiFi() {
    if (!storage || !storage->isInitialized()) {
      Serial.println("ERROR: Storage not initialized");
      return false;
    }
    
    if (!transmitter) {
      Serial.println("ERROR: Transmitter not available");
      return false;
    }
    
    resetStats();
    
    Serial.println("=== Flash Reader: WiFi Mode ===");
    Serial.println("Sending: Batched entries (WiFi efficiency)");
    printStorageState();
    
    if (!storage->hasDataToRead()) {
      Serial.println("No data to send");
      return true;
    }
    
    // Calculate safe chunk size based on available RAM
    size_t maxChunkBytes;
    uint32_t chunkSize = calculateSafeChunkSize(maxChunkBytes);
    
    uint32_t batchNumber = 0;
    
    while (storage->hasDataToRead()) {
      batchNumber++;
      
      // Check RAM before each batch
      size_t heapBefore = ESP.getFreeHeap();
      
      // Build batch (use readNextCSVEntry without advancing storage readPtr; collect end pointer)
      String batch = "";
      batch.reserve(maxChunkBytes); // Pre-allocate to avoid fragmentation

      uint32_t entriesInBatch = 0;
      size_t batchBytes = 0;
      uint32_t batchStartEntry = entriesRead + 1;

      // Save start pointer (we will only advance the read ptr after a successful send)
      uint32_t startReadPtr = storage->getReadPtr();
      uint32_t batchEndPtr = startReadPtr;

      // Read entries for this batch (do NOT call markAsSent during building)
      while (storage->hasDataToRead() && entriesInBatch < chunkSize) {
        String entry = storage->readNextCSVEntry();

        if (entry.length() == 0) {
          break; // No more data
        }

        // Check if adding this entry would exceed max size
        size_t entrySize = entry.length() + (batch.length() > 0 ? 1 : 0); // +1 for newline
        if (batchBytes + entrySize > maxChunkBytes && entriesInBatch > 0) {
          // Batch is full - stop here and keep readPtr at startReadPtr
          Serial.println("  Batch full - capping at current size");
          break;
        }

        // Add entry to batch
        if (batch.length() > 0) {
          batch += "\n";
          batchBytes += 1;
        }
        batch += entry;
        batchBytes += entry.length();
        entriesInBatch++;
        entriesRead++;

        // Record end pointer for this entry (so we can advance readPtr after success)
        batchEndPtr = storage->getLastEntryEndPtr();
      }
      
      if (entriesInBatch == 0) {
        Serial.println("No entries in batch - stopping");
        break;
      }
      
      // Verify batch is valid
      if (batch.length() == 0) {
        Serial.println("ERROR: Empty batch created!");
        storage->markAsFailed();
        entriesFailed += entriesInBatch;
        break;
      }
      
      // Show batch info
      size_t heapAfter = ESP.getFreeHeap();
      Serial.print("\n[Batch ");
      Serial.print(batchNumber);
      Serial.print("] Entries ");
      Serial.print(batchStartEntry);
      Serial.print("-");
      Serial.print(batchStartEntry + entriesInBatch - 1);
      Serial.print(" (");
      Serial.print(entriesInBatch);
      Serial.print(" entries, ");
      Serial.print(batchBytes);
      Serial.println(" bytes)");
      Serial.print("  Heap used: ");
      Serial.print(heapBefore - heapAfter);
      Serial.print(" bytes, remaining: ");
      Serial.print(heapAfter);
      Serial.println(" bytes");
      Serial.print("  Sending... ");
      
      // Send batch via WiFi
      bool sent = transmitter->handleDataTransmission(batch);
      
      if (sent) {
        // Success - advance read pointer to end of last entry in this batch
        storage->setReadPtr(batchEndPtr);
        entriesSent += entriesInBatch;
        Serial.println("✓ OK");
        
        Serial.print("  Total sent: ");
        Serial.print(entriesSent);
        Serial.println(" entries");
      } else {
        // Failed - don't advance pointers
        storage->markAsFailed();
        entriesFailed += entriesInBatch;
        Serial.println("✗ FAILED");
        
        Serial.println("Stopping - batch remains for retry");
        storage->saveState();
        printSummary("WiFi");
        return false;
      }
      
      // Delay between batches to let WiFi/server recover
      delay(500);
      yield(); // Allow WiFi stack to process
      
      // Verify WiFi still connected before next batch
      if (storage->hasDataToRead()) {
        Serial.print("  Checking WiFi connection... ");
        if (!CustomWiFi::isConnected()) {
          Serial.println("DISCONNECTED - Reconnecting...");
          if (!CustomWiFi::connectWiFi()) {
            Serial.println("ERROR: WiFi reconnection failed");
            storage->saveState();
            printSummary("WiFi");
            return false;
          }
          Serial.println("Reconnected successfully");
          delay(500); // Extra delay after reconnection
        } else {
          Serial.println("OK");
        }
        
        // Recalculate chunk size for next batch (heap may have changed)
        chunkSize = calculateSafeChunkSize(maxChunkBytes);
      }
    }
    
    // Save final state
    storage->saveState();
    printSummary("WiFi");
    
    return (entriesFailed == 0);
  }
  
  // Read single entry without sending (for testing/debugging)
  String peekNextEntry() {
    if (!storage || !storage->hasDataToRead()) {
      return String("");
    }
    
    // Save current read pointer
    uint32_t savedPtr = storage->getReadPtr();
    
    // Read entry
    String entry = storage->readNextCSVEntry();
    
    // Restore pointer (don't consume entry)
    storage->setReadPtr(savedPtr);
    
    return entry;
  }
  
  // Count available entries (without consuming them)
  uint32_t countAvailableEntries() {
    if (!storage || !storage->hasDataToRead()) {
      return 0;
    }
    
    uint32_t count = 0;
    uint32_t savedPtr = storage->getReadPtr();
    
    Serial.print("Counting entries... ");
    
    while (storage->hasDataToRead()) {
      String entry = storage->readNextCSVEntry();
      if (entry.length() > 0) {
        count++;
        storage->markAsSent(); // Advance for counting
      } else {
        break;
      }
      
      // Safety: limit count iterations
      if (count > 10000) {
        Serial.println("WARNING: >10000 entries, stopping count");
        break;
      }
    }
    
    // Restore pointer
    storage->setReadPtr(savedPtr);
    
    Serial.println(count);
    return count;
  }
  
  // Print current storage state
  void printStorageState() {
    Serial.print("Read Ptr:  0x");
    Serial.print(storage->getReadPtr(), HEX);
    Serial.print(" | Write Ptr: 0x");
    Serial.println(storage->getWritePtr(), HEX);
    
    uint32_t available = countAvailableEntries();
    Serial.print("Available: ");
    Serial.print(available);
    Serial.println(" entries");
    Serial.println();
  }
  
  // Print transmission summary
  void printSummary(const char* mode) {
    Serial.println("\n╔════════════════════════════════╗");
    Serial.print("║   Transmission Summary (");
    Serial.print(mode);
    Serial.println(")   ║");
    Serial.println("╠════════════════════════════════╣");
    Serial.print("║ Entries Read:   ");
    Serial.print(entriesRead);
    for (int i = 0; i < (15 - String(entriesRead).length()); i++) Serial.print(" ");
    Serial.println("║");
    Serial.print("║ Entries Sent:   ");
    Serial.print(entriesSent);
    for (int i = 0; i < (15 - String(entriesSent).length()); i++) Serial.print(" ");
    Serial.println("║");
    Serial.print("║ Entries Failed: ");
    Serial.print(entriesFailed);
    for (int i = 0; i < (15 - String(entriesFailed).length()); i++) Serial.print(" ");
    Serial.println("║");
    Serial.println("╠════════════════════════════════╣");
    
    if (entriesFailed == 0 && entriesSent > 0) {
      Serial.println("║ Status: ✓ SUCCESS             ║");
    } else if (entriesFailed > 0) {
      Serial.println("║ Status: ⚠ PARTIAL             ║");
    } else {
      Serial.println("║ Status: ○ NO DATA             ║");
    }
    
    Serial.println("╚════════════════════════════════╝");
    Serial.print("Final Read Ptr: 0x");
    Serial.println(storage->getReadPtr(), HEX);
    Serial.println();
  }
  
  // Get statistics
  uint32_t getEntriesSent() { return entriesSent; }
  uint32_t getEntriesFailed() { return entriesFailed; }
  uint32_t getEntriesRead() { return entriesRead; }
};

#endif

