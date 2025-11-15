/*
 * ESP32-S3 WROOM with BNO085 IMU Sensor and NEO-M9N GPS
 * Reads and sends IMU and GPS data as JSON via POST request
 */

#include "imu_sensor.h"
#include "gps_sensor.h"
#include "customwifi.h"
#include "time_sync.h"
#include "device_id.h"
#include "data_queue.h"
#include "transmission_handler.h"
#include "nvs_config.h"
#include "battery_monitor.h"
#include "battery_indicator_led.h"
#include "ble_config.h"
#include <esp_wifi.h>
#include <esp_event.h>
#include <Adafruit_NeoPixel.h>

// LED Configuration
const int LED_PIN = 40;
const unsigned long LED_PULSE_DURATION = 20; // milliseconds

// NeoPixel Status LED Configuration
const int STATUS_LED_PIN = 39;
const int NUM_LEDS = 1;

// LED State
volatile bool ledPulseRequested = false;
unsigned long ledPulseStartTime = 0;

// NeoPixel Status LED instance
Adafruit_NeoPixel statusLED(NUM_LEDS, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

// Battery Indicator LED instance (separate from status LED)
BatteryIndicatorLED batteryIndicatorLED;

// Transmission handler for failed data retry logic
TransmissionHandler transmissionHandler;

// Non-blocking POST state
bool postInProgress = false;
unsigned long postStartTime = 0;
String pendingJSONData = "";

// Create sensor instances
IMUSensor imuSensor;
GPSSensor gpsSensor;

// Sensor status flags
bool imuInitialized = false;
bool gpsInitialized = false;

// Configuration: Reading interval in milliseconds
const unsigned long READING_INTERVAL = 30000; // 30000ms = 30 seconds

// WiFi Event Handler for TX events
void wifiEventHandler(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
  if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_TX_DONE) {
    ledPulseRequested = true;
    ledPulseStartTime = millis();
  }
}

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
  
  Serial.println("ESP32-S3 BNO085 IMU + NEO-M9N GPS Sensor Test");
  Serial.println("=============================================");
  
  // Initialize NVS for WiFi credentials storage
  NVSConfig::initializeNVS();
  
  // Initialize BLE for WiFi credential configuration (always advertising)
  BLEConfig::begin();
  
  // Initialize Battery Monitor ADC
  BatteryMonitor::initializeADC();
  delay(100);
  
  // Initialize Battery Indicator LED
  batteryIndicatorLED.begin();
  batteryIndicatorLED.updateBatteryLED(); // Set initial color based on battery
  delay(100);
  
  // Initialize NeoPixel Status LED
  statusLED.begin();
  statusLED.setBrightness(255);
  updateStatusLED(); // Show red initially (not initialized)
  Serial.println("NeoPixel Status LED initialized on GPIO " + String(STATUS_LED_PIN));
  
  // Initialize LED pin
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Serial.println("LED initialized on GPIO " + String(LED_PIN));
  
  // Register WiFi event handler for TX events
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_TX_DONE, &wifiEventHandler, NULL));
  Serial.println("WiFi TX event handler registered");
  
  // Initialize IMU sensor
  imuInitialized = imuSensor.begin();
  if (imuInitialized) {
    Serial.println("IMU sensor initialized successfully");
  } else {
    Serial.println("IMU sensor initialization FAILED");
  }
  
  // Initialize GPS sensor
  gpsInitialized = gpsSensor.begin();
  if (gpsInitialized) {
    Serial.println("GPS sensor initialized successfully");
  } else {
    Serial.println("GPS sensor initialization FAILED");
  }
  
  // Update status LED based on sensor initialization
  updateStatusLED();
  
  // Connect to WiFi
  CustomWiFi::connectWiFi();
  
  // Sync time with NTP server
  TimeSync::syncTimeNTP();
  
  Serial.println("\nStarting data stream...\n");
  delay(100);
}

String createSensorJSON(IMUData imuData, GPSData gpsData) {
  String json = "{";

  json += "\"device_id\":\"" + DeviceID::getMACAddress() + "\",";
  json += "\"battery_level\":" + String(BatteryMonitor::getBatteryPercentage()) + ",";
  json += "\"timestamp\":\"" + TimeSync::getCurrentTimeString() + "\",";
  
  // IMU Data
  json += "\"imu\":{";
  
  if (imuData.hasQuaternion) {
    json += "\"quaternion\":{";
    json += "\"i\":" + String(imuData.quaternion.i, 4) + ",";
    json += "\"j\":" + String(imuData.quaternion.j, 4) + ",";
    json += "\"k\":" + String(imuData.quaternion.k, 4) + ",";
    json += "\"real\":" + String(imuData.quaternion.real, 4) + ",";
    json += "\"accuracy\":" + String(imuData.quaternion.accuracy, 4);
    json += "},";
    
    json += "\"euler\":{";
    json += "\"roll\":" + String(imuData.euler.roll, 2) + ",";
    json += "\"pitch\":" + String(imuData.euler.pitch, 2) + ",";
    json += "\"yaw\":" + String(imuData.euler.yaw, 2);
    json += "},";
  }
  
  if (imuData.hasAccel) {
    json += "\"accelerometer\":{";
    json += "\"x\":" + String(imuData.accelerometer.x, 3) + ",";
    json += "\"y\":" + String(imuData.accelerometer.y, 3) + ",";
    json += "\"z\":" + String(imuData.accelerometer.z, 3);
    json += "},";
  }
  
  if (imuData.hasGyro) {
    json += "\"gyroscope\":{";
    json += "\"x\":" + String(imuData.gyroscope.x, 3) + ",";
    json += "\"y\":" + String(imuData.gyroscope.y, 3) + ",";
    json += "\"z\":" + String(imuData.gyroscope.z, 3);
    json += "},";
  }
  
  if (imuData.hasMag) {
    json += "\"magnetometer\":{";
    json += "\"x\":" + String(imuData.magnetometer.x, 3) + ",";
    json += "\"y\":" + String(imuData.magnetometer.y, 3) + ",";
    json += "\"z\":" + String(imuData.magnetometer.z, 3);
    json += "}";
  }
  
  // GPS Data
  json += "\"gps\":{";
  json += "\"fix\":" + String(gpsData.hasValidFix ? "true" : "false") + ",";
  json += "\"fixType\":" + String(gpsData.fixType) + ",";
  json += "\"satellites\":" + String(gpsData.satellites) + ",";
  
  if (gpsData.hasValidFix) {
    json += "\"latitude\":" + String(gpsData.latitude, 7) + ",";
    json += "\"longitude\":" + String(gpsData.longitude, 7) + ",";
    json += "\"altitude\":" + String(gpsData.altitude, 2) + ",";
    json += "\"speed\":" + String(gpsData.speed, 2) + ",";
    json += "\"heading\":" + String(gpsData.heading, 2) + ",";
    json += "\"hdop\":" + String(gpsData.hdop, 2);
  } else {
    json += "\"latitude\":0,";
    json += "\"longitude\":0,";
    json += "\"altitude\":0,";
    json += "\"speed\":0,";
    json += "\"heading\":0,";
    json += "\"hdop\":0";
  }

  json += "}";
  
  return json;
}

void sendDataWithRetryLogic(String jsonData) {
  Serial.println("\n--- Starting Data Transmission with Retry Logic ---");
  
  // Use transmission handler to check queue and send appropriately
  bool success = transmissionHandler.handleDataTransmission(jsonData);
  
  if (success) {
    Serial.println("Data transmission successful!");
  } else {
    Serial.println("Data transmission failed - queued for retry");
  }
  postInProgress = false;
}

void handleNonBlockingPost() {
  if (!postInProgress) return;
  
  unsigned long elapsed = millis() - postStartTime;
  
  // Timeout after 30 seconds
  if (elapsed > 30000) {
    Serial.println("POST request timeout");
    postInProgress = false;
  }
}

void loop() {
  static unsigned long lastPrintTime = 0;
  static unsigned long lastBatteryUpdate = 0;
  static bool sensorsOn = true;
  static bool firstRun = true;
  unsigned long currentTime = millis();
  
  // Update BLE (handles connections and processes WiFi credentials)
  BLEConfig::update();
  
  // Ensure BLE advertising continues (restart if needed)
  if (!BLEConfig::isConnected()) {
    static unsigned long lastBLEAdvertiseCheck = 0;
    if (currentTime - lastBLEAdvertiseCheck >= 5000) { // Check every 5 seconds
      BLEConfig::restartAdvertising();
      lastBLEAdvertiseCheck = currentTime;
    }
  }
  
  // Handle LED pulse for WiFi TX events (runs every loop iteration)
  if (ledPulseRequested) {
    if (currentTime - ledPulseStartTime < LED_PULSE_DURATION) {
      digitalWrite(LED_PIN, HIGH); // LED ON
    } else {
      digitalWrite(LED_PIN, LOW); // LED OFF
      ledPulseRequested = false;
    }
  }
  
  // Update battery indicator LED every 5 seconds
  if (currentTime - lastBatteryUpdate >= 5000) {
    batteryIndicatorLED.updateBatteryLED();
    lastBatteryUpdate = currentTime;
    
    // Print battery info to serial
    Serial.println(BatteryMonitor::getBatteryStatus());
    
    // Critical battery warning
    if (BatteryMonitor::getBatteryPercentage() < 10) {
      Serial.println("!!! CRITICAL BATTERY !!!");
      batteryIndicatorLED.doubleBlink();
    }
  }
  
  // Handle non-blocking POST in background
  handleNonBlockingPost();
  
  // Check if interval has passed or first run
  if (firstRun || (currentTime - lastPrintTime >= READING_INTERVAL)) {
    // Power on sensors (skip on first run since they're already on)
    if (!sensorsOn && !firstRun) {
      Serial.println("\n--- Waking up sensors ---");
      imuSensor.powerOn();
      gpsSensor.powerOn();
      sensorsOn = true;
      delay(2000); // Give sensors time to stabilize
    }
    
    // Reconnect WiFi for retry logic and transmission
    Serial.println("\n--- Reconnecting WiFi ---");
    CustomWiFi::connectWiFi();
    delay(1000); // Wait for WiFi to stabilize
    
    // Update sensors to get fresh data
    for (int i = 0; i < 20; i++) {
      imuSensor.update();
      gpsSensor.update();
      delay(50); // Wait 50ms between updates to collect data
    }
    
    // Get data from sensors
    IMUData imuData = imuSensor.getIMUData();
    GPSData gpsData = gpsSensor.getGPSData();
    
    // Create JSON and send with retry logic
    String jsonData = createSensorJSON(imuData, gpsData);
    sendDataWithRetryLogic(jsonData);
    
    // Power off sensors immediately (transmission continues in background)
    Serial.println("--- Powering down sensors ---");
    imuSensor.powerOff();
    gpsSensor.powerOff();
    sensorsOn = false;
    
    // Power off WiFi
    Serial.println("--- Turning off WiFi ---");
    CustomWiFi::disconnectWiFi();
    
    lastPrintTime = currentTime;
    firstRun = false;
  }
  
  delay(100);
}