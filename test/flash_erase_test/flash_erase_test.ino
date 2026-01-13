// SPI Flash Chip Erase Test
// Tests complete chip erase functionality and CSV storage reset
//
// Test Functions:
// 1. Initialize SPI flash
// 2. Display flash chip information
// 3. Menu-driven interface for:
//    - Full chip erase
//    - Sector erase
//    - Write test data
//    - Read test data
//    - Display storage statistics
//
// WARNING: Chip erase will delete ALL data on the flash chip!

#include <Arduino.h>
#include "../../spi_flash_handler.h"
#include "../../unified_csv_storage.h"

// Create instances
SPIFlashHandler spiFlash;
UnifiedCSVStorage unifiedCSVStorage;

// Test data
const char* testData = "Test data written after erase";
const uint32_t testAddress = 0x1000; // Test at address 4KB

void printMenu() {
  Serial.println("\n========================================");
  Serial.println("SPI Flash Erase Test Menu");
  Serial.println("========================================");
  Serial.println("1 - Full chip erase (WARNING: Deletes ALL data!)");
  Serial.println("2 - Erase single sector (4KB at address 0x0000)");
  Serial.println("3 - Write test data");
  Serial.println("4 - Read test data");
  Serial.println("5 - Display flash info");
  Serial.println("6 - Display CSV storage statistics");
  Serial.println("7 - Reset CSV storage pointers");
  Serial.println("8 - Show menu again");
  Serial.println("========================================");
  Serial.println("Enter command (1-8):");
}

void displayFlashInfo() {
  Serial.println("\n--- Flash Chip Information ---");
  Serial.print("Manufacturer ID: 0x");
  Serial.println(spiFlash.getManufacturerID(), HEX);
  Serial.print("Capacity: ");
  Serial.print(spiFlash.getCapacity());
  Serial.println(" bytes");
  Serial.print("Max Pages: ");
  Serial.println(spiFlash.getMaxPages());
  Serial.println("------------------------------");
}

void fullChipErase() {
  Serial.println("\n!!! WARNING: FULL CHIP ERASE !!!");
  Serial.println("This will delete ALL data on the flash chip!");
  Serial.println("Type 'YES' to confirm (within 10 seconds):");
  
  unsigned long startTime = millis();
  String confirmation = "";
  
  while (millis() - startTime < 10000) {
    if (Serial.available()) {
      char ch = Serial.read();
      if (ch == '\n' || ch == '\r') {
        if (confirmation.length() > 0) break;
      } else {
        confirmation += ch;
      }
    }
    delay(10);
  }
  
  if (confirmation == "YES") {
    Serial.println("\nStarting chip erase...");
    Serial.println("This may take 20-60 seconds...");
    
    unsigned long eraseStart = millis();
    bool success = spiFlash.eraseChip();
    unsigned long eraseTime = millis() - eraseStart;
    
    if (success) {
      Serial.print("✓ Chip erase successful! Time: ");
      Serial.print(eraseTime);
      Serial.println(" ms");
      
      // Reset CSV storage pointers
      if (unifiedCSVStorage.isInitialized()) {
        unifiedCSVStorage.clear();
        Serial.println("✓ CSV storage pointers reset");
      }
    } else {
      Serial.println("✗ Chip erase FAILED!");
    }
  } else {
    Serial.println("\nChip erase cancelled (confirmation not received)");
  }
}

void sectorErase() {
  Serial.println("\n--- Sector Erase ---");
  Serial.println("Erasing 4KB sector at address 0x0000...");
  
  unsigned long eraseStart = millis();
  bool success = spiFlash.eraseSector(0x0000);
  unsigned long eraseTime = millis() - eraseStart;
  
  if (success) {
    Serial.print("✓ Sector erase successful! Time: ");
    Serial.print(eraseTime);
    Serial.println(" ms");
  } else {
    Serial.println("✗ Sector erase FAILED!");
  }
}

void writeTestData() {
  Serial.println("\n--- Write Test Data ---");
  Serial.print("Writing to address 0x");
  Serial.print(testAddress, HEX);
  Serial.println("...");
  
  // First erase the sector
  uint32_t sectorAddress = (testAddress / 4096) * 4096;
  Serial.print("Erasing sector at 0x");
  Serial.println(sectorAddress, HEX);
  spiFlash.eraseSector(sectorAddress);
  
  // Write test data
  size_t len = strlen(testData) + 1; // Include null terminator
  bool success = spiFlash.write(testAddress, (uint8_t*)testData, len);
  
  if (success) {
    Serial.print("✓ Wrote ");
    Serial.print(len);
    Serial.println(" bytes successfully");
    Serial.print("Data: \"");
    Serial.print(testData);
    Serial.println("\"");
  } else {
    Serial.println("✗ Write FAILED!");
  }
}

void readTestData() {
  Serial.println("\n--- Read Test Data ---");
  Serial.print("Reading from address 0x");
  Serial.print(testAddress, HEX);
  Serial.println("...");
  
  char buffer[64] = {0};
  bool success = spiFlash.read(testAddress, (uint8_t*)buffer, sizeof(buffer) - 1);
  
  if (success) {
    Serial.println("✓ Read successful");
    Serial.print("Data: \"");
    Serial.print(buffer);
    Serial.println("\"");
    
    // Check if it matches test data
    if (strcmp(buffer, testData) == 0) {
      Serial.println("✓ Data matches expected test data");
    } else {
      Serial.println("✗ Data does NOT match (might be erased or empty)");
    }
  } else {
    Serial.println("✗ Read FAILED!");
  }
}

void displayCSVStorageStats() {
  Serial.println("\n--- CSV Storage Statistics ---");
  
  if (!unifiedCSVStorage.isInitialized()) {
    Serial.println("CSV storage not initialized");
    return;
  }
  
  Serial.print("Has data to read: ");
  Serial.println(unifiedCSVStorage.hasDataToRead() ? "YES" : "NO");
  
  Serial.print("Available space: ");
  Serial.print(unifiedCSVStorage.getAvailableSpace());
  Serial.println(" bytes");
  
  Serial.print("Used space: ");
  Serial.print(unifiedCSVStorage.getUsedSpace());
  Serial.println(" bytes");
  
  Serial.println("------------------------------");
}

void resetCSVStorage() {
  Serial.println("\n--- Reset CSV Storage ---");
  
  if (!unifiedCSVStorage.isInitialized()) {
    Serial.println("CSV storage not initialized");
    return;
  }
  
  unifiedCSVStorage.clear();
  Serial.println("✓ CSV storage pointers reset");
  Serial.println("Write pointer: 0");
  Serial.println("Read pointer: 0");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n========================================");
  Serial.println("SPI Flash Erase Test");
  Serial.println("========================================\n");
  
  // Initialize SPI Flash
  Serial.println("Initializing SPI Flash...");
  if (!spiFlash.begin()) {
    Serial.println("✗ SPI Flash initialization FAILED!");
    Serial.println("Check wiring:");
    Serial.println("  CS   → GPIO 18");
    Serial.println("  MISO → GPIO 2");
    Serial.println("  MOSI → GPIO 7");
    Serial.println("  SCK  → GPIO 6");
    while (1) delay(1000);
  }
  Serial.println("✓ SPI Flash initialized successfully");
  
  // Display flash info
  displayFlashInfo();
  
  // Initialize CSV storage
  Serial.println("\nInitializing CSV storage...");
  if (!unifiedCSVStorage.begin(&spiFlash)) {
    Serial.println("✗ CSV storage initialization FAILED!");
  } else {
    Serial.println("✓ CSV storage initialized successfully");
  }
  
  // Display initial statistics
  displayCSVStorageStats();
  
  // Show menu
  printMenu();
}

void loop() {
  if (!Serial.available()) {
    delay(10);
    return;
  }
  
  char cmd = Serial.read();
  
  // Clear remaining input
  while (Serial.available()) {
    Serial.read();
    delay(1);
  }
  
  switch (cmd) {
    case '1':
      fullChipErase();
      break;
      
    case '2':
      sectorErase();
      break;
      
    case '3':
      writeTestData();
      break;
      
    case '4':
      readTestData();
      break;
      
    case '5':
      displayFlashInfo();
      break;
      
    case '6':
      displayCSVStorageStats();
      break;
      
    case '7':
      resetCSVStorage();
      break;
      
    case '8':
      printMenu();
      break;
      
    default:
      if (cmd != '\n' && cmd != '\r') {
        Serial.print("Unknown command: ");
        Serial.println(cmd);
        Serial.println("Enter 8 to show menu");
      }
      break;
  }
  
  Serial.println(); // Add spacing
}
