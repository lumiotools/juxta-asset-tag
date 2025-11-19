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

// Device ID Configuration (hardcoded to save memory)
const char* DEVICE_ID = "ASSET_TAG_001";  // Change this for each device

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
  
  // Initialize BLE for WiFi credential configuration (always advertising)
  BLEConfig::begin();
  
  // Set Device ID for BLE
  BLEConfig::setDeviceId(DEVICE_ID);
  
  // Initialize BLE LED pin
  BLEConfig::setBLELEDPin(BLE_LED_PIN);
  
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

String createSensorJSON(IMUData imuData, GPSData gpsData) {
  static char jsonBuffer[800];
  char timestamp[20];
  int pos = 0;
  
  TimeSync::getCurrentTimeString(timestamp, sizeof(timestamp));
  int batteryLevel = BatteryMonitor::getBatteryPercentage();
  
  pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, "{\"device_id\":\"%s\",\"battery_level\":%d,\"timestamp\":\"%s\",\"imu\":{\"quaternion\":{\"i\":%.4f,\"j\":%.4f,\"k\":%.4f,\"real\":%.4f},", 
                  DEVICE_ID, batteryLevel, timestamp,
                  imuData.quaternion.i, imuData.quaternion.j, imuData.quaternion.k, imuData.quaternion.real);
  
  pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, 
                  "\"euler\":{\"roll\":%.2f,\"pitch\":%.2f,\"yaw\":%.2f},", 
                  imuData.euler.roll, imuData.euler.pitch, imuData.euler.yaw);
  
  pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, 
                  "\"accelerometer\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f},", 
                  imuData.accelerometer.x, imuData.accelerometer.y, imuData.accelerometer.z);
  
  pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, 
                  "\"gyroscope\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f},", 
                  imuData.gyroscope.x, imuData.gyroscope.y, imuData.gyroscope.z);
  
  pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, 
                  "\"magnetometer\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f}},", 
                  imuData.magnetometer.x, imuData.magnetometer.y, imuData.magnetometer.z);
  
  pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, "\"gps\":{\"fix\":%s,\"fixType\":%d,\"satellites\":%d,", 
                  gpsData.hasValidFix ? "true" : "false", gpsData.fixType, gpsData.satellites);
  
  if (gpsData.hasValidFix) {
    pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, 
                    "\"latitude\":%.7f,\"longitude\":%.7f,\"altitude\":%.2f,\"speed\":%.2f,\"heading\":%.2f,\"hdop\":%.2f", 
                    gpsData.latitude, gpsData.longitude, gpsData.altitude, 
                    gpsData.speed, gpsData.heading, gpsData.hdop);
  } else {
    pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, 
                    "\"latitude\":0,\"longitude\":0,\"altitude\":0,\"speed\":0,\"heading\":0,\"hdop\":0");
  }
  
  pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, "}}");
  
  return String(jsonBuffer);
}

void sendDataWithRetryLogic(String jsonData) {
  // Use transmission handler to send via WiFi and/or BLE with queue logic
  transmissionHandler.handleDataTransmission(jsonData);
}

void loop() {
  static unsigned long lastPrintTime = 0;
  static unsigned long lastBatteryUpdate = 0;
  static bool sensorsOn = true;
  static bool firstRun = true;
  unsigned long currentTime = millis();
  
  // Update BLE (handles connections and processes WiFi credentials)
  BLEConfig::update();
  
  // Ensure BLE advertising continues (simplified check)
  static unsigned long lastBLEAdvertiseCheck = 0;
  if (!BLEConfig::isConnected() && (currentTime - lastBLEAdvertiseCheck >= 5000)) {
    BLEConfig::restartAdvertising();
    lastBLEAdvertiseCheck = currentTime;
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
    // Power on sensors (skip on first run since they're already on)
    if (!sensorsOn && !firstRun) {
      Serial.println("imuInitialized: " + String(imuInitialized));
      Serial.println("gpsInitialized: " + String(gpsInitialized));
      if (imuInitialized) {
        imuSensor.powerOn();
        // Update initialization flag in case powerOn() re-initialized successfully
        imuInitialized = imuSensor.getInitialized();
      }
      if (gpsInitialized) {
        gpsSensor.powerOn();
      }
      sensorsOn = true;
      delay(2000); // Give sensors time to stabilize
    }
    
    if (!BLEConfig::isConnected()) {
      // Reconnect WiFi for retry logic and transmission
      CustomWiFi::connectWiFi();
      delay(1000); // Wait for WiFi to stabilize
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
    
    // Create JSON and send with retry logic (LED blinks automatically during transmission)
    String jsonData = createSensorJSON(imuData, gpsData);
    sendDataWithRetryLogic(jsonData);
    
    // Power off sensors immediately
    imuSensor.powerOff();
    gpsSensor.powerOff();
    sensorsOn = false;
    
    if (CustomWiFi::isConnected()) {
      // Power off WiFi
      CustomWiFi::disconnectWiFi();
    }
    
    lastPrintTime = currentTime;
    firstRun = false;
  }
  
  delay(100);
}