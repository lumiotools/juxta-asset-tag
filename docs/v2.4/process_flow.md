# Call Summary – Device Process Flow

Final firmware logic discussed with all members of the Juxta team during the scheduled call on 18/12/2025.

---

## Process Flow

The device starts up (powers on) with both **IMU and GPS powered ON**.

The GPS is initialized and attempts to acquire a fix for the next **2 minutes**. From this point, there can be **four scenarios**.

---

## 1\. GPS Finds a High Accuracy Fix

a. When the GPS finds a **high accuracy fix**, the device stores the GPS location in a reference position variable and starts the IMU read cycle.  
b. The IMU data is read and stored at a 100 Hz interval (10 ms) *(interval provided by the Juxta team)*.  
c. The GPS data is read and stored at a different cycle time configured by the user in the UI *(default value to be provided by the Juxta team)*.  
d. The device would keep recording the data and send it at transmission cycle intervals to the UI/backend server with DB *(hosted by PMC)*.  
e. The **high accuracy GPS value** would be sent to the DB instead of processed latitude and longitude.  
f. This would continue till the device has high GPS accuracy and the accuracy does not fall below the decided threshold *(threshold to be provided by the Juxta team)*.  
g. If the GPS fix is lost during this scenario, the device would switch to **Scenario 2** with the last stored position in the reference position variable as the reference point.  
**Note:**  
The reference position variable (containing the GPS location when IMU started) is updated after every transmission cycle.  
Threshold to be configurable on the UI

---

## 2\. GPS Finds a Low Accuracy Fix

a. The device keeps trying to find a high accuracy fix for 2 minutes, but at the end of 2 minutes has not been able to find one.  
b. In this scenario, the device would store the last known GPS position (low accuracy) in the reference position variable and start recording the IMU data at a 100 Hz interval (10 ms) *(interval provided by the Juxta team)*.  
c. The **GPS power turns OFF** to conserve battery.  
d. The IMU data, along with the reference position stored in the variable, would be sent to the model server (hosted by Juxta) in the format:  
(lat, long), imuObj1, imuObj2, …  
e. The model server would then respond with delta position (change in the provided latitude and longitude), which would be processed in the device and saved as the new position.  
f. This computed latitude and longitude would be sent to the UI/backend server with DB.  
g. **After the transmission cycle has completed**, the GPS power turns back ON and attempts to find a fix again, while the IMU continues reading and storing values.  
h. If a **high accuracy fix is found**, the device would follow **Scenario 1** logic.  
i. If a **low accuracy fix is found** or **no fix is found**, the device would continue with **Scenario 2** logic.  
**Note:**  
The reference position variable is updated after every transmission cycle. If the GPS fix is lost during **Scenario 1**, the device would switch to **Scenario 2** with the last stored position in the reference position variable as the reference point.

---

## 3\. GPS Could Not Find a Fix

a. If the GPS is not able to find a fix within 2 minutes, the device would go into deep sleep (full power-saving mode) for the next 30 seconds *(time provided by the Juxta team)*, wake up, and try to find a fix again.  
b. This process would continue till the GPS finds either a high accuracy fix or a low accuracy fix, after which the device would follow **Scenario 1** or **Scenario 2** based on the type of fix.  
**Note:**  
The device would not be sending any data till it receives a fix *(as mentioned by the Juxta team)*.

---

## 4\. Initial Position Is Input from the UI

a. There would be an option for the user to provide the initial latitude and longitude position from the Dashboard in Configure Mode.  
b. In this scenario, the device would consider this as the initial location and continue the process according to **Scenario 2.b**.  
c. If the device finds a fix it will switch to scenario 1 or 2\.

---

## Transmission Logic

When the device detects that the transmission cycle time/condition is hit, it performs the following actions:

### Batch Processing and Transmission

a. The IMU data is read and the device checks how many IMU objects are stored.  
b. The stored IMU data is divided into batches based on available free RAM to optimize memory usage.  
c. Each batch is transmitted in separate POST requests to the model server.  
d. Each batch request is prefixed with the current latitude and longitude position in the format:  
(lat, long), imuObj1, imuObj2, …  
e. The first batch is sent to the model server, and a delta position (change in latitude and longitude) is received in the response.  
f. Using the last known position and the received delta position, the device calculates the current position.  
g. The next batch request is sent using this newly calculated processed position as the prefix.  
h. This process continues iteratively until all IMU objects are transmitted successfully.

### Position Update After Transmission

After all IMU objects are transmitted successfully:

- **In Scenario 1:** The last position (reference position variable) is updated to the current position from GPS (high accuracy value).  
- **In Scenario 2:** The last position (reference position variable) is updated to the processed value from the last received delta position response.

### GPS Fix Finding in Scenario 2

After the transmission cycle has completed in Scenario 2:

a. The GPS power turns back ON.  
b. The GPS attempts to find a fix for the next 1 minute while the IMU continues reading and storing values in the background.  
c. If a **high accuracy fix is found**, the device switches to **Scenario 1** logic.  
d. If a **low accuracy fix is found** or **no fix is found**, the device continues with **Scenario 2** logic.  
e. The GPS power turns OFF again after the fix attempt to conserve battery.

### Communication Protocols

- **Model Server (Hosted by Juxta):**  
  Data transmission to the model server (initial position and raw IMU data) is **only sent through WiFi** due to the large payload size of IMU data.  
    
- **Backend Server with DB (Hosted by PMC):**  
  Data for saving in the database (latitude, longitude values with device details) can be sent through **both BLE and WiFi** due to its small size.  
    
- **Configuration Mode:**  
  **BLE is used for configuration mode** to allow device setup and initial position input from the Dashboard.  
  When the device is connected, the Dashboard will display a countdown timer indicating the remaining duration for the configuration mode.  
  The Configuration mode can be started again after countdown ends by pressing the connect button on the device.

---

## Server Endpoints

There would be two separate endpoints:

- **Model Server (Hosted by Juxta):**  
  The initial position and raw IMU data would be sent to this server, and the device would receive delta position values in the response.  
- **Backend Server with DB (Hosted by PMC):**  
  The device location data would be sent to this server for UI and data storage.

---

## Pending Configuration Items

1. GPS read cycle time default value.  
2. Accuracy threshold for GPS position data.  
3. Precision for the Lat and Long

## Questions

1. What if the transmission to the model server over wifi fails and partial or no data is transmitted, what do we transmit to the ui/backend with db then?
