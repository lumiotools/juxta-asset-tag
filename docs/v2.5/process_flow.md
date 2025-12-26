# Device Process Flow v2.5

Final firmware logic for ESP32-C6 based asset tracking device.

**Hardware Platform:** ESP32-C6 Mini

---

## Process Flow

The device starts up (powers on) with both **IMU and GPS powered ON**.

The GPS is initialized and attempts to acquire a fix for the next **2 minutes**. From this point, there can be **four scenarios**.

---

## 1. GPS Finds a High Accuracy Fix

a. When the GPS finds a **high accuracy fix** (during the 2-minute acquisition period or after), the device stores the GPS location in a reference position variable (stored in NVS/internal flash) and **starts the IMU read cycle immediately**.  
b. The IMU data is read and stored at a **100 Hz interval (10 ms)** using interrupt-driven ticker.  
c. The GPS data is read and stored at a different cycle time configured by the user in the UI *(default value to be provided by the Juxta team)*.  
d. The device would keep recording the data and send it at transmission cycle intervals to the UI/backend server with DB *(hosted by PMC)*.  
e. The **high accuracy GPS value** would be sent directly to the backend server (via BLE or WiFi) in the format: `device_id,battery%,voltage,timestamp,scenario,lat,lon`  
f. This would continue till the device has high GPS accuracy and the accuracy does not fall below the decided threshold *(threshold to be provided by the Juxta team)*.  
g. If the GPS fix is lost during this scenario (checked at transmission cycle time), the device would switch to **Scenario 2** with the last stored position in the reference position variable as the reference point.  
**Note:**  
- The reference position variable (containing the GPS location when IMU started) is **updated after every transmission cycle** with the current high accuracy GPS position.  
- Threshold to be configurable on the UI  
- IMU sampling starts **as soon as high accuracy fix is detected**, even during the 2-minute acquisition period

---

## 2. GPS Finds a Low Accuracy Fix

a. The device keeps trying to find a high accuracy fix for 2 minutes, but at the end of 2 minutes has not been able to find one.  
b. In this scenario, the device would store the last known GPS position (low accuracy) in the reference position variable (stored in NVS/internal flash) and **start recording the IMU data at a 100 Hz interval (10 ms) after the 2-minute acquisition period completes**.  
c. The **GPS power turns OFF** to conserve battery.  
d. The IMU data, along with the reference position stored in the variable, would be sent to the model server (hosted by Juxta) in the format:  
`(lat, long, hdop), imuObj1, imuObj2, …`  
e. The model server would then respond with delta position (change in the provided latitude and longitude), which would be processed in the device and saved as the new position.  
f. This computed latitude and longitude would be sent to the UI/backend server with DB (via BLE or WiFi).  
g. **GPS fix attempts are continuous and independent of transmission cycles**: After GPS is turned off, the device waits for a configurable "GPS on after" delay (set via UI), then powers on GPS for 1 minute to attempt a fix, while the IMU continues reading and storing values.  
h. If a **high accuracy fix is found** during a fix attempt, the device would switch to **Scenario 1** logic.  
i. If a **low accuracy fix is found** or **no fix is found**, the device would continue with **Scenario 2** logic and GPS turns OFF again.  
**Note:**  
- The reference position variable is **updated after every transmission cycle** with the processed value from the last received delta position response.  
- If the GPS fix is lost during **Scenario 1**, the device would switch to **Scenario 2** with the last stored position in the reference position variable as the reference point.  
- GPS fix attempts run continuously in the background, independent of transmission cycle timing

---

## 3. GPS Could Not Find a Fix

a. If the GPS is not able to find a fix within 2 minutes, the device would go into **deep sleep (full power-saving mode) for the next 30 seconds** *(time provided by the Juxta team)*, wake up, and try to find a fix again.  
b. This process would continue till the GPS finds either a high accuracy fix or a low accuracy fix, after which the device would follow **Scenario 1** or **Scenario 2** based on the type of fix.  
**Note:**  
- The device would **not be sending any data** till it receives a fix *(as mentioned by the Juxta team)*.  
- IMU sampling does not start in this scenario

---

## 4. Initial Position Is Input from the UI

a. There would be an option for the user to provide the initial latitude and longitude position from the Dashboard in Configure Mode (via BLE).  
b. In this scenario, the device would consider this as the initial location, **turn off GPS immediately**, and **start IMU sampling immediately**.  
c. The device continues the process according to **Scenario 2.b** onwards (IMU data sent to model server with position prefix).  
d. If the device finds a fix during GPS fix attempts (if enabled), it will switch to scenario 1 or 2 based on fix accuracy.  
**Note:**  
- GPS remains OFF when UI position is provided  
- IMU sampling starts immediately upon receiving UI position

---

## Transmission Logic

When the device detects that the transmission cycle time/condition is hit, it performs the following actions:

### Scenario-Based Transmission

#### Scenario 1: High Accuracy GPS
- Reads current GPS position
- Sends position directly to backend server (BLE or WiFi) in format: `device_id,battery%,voltage,timestamp,scenario,lat,lon`
- Updates reference position in NVS with current GPS position
- No IMU data transmission (only GPS position)

#### Scenario 2/4: Model Server Transmission
- Gets reference position from NVS (last known position)
- Connects to WiFi (model server requires WiFi)
- Reads IMU data from flash storage in batches
- Each batch is sent with position prefix: `(lat, long, hdop), imuObj1, imuObj2, ...`
- Model server returns delta position (Δlat, Δlon)
- Position is updated iteratively: `newLat = oldLat + Δlat`, `newLon = oldLon + Δlon`
- After all batches, sends final computed position to backend server (BLE or WiFi)
- Updates reference position in NVS with final computed position

#### Fallback (Other Scenarios)
- Uses FlashReader to read CSV entries from flash
- Tries BLE first (one entry at a time)
- Falls back to WiFi (batches multiple entries)
- If both fail, data remains in flash for next cycle

### Batch Processing and Transmission

a. The IMU data is read from external flash storage (circular buffer) and the device checks how many CSV entries are stored.  
b. The stored IMU data is divided into batches based on available free RAM to optimize memory usage (typically 4-10KB per batch, up to 10 entries per batch).  
c. Each batch is transmitted in separate POST requests to the model server.  
d. Each batch request is prefixed with the current latitude and longitude position in the format:  
`(lat, long, hdop), imuObj1, imuObj2, …`  
e. The first batch is sent to the model server, and a delta position (change in latitude and longitude) is received in the response.  
f. Using the last known position and the received delta position, the device calculates the current position.  
g. The next batch request is sent using this newly calculated processed position as the prefix.  
h. This process continues iteratively until all IMU entries are transmitted successfully.

### Position Update After Transmission

After all IMU objects are transmitted successfully:

- **In Scenario 1:** The last position (reference position variable) is updated to the current position from GPS (high accuracy value).  
- **In Scenario 2/4:** The last position (reference position variable) is updated to the processed value from the last received delta position response.

### GPS Fix Finding in Scenario 2

GPS fix attempts in Scenario 2 are **continuous and independent of transmission cycles**:

a. After GPS is turned OFF (after 2-minute acquisition or after fix attempt), the device waits for a configurable "GPS on after" delay (set via UI).  
b. The GPS power turns ON automatically after the delay.  
c. The GPS attempts to find a fix for the next 1 minute while the IMU continues reading and storing values in the background.  
d. If a **high accuracy fix is found**, the device switches to **Scenario 1** logic.  
e. If a **low accuracy fix is found** or **no fix is found**, the device continues with **Scenario 2** logic.  
f. The GPS power turns OFF again after the fix attempt to conserve battery.  
g. The cycle repeats (wait for "GPS on after" delay, then 1-minute fix attempt).

### Communication Protocols

- **Model Server (Hosted by Juxta):**  
  Data transmission to the model server (initial position and raw IMU data) is **only sent through WiFi** due to the large payload size of IMU data.  
  Uses separate endpoint: `MODEL_SERVER_URL` (configurable in code)
    
- **Backend Server with DB (Hosted by PMC):**  
  Data for saving in the database (latitude, longitude values with device details) can be sent through **both BLE and WiFi** due to its small size.  
  Uses separate endpoint: `SERVER_URL` (configurable in code)
    
- **Configuration Mode:**  
  **BLE is used for configuration mode** to allow device setup and initial position input from the Dashboard.  
  When the device is connected, the Dashboard will display a countdown timer indicating the remaining duration for the configuration mode.  
  The Configuration mode can be extended by sending a value to the "Extend Config Time" characteristic (adds specified seconds to configuration window).  
  Configuration mode duration: Minimum 1 minute from start, or 1 minute after connection (whichever is longer), plus any extension time.

---

## Dashboard Configuration Fields

The following fields can be configured via the Dashboard (BLE interface) during Configuration Mode:

### WiFi Configuration
- **WiFi SSID** (String): Network name for WiFi connection
- **WiFi Password** (String): Password for WiFi network
- **Current SSID** (Read-only): Displays currently saved WiFi SSID and device information (device ID, version, timestamp, battery level, voltage, GPS settings, remaining config time)

### Transmission Settings
- **Cycle Time** (Integer, seconds): Transmission cycle interval (default: 900 seconds / 15 minutes)
  - Time between transmission cycles
  - Stored in NVS and persists across reboots

### GPS Configuration
- **GPS Active** (Uint8, 0 or 1): Enable/disable GPS functionality
  - 0 = GPS OFF (switches to Scenario 4 if initial position is set)
  - 1 = GPS ON (normal GPS operation)
  - When set to 0 with initial position set, device switches to Scenario 4 and clears stored data

- **GPS Read Cycle Time** (Integer, seconds): Interval for reading GPS data in Scenario 1 (high accuracy mode)
  - Default: 0 (disabled)
  - Only applies when device is in Scenario 1 (high accuracy GPS fix)

- **GPS Accuracy Threshold** (Float): HDOP threshold for determining high vs low accuracy GPS fix
  - Used to distinguish between Scenario 1 and Scenario 2
  - Configurable via UI (threshold to be provided by Juxta team)

- **GPS On After** (Integer, seconds): Delay before GPS fix attempts in Scenario 2
  - Time to wait after GPS is turned off before attempting next fix
  - Default: 60 seconds
  - Only applies in Scenario 2 (low accuracy mode)

### Position Configuration
- **Initial Position** (String, format: "lat,lon"): Manual position input from Dashboard
  - Sets initial latitude and longitude
  - When set, device immediately:
    - Turns off GPS
    - Switches to Scenario 4
    - Starts IMU sampling immediately
  - Example format: "37.7749,-122.4194"

### Configuration Window
- **Extend Config Time** (Integer, seconds): Extends configuration window duration
  - Adds specified seconds to the configuration period
  - Can be called multiple times to extend further
  - Silent operation (no response/indication)

### Read-Only Status Fields
- **Status** (Read-only): Device status ("Ready")
- **Current SSID** (Read-only): Returns CSV string with device information:
  - Format: `device_id,device_version,timestamp,battery%,voltage,currentSSID,gps_cycle_time,transmission_time,gps_threshold,gps_on_after,gps_active,remaining_time`
  - Updated automatically when BLE connection is established

**Note:** All configuration values are saved to NVS (internal flash) immediately upon receipt and persist across device reboots and deep sleep cycles.

---

## Server Endpoints

There are two separate endpoints:

- **Model Server (Hosted by Juxta):**  
  The initial position and raw IMU data would be sent to this server, and the device would receive delta position values in the response.  
  Endpoint: `MODEL_SERVER_URL` (configurable in `customwifi.h`)
  
- **Backend Server with DB (Hosted by PMC):**  
  The device location data would be sent to this server for UI and data storage.  
  Endpoint: `SERVER_URL` (configurable in `customwifi.h`)

---

## Data Storage

- **IMU Data:** Stored in external SPI flash (5MB) as CSV entries in a circular buffer format
- **Format:** Each entry contains multiple IMU readings: `obj1,obj2,obj3,...,objN` where each object is `accX|accY|accZ|gyrX|gyrY|gyrZ|timestamp`
- **Storage Management:** Uses read/write pointers to manage circular buffer, automatically wraps when end is reached
- **Reference Position:** Stored in NVS (internal flash) and updated after each transmission cycle
- **Failed Transmissions:** Data remains in flash for retry in next cycle (read pointer not advanced)

---

## Power Management

- **Light Sleep:** Device enters light sleep between IMU reads (after first cycle) to save power, wakes every 10ms for IMU ticker interrupt
- **Deep Sleep:** If no motion detected for 5 minutes, device enters deep sleep for 30 seconds or until motion wakes it
- **GPS Power Management:** 
  - Scenario 1: GPS stays ON
  - Scenario 2: GPS cycles ON/OFF based on "GPS on after" delay and 1-minute fix attempts
  - Scenario 3: Deep sleep cycles
  - Scenario 4: GPS stays OFF

---

## Pending Configuration Items

1. GPS read cycle time default value (for Scenario 1).  
2. Accuracy threshold for GPS position data (HDOP threshold).  
3. Precision for the Lat and Long (currently 7 decimal places).  
4. "GPS on after" delay default value (for Scenario 2).  
5. Model server URL configuration.

## Questions

1. What if the transmission to the model server over WiFi fails and partial or no data is transmitted, what do we transmit to the UI/backend with DB then?  
   **Answer:** If model server transmission fails, data remains in flash (read pointer not advanced). The device will attempt to send the same data in the next transmission cycle. No position update is sent to backend server until all IMU data is successfully processed by model server.

2. What happens if GPS fix is lost during Scenario 1?  
   **Answer:** At transmission cycle time, the device checks if GPS fix is lost. If lost, it switches to Scenario 2 with the last stored reference position. IMU data continues to be collected and will be sent to model server in next cycle.

---

## Hardware Notes

- **Platform:** ESP32-C6 Mini
- **External Flash:** 5MB SPI flash for IMU data storage (circular buffer)
- **IMU:** BMI323 (I2C interface, 100Hz sampling)
- **GPS:** AT6558 (Serial interface, configurable baud rate)
- **Power Management:** Power latch pin (GPIO 14) for power control
- **Motion Detection:** BMI323 interrupt-based motion detection for deep sleep wake-up

---

## Version History

- **v2.4:** Initial process flow documentation
- **v2.5:** Updated for ESP32-C6, refined IMU start timing based on GPS scenarios, continuous GPS fix attempts in Scenario 2, separate server endpoints

