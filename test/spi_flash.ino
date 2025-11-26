/*
  ESP32 External SPI Flash Menu Demo
  1 - Write text to flash
  2 - Read text from flash
*/

#include <SPI.h>
#include <SPIMemory.h>

// --- Your wiring on ESP32-S3 DevKitC-1 ---
static const int PIN_CS   = 10;   // CS
static const int PIN_MISO = 12;   // DO  -> MISO
static const int PIN_MOSI = 13;   // DI  -> MOSI
static const int PIN_SCK  = 14;   // CLK

SPIFlash flash(PIN_CS);

const uint32_t FLASH_ADDR = 0x00000;     // sector 0
const size_t   MAX_MSG_LEN = 256;

enum InputState {
  WAIT_CMD,
  WAIT_TEXT
};

InputState inputState = WAIT_CMD;
char inputBuffer[MAX_MSG_LEN];
size_t inputPos = 0;
bool awaitingFirstNewline = false;

void printMenu() {
  Serial.println();
  Serial.println("=== Flash Menu ===");
  Serial.println("1 - Write new text");
  Serial.println("2 - Read stored text");
  Serial.println("==================");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== W25Q64 Menu Test ===");

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
