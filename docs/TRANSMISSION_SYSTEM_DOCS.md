# Data Transmission Retry System - Implementation Guide

This system implements intelligent data queuing for failed WiFi transmissions. It automatically batches failed data into JSON arrays and attempts retransmission.

---

## Step-by-Step Workflow

### STEP 1: First Data Attempt (Queue Empty)

- Current sensor data is in JSON format
- DataQueue is empty (no previous failures)
- Action: Send JSON directly via HTTP POST
- If SUCCESS: Data transmitted, cycle continues normally
- If FAILED: Data added to queue (now has 1 item)

### STEP 2: Second Data Attempt (Queue has 1 item)

- Queue contains 1 failed JSON object
- New sensor data arrives (another JSON object)
- Action: 
  1. Add new data to queue (now has 2 items)
  2. Convert queue to JSON ARRAY: `[{obj1}, {obj2}]`
  3. Send array via HTTP POST
- If SUCCESS: Both objects transmitted, queue cleared
- If FAILED: Both items remain in queue (now has 2 items)

### STEP 3: Multiple Failures (Queue has 2+ items)

- Queue accumulating failed transmissions (2 or more items)
- Serial print: `"!!! WARNING: Queue has X items !!!"`
- Serial print: `"!!! NEED BLE HERE - Queue is accumulating failed transmissions !!!"`
- Current data still added to queue if space available
- This indicates persistent WiFi failure
- ACTION REQUIRED: Implement BLE backup transmission
- Max queue capacity: 10 items (configurable)

---

## Data Structures

### `data_queue.h` - DataQueue Class

- Stores up to 10 failed JSON objects as String array
- FIFO queue management (First In, First Out)

**Key Methods:**
- `enqueue(jsonData)` - Add JSON to queue
- `dequeue()` - Remove and return first item
- `getLength()` - Get current queue size
- `isEmpty()` - Check if queue is empty
- `createJSONArray()` - Convert queue to JSON array format
- `clear()` - Empty entire queue
- `printQueueStatus()` - Debug output

### `transmission_handler.h` - TransmissionHandler Class

- Central logic for transmission decision-making
- Contains DataQueue instance internally

**Key Method:**
- `handleDataTransmission(currentJSON)`
  - Main function that implements the 3-step workflow
  - Returns: `true` if successful, `false` if failed
  - Handles all queuing logic automatically

---

## JSON Formats

### Single Transmission (Queue Empty)

```json
{
  "device_id": "AA:BB:CC:DD:EE:FF",
  "battery_level": 85,
  "timestamp": 1234567890,
  "imu": {...},
  "gps": {...}
}
```

### Batch Transmission (Queue has items)

```json
[
  {
    "device_id": "AA:BB:CC:DD:EE:FF",
    "battery_level": 85,
    "timestamp": 1234567890,
    "imu": {...},
    "gps": {...}
  },
  {
    "device_id": "AA:BB:CC:DD:EE:FF",
    "battery_level": 84,
    "timestamp": 1234567891,
    "imu": {...},
    "gps": {...}
  }
]
```

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

### 3. Function Call (in main loop)

```cpp
String jsonData = createSensorJSON(imuData, gpsData);
sendDataWithRetryLogic(jsonData);
```

### 4. Implementation Function

```cpp
void sendDataWithRetryLogic(String jsonData) {
  Serial.println("\n--- Starting Data Transmission with Retry Logic ---");
  bool success = transmissionHandler.handleDataTransmission(jsonData);
  if (success) {
    Serial.println("Data transmission successful!");
  } else {
    Serial.println("Data transmission failed - queued for retry");
  }
  postInProgress = false;
}
```

---

## Typical Operation Sequence

### Cycle 1

```
├─ Collect IMU + GPS data → Create JSON1
├─ Check queue: EMPTY
├─ Send JSON1 directly
└─ ✓ Success → Queue stays empty
```

### Cycle 2 (WiFi disconnected)

```
├─ Collect IMU + GPS data → Create JSON2
├─ Check queue: EMPTY
├─ Send JSON2 directly
└─ ✗ Failed → Add JSON2 to queue (Length: 1)
```

### Cycle 3 (WiFi still down)

```
├─ Collect IMU + GPS data → Create JSON3
├─ Check queue: LENGTH = 1
├─ Add JSON3 to queue (Length: 2)
├─ Create array: [JSON2, JSON3]
├─ Send array
└─ ✗ Failed → Queue keeps both items (Length: 2)
```

### Cycle 4 (WiFi still down)

```
├─ Collect IMU + GPS data → Create JSON4
├─ Check queue: LENGTH = 2 (≥ 2)
├─ ⚠️  Print "NEED BLE HERE"
├─ Add JSON4 to queue if space (Length: 3)
└─ Waiting for BLE implementation or WiFi recovery
```

### Cycle 5 (WiFi recovered)

```
├─ Collect IMU + GPS data → Create JSON5
├─ Check queue: LENGTH = 3 (≥ 2)
├─ ⚠️  Print "NEED BLE HERE" (still full)
└─ Add JSON5 to queue (Length: 4)
```

**Note:** Future enhancement: BLE module sends queued data or WiFi reconnects with array retry

---

## Error Handling

### Queue Full (10 items)

- New data cannot be added
- Serial output: `"ERROR: Queue is full! Data cannot be added."`
- BLE implementation required to clear queue

### Network Failure Detection

- Automatic via `CustomWiFi::sendSensorData()` return value
- No manual timeout checks needed

---

## Future Enhancements

### 1. BLE Implementation

- When queue reaches 2+ items, send via BLE instead of WiFi
- Can clear queue even if WiFi is unavailable
- **Status:** BLE is currently implemented for WiFi credential setup, but data transmission fallback is pending

### 2. Persistent Storage

- Save queue to SPIFFS/SD card
- Recover failed data after power loss

### 3. Compression

- Compress JSON before queuing to save memory
- Especially important for large arrays

### 4. Smart Retry

- Exponential backoff between WiFi retry attempts
- Check WiFi signal strength before attempting transmission

---

## Implementation Status

**Current Implementation:**
- ✅ Data queue system (max 10 items)
- ✅ Automatic retry logic with batch transmission
- ✅ Queue status monitoring and alerts
- ✅ Integration with main sketch

**Pending:**
- ⏳ BLE data transmission fallback (when queue ≥ 2 items)
- ⏳ Persistent storage for queue data
- ⏳ JSON compression
- ⏳ Smart retry with exponential backoff

---

**Last Updated:** December 2024  
**File:** `juxta-asset-tag-main.ino`  
**Related Files:** `data_queue.h`, `transmission_handler.h`, `customwifi.h`

