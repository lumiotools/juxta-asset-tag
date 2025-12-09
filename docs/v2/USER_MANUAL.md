# Juxta Asset Tag \- User Manual

## Introduction

The Juxta Asset Tag is a smart tracking device designed to monitor the location and condition of your valuable assets. It automatically tracks movement, location (GPS), and battery status, ensuring you always know where your assets are and how they are performing.

## Getting Started

### Turning On the Device

The device is designed to operate autonomously. To turn it on:

1. Ensure the battery is charged.  
2. **Power On:** The device turns on automatically when the battery or charger is connected.  
3. **Activate Configuration Mode:** Press the **Connect Button** at any time to restart the device and enable Bluetooth configuration mode for 1 minute.  
4. The **Status LED** will light up **purple** to indicate the device is starting.

### Charging

Connect the device to a standard 5V USB charger.

* **Charging Time:** Approximately 3-5 hours for a full charge.  
* **Battery Life:** The device is designed to last approximately **85 days** (approx. 3 months) on a single charge under optimal operation (sending updates every 15 minutes).

## LED Indicators

The device uses colored lights (LEDs) to communicate its status. Here is what they mean:

### 1\. Battery Indicator (Multi-color)

Located next to the **battery charge icon**, this light tells you how much charge is left.

* 🟢 **Green:** High Battery (75% \- 100%) \- Good to go\!  
* 🟡 **Yellow to Orange:** Medium Battery (25% \- 75%) \- Consider charging soon.  
* 🔴 **Red:** Low Battery (0% \- 25%) \- Charge as soon as possible.  
* 🔴 **Blinking Red:** Critical Battery (\< 10%) \- **Charge Immediately.**

### 2\. Device Status Light

Located next to the **power icon**, this light indicates if the internal sensors are working correctly.

* 🟢 **Green:** All systems normal. GPS and Motion sensors are ready.  
* 🔴 **Red:** System Error. A sensor may be malfunctioning. Try restarting the device.  
* 🟣 **Purple:** Starting up. Please wait.  
* 🔵 **Blue (Blinking):** Bluetooth data activity.  
* ⚪ **White (Blinking):** WiFi data activity.  
* 🟤 **Brown (Blinking):** Saving to External Flash Activity

### 3\. Activity Lights

The Status Light (same as Device Status Light above) blinks in different colors to indicate communication activity:

* **Blue (Blinking):** The device is being configured or transferring data via Bluetooth.  
* **White (Blinking):** The device is transmitting data via WiFi.  
* **Brown (Blinking):** The device is saving data to external flash memory.  
* **Green (Solid):** Sensors are healthy and device is ready (normal operation).

## Configuration (Setting up WiFi)

To transmit data to the cloud, the device requires a Bluetooth or WiFi connection. You can easily configure the device using the Juxta User Dashboard website, which connects to the device via Bluetooth.

### Connecting Through the Dashboard UI

1. **Enter Configuration Mode:** Press the **Connect Button** on the device to activate Bluetooth mode. The Status LED will light up **purple** during startup.

2. **Open Dashboard:** On your computer or smartphone, open the Juxta User Dashboard website.

3. **Enable Chrome Feature (Required for Web UI):** 
   > **⚠️ IMPORTANT:** If using the web-based UI, you must enable the Chrome feature flag: `chrome://flags/#enable-web-bluetooth-new-permissions-backend`. This is **required** for the web-based UI to auto-connect to the Asset Tag. The functionality will **not work** without enabling this feature.

4. **Connect to Device:**
   * Click the **Connect** button in the dashboard.
   * The dashboard will scan and display available devices nearby.
   * Select your device (named **"Juxta {DeviceID} v2.0.0"**).
   * Pair with the selected device.

5. **Connection Duration:** The device will stay connected for **1 minute** after pairing. During this time, you can configure settings and view data.

6. **Auto Reconnection:** The device will automatically disconnect and reconnect through the portal when it enters deep sleep or wakes up. This allows seamless data transmission without manual intervention.

### Configurable Parameters

Once connected, you can configure the following parameters:

* **Debug Mode:** When enabled, LEDs are dimmed to 10% brightness during deep sleep instead of being turned off. This helps monitor device status during sleep.
* **WiFi Credentials:** 
  * Click the **WiFi Configuration** button.
  * Click the **Update** button.
  * Enter your **WiFi SSID** (network name).
  * Enter your **WiFi Password**.
  * Click on **Update** button to save the credentials to the device.
* **Cycle Time:** Set the deep sleep duration (default: 15 minutes).
  > **Note:** Cycle time is the duration the device spends in deep sleep. The device wakes up from deep sleep every X seconds (where X is the cycle time), completes its processes (device status check, sensor data collection, GPS reading, data transmission), and then goes back to sleep for X seconds. The processing time is **not included** in the cycle time. This means the device does **not** transmit every X seconds — it transmits after waking up, and the actual transmission interval includes both the sleep time and processing time.

### Viewing Data

* **Last Received Data:** After connecting via BLE, you can view the last received sensor data directly on the dashboard screen. This includes GPS location, IMU data, battery status, and timestamp.

* **Device History:** To view detailed device pairing history and data logs:
  * Navigate to the **My Devices** page in the dashboard.
  * Select your Asset Tag device.
  * View the complete history of data transmissions, connection events, and device status.

**Note:** Configuration mode is active for **1 minute** after the device is first turned on or the **Connect Button** is pressed. On subsequent wake-ups, Bluetooth is available for **10 seconds**.

## Daily Operation

Once configured, the device works automatically. You do not need to do anything.

* **Wake Up:** Every 15 minutes (default), the device wakes up from deep sleep.  
* **Measure:** It records its current location (GPS) and movement (IMU).  
* **Send:** It securely sends this information to the cloud via Bluetooth (primary) or WiFi (secondary).  
* **Sleep:** It goes back to deep sleep to save battery. Lights are turned off during sleep in normal mode (default), or dimmed to 10% brightness in debug mode.

**What if WiFi and Bluetooth are unavailable?** If the device cannot connect to Bluetooth or WiFi, it will safely store the data in its internal memory (Flash) as a backup. The next time it connects to either network, it will attempt to upload the stored history.

## Troubleshooting

| Problem | Possible Cause | Solution |
| :---- | :---- | :---- |
| **Status Light is Red** | Sensor error | Press the Connect button to restart the device. If the light stays red, the device may need service. |
| **No Lights are On** | Device is sleeping or dead battery | This is normal\! The device sleeps most of the time to save power. Press the Connect button to wake it up to check status, or charge the battery. |
| **Cannot Connect to Bluetooth** | Timeout | Bluetooth is active for 1 minute after power-on/reset. Press the Connect button to try again. |
| **Data Not Updating** | Poor Signal | Ensure the device has a clear view of the sky for GPS and is within range of the Bluetooth or WiFi network. |

## Safety & Care

* **Water Resistance:** Keep the device dry and away from direct water submersion unless specified by your enclosure rating.  
* **Temperature:** Operate within standard temperature ranges (-10°C to 50°C).  
* **Handling:** While rugged, avoid severe impacts to protect the internal sensors.
