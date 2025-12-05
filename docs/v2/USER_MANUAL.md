# Juxta Asset Tag - User Manual

## Introduction
The Juxta Asset Tag is a smart tracking device designed to monitor the location and condition of your valuable assets. It automatically tracks movement, location (GPS), and battery status, ensuring you always know where your assets are and how they are performing.

## Getting Started

### Turning On the Device
The device is designed to operate autonomously. To turn it on:
1.  Ensure the battery is charged.
2.  **Power On:** The device turns on automatically when the battery or charger is connected.
3.  **Activate Configuration Mode:** Press the **Reset Button** at any time to restart the device and enable Bluetooth configuration mode for 1 minute.
4.  The **Status LED** will light up **purple** to indicate the device is starting.

### Charging
Connect the device to a standard 5V USB charger.
*   **Charging Time:** Approximately 3-5 hours for a full charge.
*   **Battery Life:** The device is designed to last approximately **85 days** (approx. 3 months) on a single charge under optimal operation (sending updates every 15 minutes).

## LED Indicators
The device uses colored lights (LEDs) to communicate its status. Here is what they mean:

### 1. Battery Indicator (Multi-color)
Located next to the **battery charge icon**, this light tells you how much charge is left.
*   🟢 **Green:** High Battery (75% - 100%) - Good to go!
*   🟡 **Yellow to Orange:** Medium Battery (25% - 75%) - Consider charging soon.
*   🔴 **Red:** Low Battery (0% - 25%) - Charge as soon as possible.
*   🔴 **Blinking Red:** Critical Battery (< 10%) - **Charge Immediately.**

### 2. Device Status Light
Located next to the **power icon**, this light indicates if the internal sensors are working correctly.
*   🟢 **Green:** All systems normal. GPS and Motion sensors are ready.
*   🔴 **Red:** System Error. A sensor may be malfunctioning. Try restarting the device.
*   🟣 **Purple:** Starting up. Please wait.
*   🔵 **Blue (Blinking):** Bluetooth data activity.
*   🟠 **Orange (Blinking):** WiFi data activity.

### 3. Activity Lights
These small lights blink when the device is communicating.
*   **Status Light (Blue Blinking):** Blinks when the device is being configured or transferring data via Bluetooth.
*   **Status Light (Orange Blinking):** Blinks when the device is transmitting data via WiFi.
*   **Status Light (Green):** Solid green when sensors are healthy and device is ready.


## Configuration (Setting up WiFi)
To transmit data to the cloud, the device requires a Bluetooth or WiFi connection. You can easily configure the network credentials using the Juxta User Dashboard website, which connects to the device via Bluetooth.

**Note:** Configuration mode is only active for **1 minute** after the device is turned on or the **Reset Button** is pressed.

1.  **Enter Configuration Mode:** Press the Reset button on the device.
2.  **Search for Device:** On your smartphone or computer, open your WiFi configuration app (or compatible Bluetooth scanner).
3.  **Connect:** Look for a device named **"Juxta AssetTag v2.0.0"** and connect to it.
4.  **Enter Credentials:**
    *   Select the **SSID** characteristic to enter your WiFi Name.
    *   Select the **Password** characteristic to enter your WiFi Password.
5.  **Save:** The device will save these settings. The Status Light will blink blue during data transfer.

## Daily Operation
Once configured, the device works automatically. You do not need to do anything.
*   **Wake Up:** Every 15 minutes (default), the device wakes up from deep sleep.
*   **Measure:** It records its current location (GPS) and movement (IMU).
*   **Send:** It securely sends this information to the cloud via Bluetooth (primary) or WiFi (secondary).
*   **Sleep:** It goes back to deep sleep to save battery. Lights are turned off during sleep in normal mode (default), or dimmed to 10% brightness in debug mode.

**What if WiFi and Bluetooth are unavailable?**
If the device cannot connect to Bluetooth or WiFi, it will safely store the data in its internal memory (Flash) as a backup. The next time it connects to either network, it will attempt to upload the stored history.

## Troubleshooting

| Problem | Possible Cause | Solution |
| :--- | :--- | :--- |
| **Status Light is Red** | Sensor error | Press the Reset button to restart the device. If the light stays red, the device may need service. |
| **No Lights are On** | Device is sleeping or dead battery | This is normal! The device sleeps most of the time to save power. Press the Reset button to wake it up to check status, or charge the battery. |
| **Cannot Connect to Bluetooth** | Timeout | Bluetooth is only active for 1 minute after power-on. Press the Reset button to try again. |
| **Data Not Updating** | Poor Signal | Ensure the device has a clear view of the sky for GPS and is within range of the Bluetooth or WiFi network. |

## Safety & Care
*   **Water Resistance:** Keep the device dry and away from direct water submersion unless specified by your enclosure rating.
*   **Temperature:** Operate within standard temperature ranges (-10°C to 50°C).
*   **Handling:** While rugged, avoid severe impacts to protect the internal sensors.
