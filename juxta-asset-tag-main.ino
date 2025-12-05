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

// Create sensor instances
IMUSensor imuSensor;
GPSSensor gpsSensor;
SPIFlashHandler spiFlash;

// Sensor status flags
bool imuInitialized = false;
bool gpsInitialized = false;
bool flashInitialized = false;

// Configuration: Cycle duration in minutes
const unsigned long CYCLE_DURATION_MINUTES = 15; // 15 minutes per cycle
const unsigned long CYCLE_DURATION_MICROSECONDS = CYCLE_DURATION_MINUTES * 60 * 1000000ULL;

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
  unsigned long currentTime = millis();
  static bool lastUSBState = isUSBConnected();
  
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
  } else if (BLEConfig::isEnabled() && bleStartTime == 0) {
    // BLE is enabled but start time not set (shouldn't happen, but safety check)
    bleStartTime = millis();
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
  
  // Update BLE (handles connections and processes WiFi credentials)
  // Keep BLE running during the full advertising period
  if (BLEConfig::isEnabled()) {
    BLEConfig::update();
  }
  
  // Check if BLE advertising period has elapsed
  bool bleAdvertiseTimeElapsed = (bleStartTime > 0 && (currentTime - bleStartTime >= bleAdvertiseDuration));
  
  // CYCLE LOGIC: Execute once per cycle AFTER BLE advertising period completes
  // Only turn off BLE and switch to WiFi after the full advertising time has elapsed
  if (bleAdvertiseTimeElapsed && !cycleStarted) {
    cycleStarted = true; // Mark cycle as started to prevent re-execution
    Serial.println("BLE advertising period elapsed, executing cycle logic...");
    
    // Now that advertising period is complete, check BLE connection status
    bool bleConnected = BLEConfig::isEnabled() && BLEConfig::isConnected();
    
    // Collect sensor data
    // Send hot start command to GPS if we have last known location
    if (gpsInitialized) {
      gpsSensor.sendHotStartIfAvailable();
    }
    
    // Update sensors to get fresh data
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
    String csvData = createSensorCSV(imuData, gpsData);
    
    bool dataSent = false;
    bool shouldEnterStorageMode = false;
    
    // STEP 1: Try BLE transmission if connected (after advertising period completed)
    if (bleConnected) {
      Serial.println("BLE connected - attempting data transmission...");
      dataSent = sendDataWithRetryLogic(csvData);
      
      if (dataSent) {
        Serial.println("Data sent successfully via BLE");
      } else {
        Serial.println("Data transmission failed via BLE");
      }
    }
    
    // Turn off BLE now that advertising period is complete and transmission attempted
    if (BLEConfig::isEnabled()) {
      Serial.println("BLE advertising period complete - turning off BLE...");
      BLEConfig::stop();
      bleStartTime = 0;
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
    
    // Always deep sleep for 15 minutes
    Serial.print("Deep sleeping for ");
    Serial.print(CYCLE_DURATION_MINUTES);
    Serial.println(" minutes...");
    esp_sleep_enable_timer_wakeup(CYCLE_DURATION_MICROSECONDS);
    
    // Small delay to allow serial output to complete
    delay(100);
    
    // Enter deep sleep
    esp_deep_sleep_start();
  }
  
  // If cycle hasn't started yet, continue normal operation (BLE advertising, battery updates, etc.)
  delay(100);
}