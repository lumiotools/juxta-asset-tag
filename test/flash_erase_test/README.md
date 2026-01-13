# SPI Flash Erase Test

This test program provides a menu-driven interface to test and manage the external SPI flash chip (W25Q64).

## Features

1. **Full Chip Erase** - Erases the entire flash chip (requires confirmation)
2. **Sector Erase** - Erases a single 4KB sector
3. **Write Test Data** - Writes test data to a specific address
4. **Read Test Data** - Reads and verifies test data
5. **Flash Info** - Displays chip information (manufacturer, capacity, etc.)
6. **CSV Storage Stats** - Shows storage usage statistics
7. **Reset CSV Pointers** - Resets read/write pointers in CSV storage

## Hardware Connections

```
ESP32-S3 → W25Q64 Flash
-----------------------
GPIO 10  → CS (Chip Select)
GPIO 12  → MISO (SO/DO)
GPIO 13  → MOSI (SI/DI)
GPIO 14  → SCK (Clock)
3.3V     → VCC
GND      → GND
```

## Usage

1. Open the test in Arduino IDE
2. Select your ESP32-S3 board
3. Upload the sketch
4. Open Serial Monitor at 115200 baud
5. Use the menu options (1-8) to test flash operations

## Menu Commands

```
1 - Full chip erase (WARNING: Deletes ALL data!)
2 - Erase single sector (4KB at address 0x0000)
3 - Write test data
4 - Read test data
5 - Display flash info
6 - Display CSV storage statistics
7 - Reset CSV storage pointers
8 - Show menu again
```

## Safety

⚠️ **WARNING**: Full chip erase (option 1) will delete ALL data on the flash chip!
- You will be prompted to type "YES" within 10 seconds to confirm
- Erase operation may take 20-60 seconds
- Do not power off the device during erase operation

## Expected Output

After successful initialization, you should see:
```
✓ SPI Flash initialized successfully
Manufacturer ID: 0xEF (Winbond)
Capacity: 8388608 bytes (8MB)
✓ CSV storage initialized successfully
```

## Troubleshooting

If flash initialization fails:
1. Check all wiring connections
2. Verify power supply is stable (3.3V)
3. Check SPI pin assignments match your hardware
4. Ensure flash chip is properly seated (if using socket)

## Related Files

- `spi_flash_handler.h` - SPI flash driver
- `unified_csv_storage.h` - CSV storage management
- Main firmware: `juxta-asset-tag-main.ino` (lines 314-323 for erase code)
