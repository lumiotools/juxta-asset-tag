#ifndef SPI_FLASH_HANDLER_H
#define SPI_FLASH_HANDLER_H

#include <SPI.h>
#include <SPIMemory.h>

// Default pins for ESP32-S3 DevKitC-1 (as used in test/spi_flash.ino)
#define SPI_FLASH_CS   10
#define SPI_FLASH_MISO 12
#define SPI_FLASH_MOSI 13
#define SPI_FLASH_SCK  14

class SPIFlashHandler {
private:
  int _csPin;
  int _misoPin;
  int _mosiPin;
  int _sckPin;
  SPIFlash _flash;
  bool _initialized;

public:
  SPIFlashHandler(int cs = SPI_FLASH_CS, int miso = SPI_FLASH_MISO, int mosi = SPI_FLASH_MOSI, int sck = SPI_FLASH_SCK)
    : _csPin(cs), _misoPin(miso), _mosiPin(mosi), _sckPin(sck), _flash(cs), _initialized(false) {
  }

  // Check if flash device is connected and responding
  bool checkDeviceConnection() {
    // Try to initialize the flash chip (detects device and reads chip ID)
    if (!_flash.begin()) {
      return false;
    }
    
    // Additional verification: try to read capacity (requires device to respond)
    uint32_t capacity = _flash.getCapacity();
    if (capacity == 0) {
      return false; // Invalid capacity indicates device not responding
    }
    
    return true;
  }

  bool begin() {
    if (_initialized) return true;

    Serial.print("Initializing SPI Flash (CS=");
    Serial.print(_csPin);
    Serial.print(", MISO=");
    Serial.print(_misoPin);
    Serial.print(", MOSI=");
    Serial.print(_mosiPin);
    Serial.print(", SCK=");
    Serial.print(_sckPin);
    Serial.println(")...");

    // Initialize SPI bus with defined pins
    SPI.begin(_sckPin, _misoPin, _mosiPin, _csPin);
    delay(10); // Small delay for SPI bus to stabilize

    // Check if flash device is connected and responding
    if (!checkDeviceConnection()) {
      Serial.println("ERROR: SPI Flash device not detected or not responding!");
      Serial.println("Check: 1) Wiring connections 2) CS pin 3) Power supply");
      return false;
    }

    _initialized = true;
    Serial.print("SPI Flash initialized successfully. Capacity: ");
    Serial.print(_flash.getCapacity());
    Serial.println(" bytes");
    return true;
  }

  bool isInitialized() {
    return _initialized;
  }

  // Erase a 4KB sector at the given address
  bool eraseSector(uint32_t addr) {
    if (!_initialized) return false;
    return _flash.eraseSector(addr);
  }

  // Erase the entire chip (use with caution, takes time)
  bool eraseChip() {
    if (!_initialized) return false;
    return _flash.eraseChip();
  }

  // Write a string to flash at a specific address
  // Note: You might need to erase the sector first if it's not empty
  bool writeString(uint32_t addr, String text) {
    if (!_initialized) return false;
    return _flash.writeStr(addr, text);
  }

  // Read a string from flash at a specific address
  bool readString(uint32_t addr, String &output) {
    if (!_initialized) return false;
    return _flash.readStr(addr, output);
  }
  
  // Read bytes from flash at a specific address
  bool readBytes(uint32_t addr, uint8_t* buffer, size_t len) {
    if (!_initialized) return false;
    return _flash.readCharArray(addr, (char*)buffer, len);
  }
  
  // Read char array from flash at a specific address
  bool readCharArray(uint32_t addr, char* buffer, size_t len) {
    if (!_initialized) return false;
    return _flash.readCharArray(addr, buffer, len);
  }
  
  // Write char array to flash at a specific address
  bool writeCharArray(uint32_t addr, const char* data, size_t len) {
    if (!_initialized) return false;
    // Cast away const for library call (library doesn't modify the data)
    return _flash.writeCharArray(addr, (char*)data, len);
  }
  
  // Get flash capacity in bytes
  uint32_t getCapacity() {
    if (!_initialized) return 0;
    return _flash.getCapacity();
  }
};

#endif

