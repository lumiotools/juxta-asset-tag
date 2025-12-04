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

// Cycle configuration
const unsigned long CYCLE_DURATION = 15 * 60 * 1000000ULL; // 15 minutes in microseconds
const unsigned long BLE_ADV_TIMEOUT_FIRST = 60 * 1000; // 60 seconds for first cycle (milliseconds)
const unsigned long BLE_ADV_TIMEOUT_NORMAL = 4 * 1000; // 4 seconds for normal cycles (milliseconds)

// Cycle tracking
static bool isFirstCycle = true;

// Create sensor instances
IMUSensor imuSensor;
GPSSensor gpsSensor;
SPIFlashHandler spiFlash;

// Sensor status flags
bool imuInitialized = false;
bool gpsInitialized = false;
bool flashInitialized = false;

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
  setPixelAndShow(0, 128, 0, 255); // Purple on pixel 0
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
  
  // Check ESP reset reason
  esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.print("Reset reason: ");
  Serial.println(resetReason);
  
  // Determine if this is the first cycle (POWER_ON or reset button press)
  if (resetReason == ESP_RST_POWERON || resetReason == ESP_RST_EXT) {
    isFirstCycle = true;
    Serial.println("First cycle detected (POWER_ON or reset button press)");
  } else {
    isFirstCycle = false;
    Serial.println("Normal cycle (wake from deep sleep)");
  }
  
  // BLE and WiFi will be started in loop() for each cycle
  
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
  
  // WiFi and time sync will be handled in loop() during first cycle if needed
  // Update status LED based on sensor initialization
  updateStatusLED();
  
  delay(100);
}

String createSensorCSV(IMUData imuData, GPSData gpsData, int signalStrength) {
  static char csvBuffer[380];
  char timestamp[20];
  
  TimeSync::getCurrentTimeString(timestamp, sizeof(timestamp));
  int batteryLevel = BatteryMonitor::getBatteryPercentage();
  
  // CSV format: device_id,battery_level,timestamp,imu.accelerometer.x,imu.accelerometer.y,imu.accelerometer.z,
  //             imu.gyroscope.x,imu.gyroscope.y,imu.gyroscope.z,imu.temperature,
  //             gps.fix,gps.fixType,gps.lastFixTimestamp,gps.satellites,gps.latitude,gps.longitude,gps.altitude,gps.speed,gps.heading,gps.hdop,signal_strength
  
  snprintf(csvBuffer, sizeof(csvBuffer), 
           "%s,%d,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%s,%d,%s,%d,%.7f,%.7f,%.2f,%.2f,%.2f,%.2f,%d",
           DEVICE_ID,
           batteryLevel,
           timestamp,
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
           gpsData.hasValidFix ? gpsData.hdop : 0.0,
           signalStrength
  );
  
  return String(csvBuffer);
}

bool sendDataWithRetryLogic(String csvData) {
  // Use transmission handler to send via WiFi and/or BLE with queue logic
  return transmissionHandler.handleDataTransmission(csvData);
}

void loop() {
  // Single-cycle execution: runs once per wake from deep sleep
  unsigned long cycleStartTime = millis();
  bool dataSent = false;
  
  Serial.println("=== Starting new cycle ===");
  Serial.print("Cycle type: ");
  Serial.println(isFirstCycle ? "FIRST (60s BLE timeout)" : "NORMAL (4s BLE timeout)");
  
  // ============================================
  // STEP 1: Collect sensor data
  // ============================================
  Serial.println("Collecting sensor data...");
  
  // Save last known GPS location to GPS module flash before updating GPS
  // This pushes the location to the GPS module's internal flash memory
  if (gpsInitialized) {
    Serial.println("Saving last known GPS location to GPS module flash...");
    gpsSensor.saveLocationToGPSModule();
  }
  
  // Update sensors to get fresh data (increased to allow all IMU sensor types to report)
  // Only update if sensors are initialized to avoid crashes
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
  
  // Determine signal strength based on available connection
  // Priority: BLE if connected, then WiFi if connected, otherwise -100 (invalid)
  int signalStrength = -100; // Default: invalid signal
  if (BLEConfig::isConnected()) {
    signalStrength = BLEConfig::getRSSI();
    Serial.print("BLE RSSI: ");
    Serial.println(signalStrength);
  } else if (CustomWiFi::isConnected()) {
    signalStrength = CustomWiFi::getRSSI();
    Serial.print("WiFi RSSI: ");
    Serial.println(signalStrength);
  }
  
  // Create CSV data with signal strength
  String csvData = createSensorCSV(imuData, gpsData, signalStrength);
  Serial.println("Sensor data collected");
  
  // ============================================
  // STEP 2: Try BLE transmission first
  // ============================================
  Serial.println("Attempting BLE transmission...");
  
  // Determine BLE advertising timeout
  unsigned long bleTimeout = isFirstCycle ? BLE_ADV_TIMEOUT_FIRST : BLE_ADV_TIMEOUT_NORMAL;
  Serial.print("BLE advertising timeout: ");
  Serial.print(bleTimeout / 1000);
  Serial.println(" seconds");
  
  // Start BLE
  BLEConfig::begin();
  BLEConfig::setDeviceId(DEVICE_ID);
  BLEConfig::setDeviceVersion(DEVICE_VERSION);
  
  unsigned long bleStartTime = millis();
  bool bleConnected = false;
  bool bleDataSent = false;
  
  // Wait for BLE connection with timeout
  while ((millis() - bleStartTime) < bleTimeout) {
    BLEConfig::update(); // Handle BLE connections and process received data
    
    if (BLEConfig::isConnected()) {
      bleConnected = true;
      Serial.println("BLE connected! Sending data...");
      
      // Update signal strength for BLE (in case it changed)
      int bleRSSI = BLEConfig::getRSSI();
      Serial.print("BLE RSSI: ");
      Serial.println(bleRSSI);
      
      // Recreate CSV with BLE signal strength
      csvData = createSensorCSV(imuData, gpsData, bleRSSI);
      
      // Send data via BLE
      bleDataSent = sendDataWithRetryLogic(csvData);
      
      if (bleDataSent) {
        Serial.println("Data sent successfully via BLE");
        dataSent = true;
      } else {
        Serial.println("BLE transmission failed");
      }
      
      // Stop BLE after transmission attempt
      BLEConfig::stop();
      break; // Exit BLE loop
    }
    
    delay(100); // Small delay to prevent tight loop
  }
  
  // If BLE timeout reached without connection
  if (!bleConnected) {
    Serial.println("BLE advertising timeout reached - no connection");
    BLEConfig::stop(); // Ensure BLE is stopped
  }
  
  // ============================================
  // STEP 3: Try WiFi transmission (if BLE failed)
  // ============================================
  if (!dataSent) {
    Serial.println("Attempting WiFi transmission...");
    
    // Check if WiFi credentials exist
    String ssid = NVSConfig::getWiFiSSID();
    String password = NVSConfig::getWiFiPassword();
    
    if (ssid.length() > 0 && password.length() > 0) {
      Serial.println("WiFi credentials found, attempting connection...");
      
      // Connect to WiFi
      bool wifiConnected = CustomWiFi::connectWiFi();
      
      if (wifiConnected) {
        Serial.println("WiFi connected! Sending data...");
        
        // Get WiFi signal strength
        int wifiRSSI = CustomWiFi::getRSSI();
        Serial.print("WiFi RSSI: ");
        Serial.println(wifiRSSI);
        
        // Recreate CSV with WiFi signal strength
        csvData = createSensorCSV(imuData, gpsData, wifiRSSI);
        
        // Sync time if not already synced (try on first cycle, or retry if previous sync failed)
        if (!TimeSync::isTimeSynced()) {
          Serial.println("Time not synced - attempting NTP sync...");
          bool syncSuccess = TimeSync::syncTimeNTP();
          if (syncSuccess) {
            Serial.println("Time sync successful!");
          } else {
            Serial.println("Time sync failed - will retry next cycle");
          }
        } else {
          Serial.println("Time already synced");
        }
        
        // Send data via WiFi
        bool wifiDataSent = sendDataWithRetryLogic(csvData);
        
        if (wifiDataSent) {
          Serial.println("Data sent successfully via WiFi");
          dataSent = true;
        } else {
          Serial.println("WiFi transmission failed");
        }
        
        // Disconnect WiFi
        CustomWiFi::disconnectWiFi();
      } else {
        Serial.println("WiFi connection failed");
      }
    } else {
      Serial.println("No WiFi credentials found - skipping WiFi attempt");
    }
  } else {
    Serial.println("Skipping WiFi (data already sent via BLE)");
  }
  
  // ============================================
  // STEP 4: Queue data if transmission failed
  // ============================================
  // Note: sendDataWithRetryLogic already handles queuing automatically if transmission fails
  // So we don't need to call it again here - it was already called in BLE or WiFi sections
  if (!dataSent) {
    Serial.println("Note: Data was queued by transmission handler (will retry next cycle)");
  }
  
  // ============================================
  // STEP 5: Prepare for deep sleep
  // ============================================
  Serial.println("Preparing for deep sleep...");
  
  // Ensure BLE is stopped
  if (BLEConfig::isEnabled()) {
    Serial.println("Stopping BLE...");
    BLEConfig::stop();
  }
  
  // Ensure WiFi is off
  if (CustomWiFi::isConnected()) {
    Serial.println("Disconnecting WiFi...");
    CustomWiFi::disconnectWiFi();
  }
  
  // Force save queue pointers before deep sleep to prevent data loss
  Serial.println("Saving queue state...");
  transmissionHandler.saveQueueState();
  
  // Save last known GPS location before deep sleep (if we have a valid fix)
  if (gpsInitialized) {
    GPSData currentGPS = gpsSensor.getGPSData();
    if (currentGPS.hasValidFix) {
      Serial.println("Saving last known GPS location to NVS...");
      gpsSensor.saveLastKnownLocation();
    } else {
      // Try to save from lastKnownData if current doesn't have fix
      // This is handled internally by saveLastKnownLocation()
      gpsSensor.saveLastKnownLocation();
    }
  }
  
  // Dim LEDs based on debug mode before deep sleep
  uint8_t debugMode = NVSConfig::getDebugMode();
  if (debugMode == 1) {
    Serial.println("Debug mode enabled - Dimming LEDs to 10% for deep sleep...");
  } else {
    Serial.println("Debug mode disabled - Turning LEDs off for deep sleep...");
  }
  dimStatusLEDForDeepSleep(); // Sets brightness based on debug mode (0% or 10%)
  if (debugMode == 1) {
    batteryIndicatorLED.updateBatteryLED(); // Update battery pixel color with new brightness (only if LED is on)
  }
  delay(50); // Brief delay to ensure LED update completes
  
  // Mark that first cycle is complete
  isFirstCycle = false;
  
  // Set deep sleep timer: always 15 minutes
  Serial.println("Entering deep sleep for 15 minutes...");
  esp_sleep_enable_timer_wakeup(CYCLE_DURATION);
  
  // Small delay to allow serial output to complete
  delay(100);
  
  // Enter deep sleep
  esp_deep_sleep_start();
  
  // This code should never be reached (device will reset after deep sleep)
  // But included for safety
  Serial.println("ERROR: Deep sleep failed!");
  delay(1000);
}