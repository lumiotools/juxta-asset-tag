/*
  ESP32 External SPI Flash Menu Demo
  1 - Write text to flash
  2 - Read text from flash
  3 - Erase entire chip (with CSV storage reset)
*/

#include <SPI.h>
#include <SPIMemory.h>
#include <nvs_flash.h>
#include <nvs.h>

// --- Your wiring on ESP32-S3 DevKitC-1 ---
static const int PIN_CS   = 10;   // CS
static const int PIN_MISO = 12;   // DO  -> MISO
static const int PIN_MOSI = 13;   // DI  -> MOSI
static const int PIN_SCK  = 14;   // CLK

SPIFlash flash(PIN_CS);

const uint32_t FLASH_ADDR = 0x00000;     // sector 0
const size_t   MAX_MSG_LEN = 256;

// CSV Storage constants (hardcoded from unified_csv_storage.h)
#define CSV_FLASH_START_ADDR 0x000000
const char* NVS_NAMESPACE = "wifi_config";
const char* CSV_WRITE_PTR_KEY = "csv_write_ptr";
const char* CSV_READ_PTR_KEY = "csv_read_ptr";

enum InputState {
  WAIT_CMD,
  WAIT_TEXT
};

InputState inputState = WAIT_CMD;
char inputBuffer[MAX_MSG_LEN];
size_t inputPos = 0;
bool awaitingFirstNewline = false;

// Hardcoded CSV storage reset function (from unified_csv_storage.h lines 402-408)
// This resets the write and read pointers in NVS after chip erase
void resetCSVStorage() {
  nvs_handle_t nvsHandle;
  
  // Open NVS in read-write mode
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle) != ESP_OK) {
    Serial.println("✗ Failed to open NVS for CSV reset");
    return;
  }
  
  // Reset write pointer to start address (0x000000)
  esp_err_t err1 = nvs_set_u32(nvsHandle, CSV_WRITE_PTR_KEY, CSV_FLASH_START_ADDR);
  
  // Reset read pointer to start address (0x000000)
  esp_err_t err2 = nvs_set_u32(nvsHandle, CSV_READ_PTR_KEY, CSV_FLASH_START_ADDR);
  
  // Commit changes
  esp_err_t err3 = nvs_commit(nvsHandle);
  
  // Close NVS
  nvs_close(nvsHandle);
  
  if (err1 == ESP_OK && err2 == ESP_OK && err3 == ESP_OK) {
    Serial.println("✓ CSV storage pointers reset after chip erase");
    Serial.print("  writePtr = 0x");
    Serial.println(CSV_FLASH_START_ADDR, HEX);
    Serial.print("  readPtr = 0x");
    Serial.println(CSV_FLASH_START_ADDR, HEX);
  } else {
    Serial.println("✗ Failed to reset CSV storage pointers");
  }
}

void printMenu() {
  Serial.println();
  Serial.println("=== Flash Menu ===");
  Serial.println("1 - Write new text");
  Serial.println("2 - Read stored text");
  Serial.println("3 - Erase entire chip (WARNING: Deletes ALL data!)");
  Serial.println("==================");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== W25Q64 Menu Test ===");

  // Initialize NVS (required for CSV storage reset)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    ret = nvs_flash_init();
  }
  if (ret == ESP_OK) {
    Serial.println("NVS initialized successfully");
  } else {
    Serial.println("NVS initialization failed (non-critical)");
  }

  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);

  if (!flash.begin()) {
    Serial.println("Flash NOT detected. Check wiring.");
    while (1) delay(10);
  }

  // Erase once at start
  Serial.println("Erasing 4KB sector @ 0x000000...");
  flash.eraseSector(FLASH_ADDR);

  printMenu();
}

void loop() {
  if (!Serial.available()) return;

  char ch = Serial.read();

  switch (inputState) {

    case WAIT_CMD:
      if (ch == '1') {
        Serial.println("\nEnter text to store, then press ENTER:");
        inputPos = 0;
        memset(inputBuffer, 0, sizeof(inputBuffer));
        inputState = WAIT_TEXT;
        awaitingFirstNewline = true; // ignore first ENTER
      }
      else if (ch == '2') {
        char buf[MAX_MSG_LEN] = {0};
        bool okR = flash.readCharArray(FLASH_ADDR, buf, sizeof(buf));
        Serial.println("\n--- Stored Text ---");
        if (okR) Serial.println(buf);
        else     Serial.println("Read FAILED");
        Serial.println("-------------------");
        printMenu();
      }
      else if (ch == '3') {
        Serial.println("\n!!! WARNING: FULL CHIP ERASE !!!");
        Serial.println("This will delete ALL data on the flash chip!");
        Serial.println("Type 'YES' to confirm (within 10 seconds):");
        
        unsigned long startTime = millis();
        String confirmation = "";
        
        // Wait for confirmation input
        while (millis() - startTime < 10000) {
          if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
              if (confirmation.length() > 0) break;
            } else {
              confirmation += c;
              Serial.print(c); // Echo character
            }
          }
          delay(10);
        }
        
        Serial.println(); // New line after input
        
        if (confirmation == "YES") {
          Serial.println("\nStarting chip erase...");
          Serial.println("This may take 20-60 seconds...");
          
          unsigned long eraseStart = millis();
          bool x = flash.eraseChip();
          unsigned long eraseTime = millis() - eraseStart;
          
          String y = x ? "true" : "false";
          Serial.print("Erased: ");
          Serial.println(y);
          
          if (x) {
            Serial.print("✓ Chip erase successful! Time: ");
            Serial.print(eraseTime);
            Serial.println(" ms");
            Serial.println("All data has been erased from the flash chip");
            
            // Reset CSV storage pointers in NVS (hardcoded from unified_csv_storage.h)
            Serial.println("\nResetting CSV storage pointers...");
            resetCSVStorage();
          } else {
            Serial.println("✗ Chip erase FAILED!");
          }
        } else {
          Serial.print("Chip erase cancelled (received: '");
          Serial.print(confirmation);
          Serial.println("', expected: 'YES')");
        }
        
        printMenu();
      }
      break;


    case WAIT_TEXT:

      // Ignore the ENTER immediately after pressing "1"
      if (awaitingFirstNewline) {
        if (ch == '\n' || ch == '\r') {
          return; // keep ignoring
        }
        awaitingFirstNewline = false; // now start recording real text
      }

      if (ch == '\r') return;

      if (ch == '\n') {
        // Finish message
        inputBuffer[inputPos] = '\0';

        Serial.println("\nSaving to flash...");
        flash.eraseSector(FLASH_ADDR);
        flash.writeCharArray(FLASH_ADDR, inputBuffer, inputPos + 1);

        Serial.println("Saved!");
        inputState = WAIT_CMD;
        printMenu();
      } 
      else {
        // Normal character
        if (inputPos < MAX_MSG_LEN - 1) {
          inputBuffer[inputPos++] = ch;
          Serial.print(ch);
        }
      }
      break;
  }
}
