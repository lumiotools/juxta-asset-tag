// ESP32-S3 Asset Tag - IMU & GPS data transmission

#include "imu_sensor.h"
#include "gps_sensor.h"
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

// Device ID and Version Configuration (hardcoded to save memory)
const char* DEVICE_ID = "ASSET_TAG_001";  // Change this for each device
const char* DEVICE_VERSION = "v2.0.0";   // Device firmware/hardware version

// LED Configuration
const int WIFI_LED_PIN = 40;

// BLE LED Configuration
const int BLE_LED_PIN = 41;

// NeoPixel Status LED Configuration
const int STATUS_LED_PIN = 48;
const int STATUS_LED_COUNT = 1;

// Status LED instance
Adafruit_NeoPixel statusLED(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

// Battery Indicator LED instance (separate from status LED)
BatteryIndicatorLED batteryIndicatorLED;

// Transmission handler for failed data retry logic
TransmissionHandler transmissionHandler;

// BLE start time tracking
unsigned long bleStartTime = 0;

// Create sensor instances
IMUSensor imuSensor;
GPSSensor gpsSensor;

// Sensor status flags
bool imuInitialized = false;
bool gpsInitialized = false;

// Configuration: Reading interval in milliseconds
const unsigned long READING_INTERVAL = 30000; // 30000ms = 30 seconds


void updateStatusLED() {
  // Green if both IMU and GPS initialized, Red if either failed
  if (imuInitialized && gpsInitialized) {
    statusLED.setPixelColor(0, statusLED.Color(0, 255, 0)); // Green
  } else {
    statusLED.setPixelColor(0, statusLED.Color(255, 0, 0)); // Red
  }
  statusLED.show();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  statusLED.setPixelColor(0, statusLED.Color(255, 165, 0)); // Orange
  statusLED.show();
  delay(1000);
  
  // Initialize NVS for WiFi credentials storage
  NVSConfig::initializeNVS();
  
  // Initialize transmission handler and data queue (loads from flash, calculates max size on first boot)
  Serial.println("Initializing data queue...");
  if (!transmissionHandler.begin()) {
    Serial.println("Warning: Data queue initialization failed!");
  }
  
  // Check ESP reset reason
  esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.print("Reset reason: ");
  Serial.println(resetReason);
  
  // Initialize BLE only if reset reason is POWER_ON or reset button press
  if (resetReason == ESP_RST_POWERON || resetReason == ESP_RST_EXT) {
    Serial.println("Starting BLE (POWER_ON or reset button press)");
    BLEConfig::begin();
    BLEConfig::setDeviceId(DEVICE_ID);
    BLEConfig::setDeviceVersion(DEVICE_VERSION);
    BLEConfig::setBLELEDPin(BLE_LED_PIN);
    bleStartTime = millis(); // Set BLE start time when BLE begins
  } else {
    Serial.println("BLE not started (reset reason not POWER_ON or reset button)");
  }
  
  // Initialize Battery Monitor ADC
  BatteryMonitor::initializeADC();
  delay(100);
  
  // Initialize Battery Indicator LED
  batteryIndicatorLED.begin();
  batteryIndicatorLED.updateBatteryLED(); // Set initial color based on battery
  delay(100);
  
  // Initialize Status LED
  statusLED.begin();  // Initialize GPIO first!
  statusLED.setBrightness(100);
  statusLED.show();
  updateStatusLED(); // Show red initially (not initialized)
  
  // Initialize LED pin and register with WiFi class
  CustomWiFi::setTxLEDPin(WIFI_LED_PIN);
  
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
  
  // Update status LED based on sensor initialization
  updateStatusLED();
  // One-time credential setup (comment out after first upload)
  // NVSConfig::setWiFiSSID("GarageNeo");
  // NVSConfig::setWiFiPassword("G@r@ge#123");
  // Connect to WiFi
  CustomWiFi::connectWiFi();
  
  // Sync time with NTP server
  TimeSync::syncTimeNTP();
  
  delay(100);
}

String createSensorCSV(IMUData imuData, GPSData gpsData) {
  static char csvBuffer[500];
  char timestamp[20];
  
  TimeSync::getCurrentTimeString(timestamp, sizeof(timestamp));
  int batteryLevel = BatteryMonitor::getBatteryPercentage();
  
  // CSV format: device_id,battery_level,timestamp,imu.accelerometer.x,imu.accelerometer.y,imu.accelerometer.z,
  //             imu.gyroscope.x,imu.gyroscope.y,imu.gyroscope.z,imu.temperature,
  //             gps.fix,gps.fixType,gps.satellites,gps.latitude,gps.longitude,gps.altitude,gps.speed,gps.heading,gps.hdop
  
  snprintf(csvBuffer, sizeof(csvBuffer), 
           "%s,%d,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%s,%d,%d,%.7f,%.7f,%.2f,%.2f,%.2f,%.2f",
           DEVICE_ID,
           batteryLevel,
           timestamp,
           imuData.accelerometer.x, imuData.accelerometer.y, imuData.accelerometer.z,
           imuData.gyroscope.x, imuData.gyroscope.y, imuData.gyroscope.z,
           imuData.temperature,
           
           gpsData.hasValidFix ? "true" : "false",
           gpsData.fixType,
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

void loop() {
  static unsigned long lastPrintTime = 0;
  static unsigned long lastBatteryUpdate = 0;
  // sensor power management removed; IMU/GPS remain initialized throughout
  static bool firstRun = true;
  unsigned long currentTime = millis();
  
  // Only update BLE if it's enabled (saves power after BLE is stopped)
  static bool bleJustStopped = false;
  
  if (BLEConfig::isEnabled()) {
    // Update BLE (handles connections and processes WiFi credentials)
    BLEConfig::update();
    
    // Check if BLE is disconnected and more than 1 minute has passed since start
    if (!BLEConfig::isConnected() && bleStartTime > 0) {
      if (currentTime - bleStartTime >= 60000) { // 60000ms = 1 minute
        Serial.println("BLE disconnected for more than 1 minute, stopping BLE permanently");
        BLEConfig::stop();
        bleStartTime = 0; // Reset to prevent further checks
        bleJustStopped = true; // Flag to trigger deep sleep check after next transmission
      }
    }
  }
  
  
  
  // Update battery indicator LED every 5 seconds
  if (currentTime - lastBatteryUpdate >= 5000) {
    batteryIndicatorLED.updateBatteryLED();
    lastBatteryUpdate = currentTime;
    
    // Critical battery warning
    if (BatteryMonitor::getBatteryPercentage() < 10) {
      batteryIndicatorLED.doubleBlink();
    }
  }
  
  // Check if interval has passed or first run
  if (firstRun || (currentTime - lastPrintTime >= READING_INTERVAL)) {
    // Sensors remain initialized throughout runtime; no power cycling is performed.
    
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

    // No power-off calls — sensors remain active and will be updated periodically
    
    // Create CSV and send with retry logic (LED blinks automatically during transmission)
    String csvData = createSensorCSV(imuData, gpsData);
    
    if (!BLEConfig::isConnected()) {
      // Reconnect WiFi for retry logic and transmission
      CustomWiFi::connectWiFi();
      delay(1000); // Wait for WiFi to stabilize
    }
    
    bool sendSuccess = sendDataWithRetryLogic(csvData);

    // After BLE is powered off, check WiFi connection and transmission, then deep sleep
    if (bleJustStopped) {
      bleJustStopped = false;
      
      // Check WiFi connection status
      bool wifiConnected = CustomWiFi::isConnected();
      
      // Disconnect WiFi before deep sleep
      if (wifiConnected) {
        CustomWiFi::disconnectWiFi();
      }
      
      // Deep sleep based on WiFi connection and transmission success
      if (wifiConnected && sendSuccess) {
        Serial.println("WiFi connected and send successful - Deep sleeping for 30 seconds");
        esp_sleep_enable_timer_wakeup(30 * 1000000ULL); // 30 seconds in microseconds
      } else {
        Serial.println("WiFi not connected or send failed - Deep sleeping for 1 minute");
        esp_sleep_enable_timer_wakeup(1 * 60 * 1000000ULL); // 1 minute in microseconds
      }
      
      // Enter deep sleep
      esp_deep_sleep_start();
    }
    
    if (CustomWiFi::isConnected()) {
      // Power off WiFi
      CustomWiFi::disconnectWiFi();
    }
    
    lastPrintTime = currentTime;
    firstRun = false;
  }
  
  delay(100);
}