# Data Transmission Retry System - Implementation Guide (v2)

This system implements intelligent data queuing for failed WiFi/BLE transmissions. It automatically batches failed data into JSON arrays and attempts retransmission via WiFi or BLE. The queue persists to external SPI flash memory (W25Q128), ensuring data survives power cycles.

---

## Step-by-Step Workflow

### STEP 1: Add Current Data to Queue

- Current sensor data is in JSON format
- **Always** add current data to queue first (regardless of queue state)
- Queue persists to external SPI flash (W25Q128) after each change
- Queue survives power cycles (loaded from external flash on boot)

### STEP 2: Check Connections

- Check if WiFi is connected
- Check if BLE is connected
- If neither connected: Save to queue, return (no transmission attempt)
- If either connected: Proceed to transmission

### STEP 3: Create JSON Array

- Convert entire queue to JSON array format
- **Always** sends as array, even if only 1 item: `[{obj1}]` or `[{obj1}, {obj2}, ...]`
- Array format is consistent regardless of queue size

### STEP 4: Attempt Transmission

- **Try BLE first** (if connected):
  - Send array via BLE (chunked if large)
  - If SUCCESS: Clear queue, return true
  - If FAILED: Continue to WiFi
- **Try WiFi** (if connected and BLE failed or not available):
  - Send array via HTTP POST (5-second timeout)
  - If SUCCESS: Clear queue, return true
  - If FAILED: Keep data in queue, return false

### STEP 5: Queue Persistence

- Queue data automatically saved to external SPI flash (W25Q128) after each change
- On next boot: Queue loaded from external flash, retry continues
- Max queue capacity: Calculated dynamically based on available external flash space (typically 3-10 items)

---

## Data Structures

### `data_queue.h` - DataQueue Class

- Stores failed JSON objects as String array (dynamic max size based on external flash space)
- FIFO queue management (First In, First Out)
- **Persists to external SPI flash (W25Q128)** - survives power cycles
- Calculates max queue size on first boot based on available external flash space

**Key Methods:**
- `begin()` - Initialize queue (loads from external flash, calculates max size on first boot)
- `enqueue(jsonData)` - Add JSON to queue (auto-saves to external flash)
- `dequeue()` - Remove and return first item (auto-saves to external flash)
- `getLength()` - Get current queue size
- `isEmpty()` - Check if queue is empty
- `createJSONArray()` - Convert queue to JSON array format (always array, even if 1 item)
- `clear()` - Empty entire queue (auto-saves to external flash)
- `getMaxSize()` - Get maximum queue capacity

### `transmission_handler.h` - TransmissionHandler Class

- Central logic for transmission decision-making
- Contains DataQueue instance internally
- Tries BLE first, then WiFi
- Always sends data as JSON array format

**Key Methods:**
- `begin()` - Initialize transmission handler (must be called before use)
- `handleDataTransmission(currentJSON)`
  - Main function that implements the transmission workflow
  - Always adds current data to queue first
  - Creates JSON array from entire queue
  - Tries BLE first (if connected), then WiFi (if connected)
  - Returns: `true` if successful, `false` if failed
  - Handles all queuing logic automatically
- `getDataQueue()` - Get reference to data queue for external monitoring

---

## JSON Formats

### Transmission Format (Always JSON Array)

**Note:** System always sends data as JSON array, even if queue contains only 1 item.

**Single Item (Queue had only current data):**
```json
[
  {
    "device_id": "ASSET_TAG_001",
    "battery_level": 85,
    "timestamp": "2024-11-15T10:30:00Z",
    "imu": {...},
    "gps": {...}
  }
]
```

**Multiple Items (Queue had previous failed data):**
```json
[
  {
    "device_id": "ASSET_TAG_001",
    "battery_level": 85,
    "timestamp": "2024-11-15T10:29:30Z",
    "imu": {...},
    "gps": {...}
  },
  {
    "device_id": "ASSET_TAG_001",
    "battery_level": 84,
    "timestamp": "2024-11-15T10:30:00Z",
    "imu": {...},
    "gps": {...}
  }
]
```

**Device ID:** Hardcoded as "ASSET_TAG_001" in code (change for each device), not MAC address.

---

## Integration in `juxta-asset-tag-main.ino`

### 1. Include Files

```cpp
#include "data_queue.h"
#include "transmission_handler.h"
```

### 2. Global Instance

```cpp
TransmissionHandler transmissionHandler;
```

### 3. Initialization (in setup)

```cpp
// Initialize transmission handler and data queue (loads from flash, calculates max size on first boot)
if (!transmissionHandler.begin()) {
  Serial.println("Warning: Data queue initialization failed!");
}
```

### 4. Function Call (in main loop)

```cpp
String jsonData = createSensorJSON(imuData, gpsData);
bool sendSuccess = sendDataWithRetryLogic(jsonData);
```

### 5. Implementation Function

```cpp
bool sendDataWithRetryLogic(String jsonData) {
  // Use transmission handler to send via WiFi and/or BLE with queue logic
  // Always sends as JSON array (1 or more items)
  return transmissionHandler.handleDataTransmission(jsonData);
}
```

---

## Typical Operation Sequence

### Cycle 1 (WiFi/BLE Connected)

```
├─ Collect IMU + GPS data → Create JSON1
├─ Add JSON1 to queue (Length: 1, saved to external flash)
├─ Create array: [JSON1]
├─ Try BLE (if connected) or WiFi (if connected)
└─ ✓ Success → Queue cleared (Length: 0, saved to external flash)
```

### Cycle 2 (WiFi/BLE Disconnected)

```
├─ Collect IMU + GPS data → Create JSON2
├─ Add JSON2 to queue (Length: 1, saved to external flash)
├─ Check connections: WiFi ✗, BLE ✗
└─ No transmission attempt → Queue: [JSON2] (persists to external flash)
```

### Cycle 3 (WiFi/BLE Still Down)

```
├─ Collect IMU + GPS data → Create JSON3
├─ Add JSON3 to queue (Length: 2, saved to external flash)
├─ Check connections: WiFi ✗, BLE ✗
└─ No transmission attempt → Queue: [JSON2, JSON3] (persists to external flash)
```

### Cycle 4 (WiFi/BLE Recovered)

```
├─ Collect IMU + GPS data → Create JSON4
├─ Add JSON4 to queue (Length: 3, saved to external flash)
├─ Create array: [JSON2, JSON3, JSON4]
├─ Try BLE (if connected) or WiFi (if connected)
└─ ✓ Success → Queue cleared (Length: 0, saved to external flash)
```

### Cycle 5 (After Power Cycle)

```
├─ Boot: Queue loaded from external flash (Length: 0 if previous cycle succeeded)
├─ Collect IMU + GPS data → Create JSON5
├─ Add JSON5 to queue (Length: 1, saved to external flash)
├─ Create array: [JSON5]
├─ Try BLE (if connected) or WiFi (if connected)
└─ ✓ Success → Queue cleared
```

**Note:** Queue persists across power cycles via external SPI flash (W25Q128). Failed transmissions are automatically retried on next boot.

---

## Error Handling

### Queue Full (Max Size Reached)

- Max queue size calculated dynamically based on available external flash space (typically 3-10 items)
- New data cannot be added if queue is full
- Serial output: `"ERROR: Queue is full! Data cannot be added."`
- Queue persists to external flash, will retry on next boot or when connection restored

### Network Failure Detection

- Automatic via `CustomWiFi::sendSensorData()` return value (WiFi)
- Automatic via `BLEConfig::sendDataViaBLE()` return value (BLE)
- No manual timeout checks needed
- Transmission handler tries BLE first, then WiFi if BLE not available

### Connection Status

- Checks `CustomWiFi::isConnected()` for WiFi status
- Checks `BLEConfig::isConnected()` for BLE status
- Only attempts transmission if at least one connection is active
- If neither connected, data saved to queue without transmission attempt

---

## Future Enhancements

### 1. Compression

- Compress JSON before queuing to save external flash space
- Especially important for large arrays
- Could increase max queue size

### 2. Smart Retry

- Exponential backoff between retry attempts
- Check WiFi signal strength before attempting transmission
- Prioritize BLE when WiFi signal is weak

### 3. Queue Management

- Automatic queue size optimization based on external flash usage
- Queue rotation (FIFO with max age limit)
- Queue statistics and monitoring

### 4. Enhanced BLE Transmission

- Larger MTU negotiation for faster BLE transmission
- BLE connection priority when WiFi unavailable

---

## Implementation Status

**Current Implementation:**
- ✅ Data queue system (dynamic max size based on external flash space, typically 3-10 items)
- ✅ Automatic retry logic with batch transmission (always JSON array format)
- ✅ Queue persistence to external SPI flash (W25Q128) (survives power cycles)
- ✅ BLE data transmission fallback (tries BLE first, then WiFi)
- ✅ Integration with main sketch
- ✅ First boot queue size calculation
- ✅ Queue loaded from external flash on boot

**Pending:**
- ⏳ JSON compression (to increase max queue size)
- ⏳ Smart retry with exponential backoff
- ⏳ Queue statistics and monitoring

---

**Last Updated:** December 2024  
**File:** `juxta-asset-tag-main.ino`  
**Related Files:** `data_queue.h`, `transmission_handler.h`, `customwifi.h`, `ble_config.h`, `spi_flash_handler.h`

**Note (v2):** v2 uses external SPI flash (W25Q128) for storage instead of internal NVS. The queue and WiFi credentials are stored on the external flash chip.

