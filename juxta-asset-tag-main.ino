// ESP32-S3 Asset Tag - IMU & GPS data transmission

#include "imu_sensor.h"
#include "gps_sensor.h"
#include "spi_flash_handler.h"
#include "customwifi.h"
#include "time_sync.h"
#include "data_queue.h"
#include "transmission_handler.h"
#include "nvs_config.h"
#include "battery_monitor.h"
#include "battery_indicator_led.h"
#include "ble_config.h"
#include <Adafruit_NeoPixel.h>
#include <esp_system.h>
#include "esp_sleep.h"
#include <Ticker.h>

// Device ID and Version Configuration (hardcoded to save memory)
const char* DEVICE_ID = "ASSET_TAG_007";  // Change this for each device
const char* DEVICE_VERSION = "v2.0.0";   // Device firmware/hardware version

const int STATUS_LED_PIN = D1;
const int STATUS_LED_COUNT = 2; // 2 pixels: pixel 0 for device status, pixel 1 for battery status

// Status LED instance
Adafruit_NeoPixel statusLED(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

// Status LED blinking for transmission
Ticker statusLedTicker;
volatile bool statusLedBlinkState = false;
volatile uint8_t blinkR = 0, blinkG = 0, blinkB = 0;
uint8_t restoreR = 0, restoreG = 255, restoreB = 0; // Default to green

// Battery Indicator LED instance (shares statusLED NeoPixel, uses pixel 1)
BatteryIndicatorLED batteryIndicatorLED;

// Transmission handler for failed data retry logic
TransmissionHandler transmissionHandler;

// BLE start time tracking
unsigned long bleStartTime = 0;

// First BLE connection time tracking (for 1-minute connection requirement)
unsigned long firstBleConnectionTime = 0;
bool firstBleConnectionTracked = false;
bool waitingForOneMinute = false; // Flag to track if we're in 1-minute wait period
unsigned long lastCountdownPrint = 0; // Track last countdown print time
const unsigned long BLE_MIN_CONNECTION_DURATION_MS = 60000; // 1 minute

// Create sensor instances
IMUSensor imuSensor;
GPSSensor gpsSensor;
SPIFlashHandler spiFlash;

// Sensor status flags
bool imuInitialized = false;
bool gpsInitialized = false;
bool flashInitialized = false;

// Configuration: Cycle duration is now configurable via BLE and stored in NVS
// Default is 900 seconds (15 minutes) if not set via BLE
// Cycle duration is stored in seconds in NVS

// BLE advertising durations
const unsigned long BLE_ADVERTISE_FIRST_CYCLE_MS = 60000; // 60 seconds for first cycle (power-on/reset)
const unsigned long BLE_ADVERTISE_SUBSEQUENT_CYCLE_MS = 4000; // 4 seconds for subsequent cycles

// USB detection function for ESP32-S3
// Checks if USB is connected by verifying USB Serial availability
bool isUSBConnected() {
  // On ESP32-S3, USB Serial JTAG controller is active when USB is connected
  // Serial object is available when USB is connected and Serial.begin() has been called
  // This is a reliable method for ESP32-S3 to detect USB connection
  int v = digitalRead(D2);
  return v?true: false;  // Returns true if USB Serial is available (USB connected)
}

// Helper function to set pixel color and display
void setPixelAndShow(uint8_t pixel, uint8_t r, uint8_t g, uint8_t b) {
  statusLED.setPixelColor(pixel, statusLED.Color(r, g, b));
  statusLED.show();
}

void updateStatusLED() {
  // Green if all sensors (IMU, GPS, and Flash) initialized, Red if any failed
  if (imuInitialized && gpsInitialized && flashInitialized) {
    restoreR = 0;
    restoreG = 255;
    restoreB = 0;
    setPixelAndShow(0, 0, 255, 0); // Green on pixel 0
  } else {
    restoreR = 255;
    restoreG = 0;
    restoreB = 0;
    setPixelAndShow(0, 255, 0, 0); // Red on pixel 0
  }
}

// Toggle function for status LED blinking
void toggleStatusLED() {
  statusLedBlinkState = !statusLedBlinkState;
  if (statusLedBlinkState) {
    setPixelAndShow(0, blinkR, blinkG, blinkB);
  } else {
    setPixelAndShow(0, 0, 0, 0); // Off
  }
}

// Start blinking status LED with specified color
void startStatusLEDBlink(uint8_t r, uint8_t g, uint8_t b) {
  blinkR = r;
  blinkG = g;
  blinkB = b;
  statusLedBlinkState = false;
  statusLedTicker.attach_ms(20, toggleStatusLED); // 20ms = 50Hz blink
}

// Stop blinking and restore to normal status color
void stopStatusLEDBlink() {
  statusLedTicker.detach();
  setPixelAndShow(0, restoreR, restoreG, restoreB);
}

// Dim status LED based on debug mode for deep sleep
// If debug mode is 0: LED brightness = 0% (off)
// If debug mode is 1: LED brightness = 10%
void dimStatusLEDForDeepSleep() {
  uint8_t debugMode = NVSConfig::getDebugMode();
  if (debugMode == 1) {
    statusLED.setBrightness(10); // 10% of original 100
    setPixelAndShow(0, restoreR, restoreG, restoreB);
  } else {
    statusLED.setBrightness(0); // 0% - LED off
    setPixelAndShow(0, 0, 0, 0); // Apply brightness change
    setPixelAndShow(1, 0, 0, 0); // Turn off battery LED
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  statusLED.begin();  // Initialize NeoPixel first
  setPixelAndShow(0, 255, 0, 255); // Magenta/Purple on pixel 0 (unique boot color)
  delay(1000);
  pinMode(D2,INPUT);
  
  // Initialize NVS for WiFi credentials storage
  NVSConfig::initializeNVS();
  
  // Initialize External SPI Flash
  Serial.println("Initializing SPI Flash...");
  flashInitialized = spiFlash.begin();
  if (flashInitialized) {
    Serial.println("SPI Flash initialized successfully");
    Serial.print("Flash Capacity: ");
    Serial.print(spiFlash.getCapacity());
    Serial.println(" bytes");
  } else {
    Serial.println("SPI Flash initialization failed");
  }

  // Initialize transmission handler and data queue (loads from flash, calculates max size on first boot)
  Serial.println("Initializing data queue...");
  if (!transmissionHandler.begin(&spiFlash)) {
    Serial.println("Warning: Data queue initialization failed!");
  }
  
  // Check ESP reset reason to determine if this is first cycle
  esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.print("Reset reason: ");
  Serial.println(resetReason);
  
  // Determine if this is first cycle (power-on or reset button) vs subsequent (deep sleep wake-up)
  bool isFirstCycle = (resetReason == ESP_RST_POWERON || resetReason == ESP_RST_EXT);
  
  // Initialize BLE on first cycle (power-on or reset button press)
  // On subsequent cycles, BLE will be started in the loop
  if (isFirstCycle) {
    Serial.println("First cycle detected (POWER_ON or reset button press)");
    BLEConfig::begin();
    BLEConfig::setDeviceId(DEVICE_ID);
    BLEConfig::setDeviceVersion(DEVICE_VERSION);
    bleStartTime = millis();
  } else {
    Serial.println("Subsequent cycle detected (deep sleep wake-up)");
    // BLE will be started in loop() for subsequent cycles
  }
  
  // Initialize Battery Monitor ADC
  BatteryMonitor::initializeADC();
  delay(100);
  
  // Initialize Status LED first (both pixels on same pin)
  // statusLED.begin();  // Initialize GPIO first!
  statusLED.setBrightness(70);
  statusLED.show();
  updateStatusLED(); // Show red initially on pixel 0 (not initialized)
  
  // Initialize Battery Indicator LED (uses shared statusLED instance, pixel 1)
  batteryIndicatorLED.begin(&statusLED, 1);
  batteryIndicatorLED.updateBatteryLED(); // Set initial color based on battery on pixel 1
  delay(100);
  
  // Initialize IMU sensor
  Serial.println("Initializing IMU sensor...");
  imuInitialized = imuSensor.begin();
  if (imuInitialized) {
    Serial.println("IMU initialized successfully");
  } else {
    Serial.println("IMU initialization failed - continuing without IMU");
  }
  
  // Initialize GPS sensor
  Serial.println("Initializing GPS sensor...");
  gpsInitialized = gpsSensor.begin();
  if (gpsInitialized) {
    Serial.println("GPS initialized successfully");
  } else {
    Serial.println("GPS initialization failed - continuing without GPS");
  }
  
  // One-time credential setup (comment out after first upload)
  // NVSConfig::setWiFiSSID("GarageNeo");
  // NVSConfig::setWiFiPassword("G@r@ge#123");
  
  // Connect to WiFi on first cycle (power-on/reset) to attempt time sync
  // On subsequent cycles, WiFi will be connected in loop() if needed
  if (isFirstCycle) {
    CustomWiFi::connectWiFi();
    // Attempt time sync if WiFi connected (regardless of whether it was previously synced)
    attemptTimeSyncIfNeeded();
  } else {
    Serial.println("Subsequent cycle - skipping WiFi connection in setup (will connect in loop if needed)");
  }

  // Update status LED based on sensor initialization
  updateStatusLED();
  
  delay(100);
}

String createSensorCSV(IMUData imuData, GPSData gpsData) {
  static char csvBuffer[400];  // Increased buffer size to prevent overflow  
  char timestamp[20];
  
  TimeSync::getCurrentTimeString(timestamp, sizeof(timestamp));
  int batteryLevel = BatteryMonitor::getBatteryPercentage();
  
  // Get signal strength based on active connection (BLE priority, then WiFi)
  // Add safety checks to prevent crashes if BLE/WiFi not initialized
  int signalStrength = -100;  // Default: no connection
  if (BLEConfig::isEnabled() && BLEConfig::isConnected()) {
    signalStrength = BLEConfig::getRSSI();
  } else if (CustomWiFi::isConnected()) {
    signalStrength = CustomWiFi::getRSSI();
  }
  
  // CSV format: device_id,battery_level,timestamp,signal_strength,imu.accelerometer.x,imu.accelerometer.y,imu.accelerometer.z,
  //             imu.gyroscope.x,imu.gyroscope.y,imu.gyroscope.z,imu.temperature,
  //             gps.fix,gps.fixType,gps.last_recieved_on,gps.satellites,gps.latitude,gps.longitude,gps.altitude,gps.speed,gps.heading,gps.hdop
  
  snprintf(csvBuffer, sizeof(csvBuffer), 
           "%s,%d,%s,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%s,%d,%s,%d,%.7f,%.7f,%.2f,%.2f,%.2f,%.2f",
           DEVICE_ID,
           batteryLevel,
           timestamp,
           signalStrength,
           imuData.accelerometer.x, imuData.accelerometer.y, imuData.accelerometer.z,
           imuData.gyroscope.x, imuData.gyroscope.y, imuData.gyroscope.z,
           imuData.temperature,
           
           gpsData.hasValidFix ? "true" : "false",
           gpsData.fixType,
           gpsData.timestamp,
           gpsData.satellites,
           gpsData.hasValidFix ? gpsData.latitude : 0.0,
           gpsData.hasValidFix ? gpsData.longitude : 0.0,
           gpsData.hasValidFix ? gpsData.altitude : 0.0,
           gpsData.hasValidFix ? gpsData.speed : 0.0,
           gpsData.hasValidFix ? gpsData.heading : 0.0,
           gpsData.hasValidFix ? gpsData.hdop : 0.0
  );
  
  return String(csvBuffer);
}

bool sendDataWithRetryLogic(String csvData) {
  // Use transmission handler to send via WiFi and/or BLE with queue logic
  return transmissionHandler.handleDataTransmission(csvData);
}

// Helper function: Collect fresh sensor data
String collectSensorData() {
  // Send hot start command to GPS if we have last known location
  if (gpsInitialized) {
    gpsSensor.sendHotStartIfAvailable();
  }
  
  // Update sensors to get fresh data (50 samples over 1 second)
  for (int i = 0; i < 50; i++) {
    if (imuInitialized) {
      imuSensor.update();
    }
    if (gpsInitialized) {
      gpsSensor.update();
    }
    delay(20);
  }
  
  // Get data from sensors
  IMUData imuData = imuSensor.getIMUData();
  GPSData gpsData = gpsSensor.getGPSData();
  
  // Create and return CSV data
  return createSensorCSV(imuData, gpsData);
}

// Helper function: Collect sensor data and send via BLE
bool collectAndSendData(const char* messagePrefix) {
  Serial.println(messagePrefix);
  
  // Collect sensor data
  String csvData = collectSensorData();
  
  // Send data
  return sendDataWithRetryLogic(csvData);
}

// Helper function to check if time is already synced
bool isTimeSynced() {
  time_t now = time(nullptr);
  // Time is synced if it's greater than 24 hours (1970-01-02 00:00:00)
  // This indicates a valid NTP-synced time
  return (now > 24 * 3600);
}

// Attempt time sync if WiFi is connected and time hasn't been synced yet
void attemptTimeSyncIfNeeded() {
  if (CustomWiFi::isConnected()) {
    if (!isTimeSynced()) {
      Serial.println("Time not synced - attempting NTP sync...");
      bool syncSuccess = TimeSync::syncTimeNTP();
      if (syncSuccess) {
        Serial.println("Time sync successful");
      } else {
        Serial.println("Time sync failed");
      }
    } else {
      Serial.println("Time already synced, skipping NTP sync");
    }
  }
}

void loop() {
  static unsigned long lastBatteryUpdate = 0;
  static bool cycleStarted = false; // Track if cycle logic has started
  static bool earlyBleAttempted = false; // Track if we already tried early BLE transmission
  static bool earlyBleSucceeded = false; // Track if early BLE transmission was successful
  unsigned long currentTime = millis();
  static bool lastUSBState = isUSBConnected();
  bool bleMinConnectionTimeElapsed = true; // Default to true (no restriction)
  
  // Determine if this is first cycle (power-on or reset button) vs subsequent (deep sleep wake-up)
  // Only check once per cycle
  static bool isFirstCycleChecked = false;
  static bool isFirstCycle = false;
  if (!isFirstCycleChecked) {
    esp_reset_reason_t resetReason = esp_reset_reason();
    isFirstCycle = (resetReason == ESP_RST_POWERON || resetReason == ESP_RST_EXT);
    isFirstCycleChecked = true;
    Serial.print("Cycle type: ");
    Serial.println(isFirstCycle ? "First cycle (power-on/reset)" : "Subsequent cycle (deep sleep wake-up)");
  }
  
  // Determine BLE advertising duration based on cycle type
  unsigned long bleAdvertiseDuration = isFirstCycle ? BLE_ADVERTISE_FIRST_CYCLE_MS : BLE_ADVERTISE_SUBSEQUENT_CYCLE_MS;
  
  // Start BLE if not already enabled (for subsequent cycles after deep sleep)
  // On first cycle, BLE is already started in setup()
  if (!BLEConfig::isEnabled() && !isFirstCycle) {
    Serial.println("Restarting BLE for subsequent cycle...");
    BLEConfig::begin();
    BLEConfig::setDeviceId(DEVICE_ID);
    BLEConfig::setDeviceVersion(DEVICE_VERSION);
    bleStartTime = millis();
    cycleStarted = false; // Reset cycle flag
    earlyBleAttempted = false; // Reset early BLE attempt flag for new cycle
    earlyBleSucceeded = false; // Reset early BLE success flag for new cycle
    firstBleConnectionTime = 0; // Reset first connection tracking for new cycle
    firstBleConnectionTracked = false;
    waitingForOneMinute = false; // Reset 1-minute wait flag
    lastCountdownPrint = 0;
  } else if (BLEConfig::isEnabled() && bleStartTime == 0) {
    // BLE is enabled but start time not set (shouldn't happen, but safety check)
    bleStartTime = millis();
    earlyBleAttempted = false; // Reset early BLE attempt flag
    earlyBleSucceeded = false; // Reset early BLE success flag
    firstBleConnectionTime = 0; // Reset first connection tracking
    firstBleConnectionTracked = false;
    waitingForOneMinute = false; // Reset 1-minute wait flag
    lastCountdownPrint = 0;
  }
  
  // USB state monitoring
  bool currentUSBState = isUSBConnected();
  if (currentUSBState != lastUSBState) {
    if (currentUSBState) {
      batteryIndicatorLED.disable();
      Serial.println("USB connected - Battery LED disabled");
    } else {
      batteryIndicatorLED.enable();
      Serial.println("USB disconnected - Battery LED enabled");
    }
    lastUSBState = currentUSBState;
  }
  
  // Update battery indicator LED every 5 seconds (only if enabled)
  if (currentTime - lastBatteryUpdate >= 5000) {
    if (batteryIndicatorLED.isEnabled()) {
      int batteryPercent = BatteryMonitor::getBatteryPercentage();
      batteryIndicatorLED.updateBatteryLED();
      if (batteryPercent < 10) {
        batteryIndicatorLED.doubleBlink();
      }
    }
    lastBatteryUpdate = currentTime;
  }
  
  // Handle non-blocking 1-minute wait period (prevents watchdog reset)
  if (waitingForOneMinute && isFirstCycle && firstBleConnectionTracked && firstBleConnectionTime > 0) {
    // Keep BLE active and update it (allows reconnection if dropped)
    if (BLEConfig::isEnabled()) {
      BLEConfig::update();
    }
    
    // Check if 1 minute has elapsed
    bleMinConnectionTimeElapsed = (currentTime - firstBleConnectionTime >= BLE_MIN_CONNECTION_DURATION_MS);
    
    if (bleMinConnectionTimeElapsed) {
      // 1 minute requirement satisfied
      Serial.println("1-minute connection requirement satisfied");
      waitingForOneMinute = false;
      
      // Send data before deep sleep (if BLE still connected)
      if (BLEConfig::isEnabled() && BLEConfig::isConnected()) {
        Serial.println("Sending final data before deep sleep...");
        bool dataSentBeforeSleep = collectAndSendData("Final data transmission before deep sleep");
        if (dataSentBeforeSleep) {
          Serial.println("Final data sent successfully");
        } else {
          Serial.println("Final data transmission failed");
        }
      }
      
      // Wait 5 seconds to let BLE complete transmission before stopping
      Serial.println("Waiting 5 seconds for BLE to complete transmission...");
      delay(5000);
      
      // Now safe to turn off BLE
      Serial.println("Preparing to enter deep sleep");
      
      // Turn off BLE
      if (BLEConfig::isEnabled()) {
        Serial.println("Turning off BLE...");
        BLEConfig::stop(); // Now handles graceful disconnect internally
        bleStartTime = 0;
        firstBleConnectionTime = 0;
        firstBleConnectionTracked = false;
        waitingForOneMinute = false;
      }
      
      // Ensure status LED is restored
      updateStatusLED();
      
      // Prepare for deep sleep
      Serial.println("Preparing for deep sleep - saving queue state...");
      transmissionHandler.saveQueueState();
      
      // Dim LEDs based on debug mode before deep sleep
      uint8_t debugMode = NVSConfig::getDebugMode();
      if (debugMode == 1) {
        Serial.println("Debug mode enabled - Dimming LEDs to 10% for deep sleep...");
      } else {
        Serial.println("Debug mode disabled - Turning LEDs off for deep sleep...");
      }
      dimStatusLEDForDeepSleep();
      if (debugMode == 1) {
        batteryIndicatorLED.updateBatteryLED();
      }
      delay(50);
      
      // Get cycle time from NVS (default: 900 seconds = 15 minutes)
      uint32_t cycleTimeSeconds = NVSConfig::getCycleTime();
      unsigned long cycleTimeMicroseconds = (unsigned long)cycleTimeSeconds * 1000000ULL;
      
      // Always deep sleep for configured cycle time
      Serial.print("Deep sleeping for ");
      Serial.print(cycleTimeSeconds);
      Serial.println(" seconds...");
      esp_sleep_enable_timer_wakeup(cycleTimeMicroseconds);
      
      // Small delay to allow serial output to complete
      delay(100);
      
      // Enter deep sleep immediately
      esp_deep_sleep_start();
      return; // This should never be reached, but added for safety
    } else {
      // Still waiting - print countdown every second
      if (currentTime - lastCountdownPrint >= 1000) {
        bool currentlyConnected = BLEConfig::isEnabled() && BLEConfig::isConnected();
        unsigned long remainingTime = BLE_MIN_CONNECTION_DURATION_MS - (currentTime - firstBleConnectionTime);
        
        if (currentlyConnected) {
          Serial.print("Maintaining BLE connection - ");
        } else {
          Serial.print("BLE disconnected, but waiting full 1 minute - ");
        }
        Serial.print(remainingTime / 1000);
        Serial.println(" seconds remaining");
        lastCountdownPrint = currentTime;
      }
      // Return early to prevent proceeding - wait will continue in next loop iteration
      delay(100);
      return;
    }
  }
  
  // Update BLE (handles connections and processes WiFi credentials)
  // Keep BLE running during the full advertising period
  if (BLEConfig::isEnabled()) {
    BLEConfig::update();
    
    // Track first BLE connection time in reset cycle (only on first cycle)
    if (isFirstCycle && !firstBleConnectionTracked && BLEConfig::isConnected()) {
      firstBleConnectionTime = currentTime;
      firstBleConnectionTracked = true;
      Serial.println("First BLE connection detected - will maintain connection for 1 minute");
    }
  }
  
  // Check if BLE advertising period has elapsed
  bool bleAdvertiseTimeElapsed = (bleStartTime > 0 && (currentTime - bleStartTime >= bleAdvertiseDuration));
  
  // Check if 1 minute has passed since first BLE connection (only applies to first cycle)
  // This is used for cycle logic gate, not for the wait loop
  if (isFirstCycle && firstBleConnectionTracked && firstBleConnectionTime > 0 && !waitingForOneMinute) {
    bleMinConnectionTimeElapsed = (currentTime - firstBleConnectionTime >= BLE_MIN_CONNECTION_DURATION_MS);
  }
  
  // EARLY EXIT: If BLE is connected and we haven't started the cycle yet, send data after 5 seconds
  // Skip if we're already waiting for 1-minute requirement
  if (!cycleStarted && !earlyBleAttempted && !waitingForOneMinute && BLEConfig::isEnabled() && BLEConfig::isConnected()) {
    // Check if 5 seconds have passed since connection (hardcoded delay)
    unsigned long timeSinceConnection = currentTime - firstBleConnectionTime;
    
    if (timeSinceConnection >= 5000) {
      // 5 second delay has passed - send data now (FIRST transmission)
      earlyBleAttempted = true; // Mark that we've attempted early BLE transmission
      
      // Collect and send sensor data
      Serial.println("BLE connected - sending data after 5 second delay (FIRST transmission)...");
      bool dataSent = collectAndSendData("Data transmission 5 seconds after connection");
    
      if (dataSent) {
        Serial.println("Data sent successfully via BLE (FIRST transmission)");
        earlyBleSucceeded = true; // Mark that early BLE transmission succeeded
        cycleStarted = true; // Mark cycle as started immediately to prevent cycle logic from running
      
      // On first cycle, COMPULSORY: wait full 1 minute after first connection (even if connection drops)
      if (isFirstCycle && firstBleConnectionTracked && firstBleConnectionTime > 0) {
        // Start non-blocking 1-minute wait period (prevents watchdog reset)
        waitingForOneMinute = true;
        lastCountdownPrint = currentTime;
        Serial.println("First cycle: Compulsory 1-minute BLE connection period started");
        // Don't block here - let loop() handle the wait
        return; // Return to loop() to prevent watchdog reset
      } else {
        // Wait 3 seconds before disconnecting BLE (for subsequent cycles or if not first connection)
        Serial.println("Waiting 3 seconds before disconnecting BLE...");
        delay(3000);
      }
      
      // Send data before deep sleep (if BLE still connected)
      if (BLEConfig::isEnabled() && BLEConfig::isConnected()) {
        Serial.println("Sending final data before deep sleep...");
        bool dataSentBeforeSleep = collectAndSendData("Final data transmission before deep sleep");
        if (dataSentBeforeSleep) {
          Serial.println("Final data sent successfully");
        } else {
          Serial.println("Final data transmission failed");
        }
      }
      
      // Wait 5 seconds to let BLE complete transmission before stopping
      Serial.println("Waiting 5 seconds for BLE to complete transmission...");
      delay(5000);
      
      // Now safe to turn off BLE
      Serial.println("Preparing to enter deep sleep");
      
      // Turn off BLE (only if 1-minute wait completed)
      if (!waitingForOneMinute) {
        if (BLEConfig::isEnabled()) {
          Serial.println("Turning off BLE...");
          BLEConfig::stop(); // Now handles graceful disconnect internally
          bleStartTime = 0;
          firstBleConnectionTime = 0;
          firstBleConnectionTracked = false;
          waitingForOneMinute = false;
        }
      } else {
        // Still waiting for 1 minute - return to loop() to continue waiting
        return;
      }
      
      // Ensure status LED is restored
      updateStatusLED();
      
      // Prepare for deep sleep
      Serial.println("Preparing for deep sleep - saving queue state...");
      transmissionHandler.saveQueueState();
      
      // Dim LEDs based on debug mode before deep sleep
      uint8_t debugMode = NVSConfig::getDebugMode();
      if (debugMode == 1) {
        Serial.println("Debug mode enabled - Dimming LEDs to 10% for deep sleep...");
      } else {
        Serial.println("Debug mode disabled - Turning LEDs off for deep sleep...");
      }
      dimStatusLEDForDeepSleep();
      if (debugMode == 1) {
        batteryIndicatorLED.updateBatteryLED();
      }
      delay(50);
      
      // Get cycle time from NVS (default: 900 seconds = 15 minutes)
      uint32_t cycleTimeSeconds = NVSConfig::getCycleTime();
      unsigned long cycleTimeMicroseconds = (unsigned long)cycleTimeSeconds * 1000000ULL;
      
      // Always deep sleep for configured cycle time
      Serial.print("Deep sleeping for ");
      Serial.print(cycleTimeSeconds);
      Serial.println(" seconds...");
      esp_sleep_enable_timer_wakeup(cycleTimeMicroseconds);
      
      // Small delay to allow serial output to complete
      delay(100);
      
      // Enter deep sleep immediately
      esp_deep_sleep_start();
      return; // This should never be reached, but added for safety
    } else {
      Serial.println("Early BLE transmission failed - will wait for advertising period or try WiFi");
        earlyBleSucceeded = false; // Mark that early BLE transmission failed
      // Reset early attempt flag so cycle logic can try again if still connected
      earlyBleAttempted = false;
      // Continue to normal cycle logic below
      }
    } else {
      // Delay hasn't passed yet - wait for next loop iteration
      // Don't set earlyBleAttempted yet, will retry when delay passes
    }
  }
  
  // CYCLE LOGIC: Execute once per cycle AFTER BLE advertising period completes
  // Only turn off BLE and switch to WiFi after the full advertising time has elapsed
  // Note: For first cycle, we'll wait for 1-minute connection requirement before disconnecting BLE
  // On first cycle, also ensure 1-minute connection requirement is met before starting cycle logic
  bool canStartCycle = bleAdvertiseTimeElapsed;
  if (isFirstCycle && firstBleConnectionTracked && firstBleConnectionTime > 0) {
    // On first cycle, don't start cycle logic until 1 minute has passed since first connection
    canStartCycle = canStartCycle && bleMinConnectionTimeElapsed;
  }
  
  if (canStartCycle && !cycleStarted) {
    cycleStarted = true; // Mark cycle as started to prevent re-execution
    Serial.println("BLE advertising period elapsed, executing cycle logic...");
    
    // Now that advertising period is complete, check BLE connection status
    bool bleConnected = BLEConfig::isEnabled() && BLEConfig::isConnected();
    
    // Collect sensor data
    String csvData = collectSensorData();
    
    bool dataSent = false;
    bool shouldEnterStorageMode = false;
    
    // STEP 1: Try BLE transmission if connected (after advertising period completed)
    // Only try if we haven't already attempted early transmission, or if early attempt failed
    // If early attempt succeeded, skip BLE and WiFi (data already sent)
    if (earlyBleSucceeded) {
      Serial.println("Early BLE transmission already succeeded - skipping retry and WiFi");
      dataSent = true; // Mark as sent to prevent WiFi attempt
    } else if (bleConnected && !earlyBleAttempted) {
      Serial.println("BLE connected - attempting data transmission...");
      dataSent = sendDataWithRetryLogic(csvData);
      
      if (dataSent) {
        Serial.println("Data sent successfully via BLE");
      } else {
        Serial.println("Data transmission failed via BLE");
      }
    } else if (bleConnected && earlyBleAttempted && !earlyBleSucceeded) {
      // Early attempt was made but failed - retry BLE transmission
      Serial.println("BLE still connected - retrying BLE transmission after early attempt failed...");
      dataSent = sendDataWithRetryLogic(csvData);
      
      if (dataSent) {
        Serial.println("Data sent successfully via BLE (retry)");
    } else {
        Serial.println("Data transmission failed via BLE (retry)");
      }
    } else if (bleConnected && earlyBleAttempted && earlyBleSucceeded) {
      // This shouldn't happen if cycle logic is properly gated, but safety check
      Serial.println("BLE connected and early transmission succeeded - data already sent");
      dataSent = true;
    }
    
    // On first cycle, COMPULSORY: wait full 1 minute after first connection (even if connection drops)
    if (isFirstCycle && firstBleConnectionTracked && firstBleConnectionTime > 0) {
      // Start non-blocking 1-minute wait period (prevents watchdog reset)
      waitingForOneMinute = true;
      lastCountdownPrint = currentTime;
      Serial.println("First cycle: Compulsory 1-minute BLE connection period started");
      // Don't block here - let loop() handle the wait
      return; // Return to loop() to prevent watchdog reset
    } else {
      // Wait 3 seconds before disconnecting BLE (for subsequent cycles or if not first connection)
      Serial.println("Waiting 3 seconds before disconnecting BLE...");
      delay(3000);
    }
    
    // Send data before deep sleep (if BLE still connected)
    if (BLEConfig::isEnabled() && BLEConfig::isConnected()) {
      Serial.println("Sending final data before deep sleep...");
      bool dataSentBeforeSleep = collectAndSendData("Final data transmission before deep sleep");
      if (dataSentBeforeSleep) {
        Serial.println("Final data sent successfully");
      } else {
        Serial.println("Final data transmission failed");
      }
    }
    
    // Wait 5 seconds to let BLE complete transmission before stopping
    Serial.println("Waiting 5 seconds for BLE to complete transmission...");
    delay(5000);
    
    // Turn off BLE now that advertising period is complete and transmission attempted
    // (only if 1-minute wait completed)
    if (!waitingForOneMinute) {
      if (BLEConfig::isEnabled()) {
        Serial.println("BLE advertising period complete - turning off BLE...");
        BLEConfig::stop(); // Now handles graceful disconnect internally
        bleStartTime = 0;
        firstBleConnectionTime = 0;
        firstBleConnectionTracked = false;
        waitingForOneMinute = false;
      }
    } else {
      // Still waiting for 1 minute - return to loop() to continue waiting
      return;
    }
    
    if (dataSent) {
      // Data sent successfully via BLE, prepare for deep sleep
      Serial.println("BLE transmission successful - preparing for deep sleep");
    } else {
      // BLE transmission failed or not connected, will try WiFi next
      Serial.println("BLE transmission failed or not connected - will try WiFi");
    }
    
    // STEP 2: Try WiFi if BLE didn't send data successfully
    if (!dataSent) {
      // Check if WiFi credentials exist
      String ssid = NVSConfig::getWiFiSSID();
      String password = NVSConfig::getWiFiPassword();
      
      if (ssid.length() > 0 && password.length() > 0) {
        Serial.println("WiFi credentials found - attempting WiFi connection...");
        bool wifiConnected = CustomWiFi::connectWiFi();
        
        if (wifiConnected) {
          Serial.println("WiFi connected - attempting data transmission...");
          delay(1000); // Wait for WiFi to stabilize
          
          // Attempt time sync if not already synced
          attemptTimeSyncIfNeeded();
          
          dataSent = sendDataWithRetryLogic(csvData);
          
          if (dataSent) {
            Serial.println("Data sent successfully via WiFi");
          } else {
            Serial.println("Data transmission failed via WiFi");
          }
          
          // Turn off WiFi before proceeding
          Serial.println("Turning off WiFi...");
          CustomWiFi::disconnectWiFi();
          
          if (!dataSent) {
            // WiFi failed, enter storage mode
            shouldEnterStorageMode = true;
          }
        } else {
          Serial.println("WiFi connection failed - entering storage mode");
          shouldEnterStorageMode = true;
        }
      } else {
        Serial.println("WiFi credentials not found - entering storage mode");
        shouldEnterStorageMode = true;
      }
    }
    
    // STEP 3: Storage mode (if BLE and WiFi both failed)
    if (shouldEnterStorageMode) {
      Serial.println("Entering storage mode - queuing data to flash...");
      // Data is already queued by sendDataWithRetryLogic if transmission failed
      // Just ensure it's saved
      transmissionHandler.saveQueueState();
    }
    
    // Ensure status LED is restored
    updateStatusLED();
    
    // Send data before deep sleep (if BLE still connected)
    if (BLEConfig::isEnabled() && BLEConfig::isConnected()) {
      Serial.println("Sending final data before deep sleep...");
      bool dataSentBeforeSleep = collectAndSendData("Final data transmission before deep sleep");
      if (dataSentBeforeSleep) {
        Serial.println("Final data sent successfully");
      } else {
        Serial.println("Final data transmission failed");
      }
    }
    
    // Wait 5 seconds to let BLE complete transmission before stopping
    Serial.println("Waiting 5 seconds for BLE to complete transmission...");
    delay(5000);
    
    // Prepare for deep sleep
    Serial.println("Preparing for deep sleep - saving queue state...");
    transmissionHandler.saveQueueState();
    
    // Dim LEDs based on debug mode before deep sleep
    uint8_t debugMode = NVSConfig::getDebugMode();
    if (debugMode == 1) {
      Serial.println("Debug mode enabled - Dimming LEDs to 10% for deep sleep...");
    } else {
      Serial.println("Debug mode disabled - Turning LEDs off for deep sleep...");
    }
    dimStatusLEDForDeepSleep();
    if (debugMode == 1) {
      batteryIndicatorLED.updateBatteryLED();
    }
    delay(50);
    
    // Get cycle time from NVS (default: 900 seconds = 15 minutes)
    uint32_t cycleTimeSeconds = NVSConfig::getCycleTime();
    unsigned long cycleTimeMicroseconds = (unsigned long)cycleTimeSeconds * 1000000ULL;
    
    // Always deep sleep for configured cycle time
    Serial.print("Deep sleeping for ");
    Serial.print(cycleTimeSeconds);
    Serial.println(" seconds...");
    esp_sleep_enable_timer_wakeup(cycleTimeMicroseconds);
    
    // Small delay to allow serial output to complete
    delay(100);
    
    // Enter deep sleep
    esp_deep_sleep_start();
  }
  
  // If cycle hasn't started yet, continue normal operation (BLE advertising, battery updates, etc.)
  delay(100);
}