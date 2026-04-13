# Juxta Asset Tag Firmware

This repository contains the firmware for the Juxta Asset Tag.

## Prerequisites

- **Arduino IDE 2.x** (recommended)
- **ESP32 board support (Espressif Systems)** installed via Arduino Boards Manager
- Target board selection: **ESP32C6 Dev Module**

### Arduino IDE libraries (Library Manager)

Install these via **Arduino IDE → Tools → Manage Libraries…**:

- **FastLED** (LED/status indicator)
- **Adafruit Unified Sensor**
- **NimBLE-Arduino** (by h2zero)
- **PKAE_Timer**
- **SPIMemory** (external SPI flash)

## Configure required server URLs (compulsory)

Before compiling/flashing, set these two URLs in the firmware source:

1. **Model server URL**
   - Edit: [model_server_transmission.h](model_server_transmission.h#L13)
   - Set: `MODEL_SERVER_URL`

2. **Position server URL**
   - Edit: [position_server_transmission.h](position_server_transmission.h#L14)
   - Set: `POSITION_SERVER_URL`

Notes:
- Use a **full URL including protocol** (must start with `http://` or `https://`).
- These values are compile-time constants in the current firmware.