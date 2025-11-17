// ESP32-S3 Asset Tag - IMU & GPS data transmission

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
#include "ws2812b_simple.h"

// LED Configuration
const int LED_PIN = 40;
const unsigned long LED_PULSE_DURATION = 20; // milliseconds

// NeoPixel Status LED Configuration
const int STATUS_LED_PIN = RGB_BUILTIN;

// LED State
volatile bool ledPulseRequested = false;
unsigned long ledPulseStartTime = 0;

// Status LED instance
WS2812B statusLED(STATUS_LED_PIN);

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

// LED Pulse Handler - Called when WiFi transmission completes
void triggerLEDPulse() {
  ledPulseRequested = true;
  ledPulseStartTime = millis();
}

void updateStatusLED() {
  // Green if both IMU and GPS initialized, Red if either failed
  if (imuInitialized && gpsInitialized) {
    statusLED.setPixelColor(0, 0, 255, 0); // Green
  } else {
    statusLED.setPixelColor(0, 255, 0, 0); // Red
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
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
  
  // Initialize Status LED
  statusLED.setBrightness(255);
  updateStatusLED(); // Show red initially (not initialized)
  
  // Initialize LED pin
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Register callback for WiFi transmission completion (triggers LED pulse)
  CustomWiFi::setTransmissionCallback(triggerLEDPulse);
  
  // Initialize IMU sensor
  imuInitialized = imuSensor.begin();
  
  // Initialize GPS sensor
  gpsInitialized = gpsSensor.begin();
  
  // Update status LED based on sensor initialization
  updateStatusLED();
  // One-time credential setup (comment out after first upload)
  NVSConfig::setWiFiSSID("GarageNeo");
  NVSConfig::setWiFiPassword("G@r@ge#123");
  // Connect to WiFi
  CustomWiFi::connectWiFi();
  
  // Sync time with NTP server
  TimeSync::syncTimeNTP();
  
  delay(100);
}

String createSensorJSON(IMUData imuData, GPSData gpsData) {
  static char jsonBuffer[900]; // Reduced: typical JSON ~600-800 bytes
  int pos = 0;
  
  String deviceId = DeviceID::getMACAddress();
  String timestamp = TimeSync::getCurrentTimeString();
  int batteryLevel = BatteryMonitor::getBatteryPercentage();
  
  pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, "{\"device_id\":\"%s\",\"battery_level\":%d,\"timestamp\":\"%s\",\"imu\":{", 
                  deviceId.c_str(), batteryLevel, timestamp.c_str());
  
  if (imuData.hasQuaternion) {
    pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, 
                    "\"quaternion\":{\"i\":%.4f,\"j\":%.4f,\"k\":%.4f,\"real\":%.4f,\"accuracy\":%.4f},", 
                    imuData.quaternion.i, imuData.quaternion.j, imuData.quaternion.k, 
                    imuData.quaternion.real, imuData.quaternion.accuracy);
    pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, 
                    "\"euler\":{\"roll\":%.2f,\"pitch\":%.2f,\"yaw\":%.2f},", 
                    imuData.euler.roll, imuData.euler.pitch, imuData.euler.yaw);
  }
  
  if (imuData.hasAccel) {
    pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, 
                    "\"accelerometer\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f},", 
                    imuData.accelerometer.x, imuData.accelerometer.y, imuData.accelerometer.z);
  }
  
  if (imuData.hasGyro) {
    pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, 
                    "\"gyroscope\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f},", 
                    imuData.gyroscope.x, imuData.gyroscope.y, imuData.gyroscope.z);
  }
  
  if (imuData.hasMag) {
    pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, 
                    "\"magnetometer\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f}", 
                    imuData.magnetometer.x, imuData.magnetometer.y, imuData.magnetometer.z);
  }
  
  pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, "},\"gps\":{");
  pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, "\"fix\":%s,\"fixType\":%d,\"satellites\":%d,", 
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
  // Use transmission handler to check queue and send appropriately
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
    
    // Critical battery warning
    if (BatteryMonitor::getBatteryPercentage() < 10) {
      batteryIndicatorLED.doubleBlink();
    }
  }
  
  // Check if interval has passed or first run
  if (firstRun || (currentTime - lastPrintTime >= READING_INTERVAL)) {
    // Power on sensors (skip on first run since they're already on)
    if (!sensorsOn && !firstRun) {
      imuSensor.powerOn();
      gpsSensor.powerOn();
      sensorsOn = true;
      delay(2000); // Give sensors time to stabilize
    }
    
    // Reconnect WiFi for retry logic and transmission
    CustomWiFi::connectWiFi();
    delay(1000); // Wait for WiFi to stabilize
    
    // Update sensors to get fresh data (reduced iterations)
    for (int i = 0; i < 10; i++) {
      imuSensor.update();
      gpsSensor.update();
      delay(50);
    }
    
    // Get data from sensors
    IMUData imuData = imuSensor.getIMUData();
    GPSData gpsData = gpsSensor.getGPSData();
    
    // Create JSON and send with retry logic
    String jsonData = createSensorJSON(imuData, gpsData);
    sendDataWithRetryLogic(jsonData);
    
    // Power off sensors immediately (transmission continues in background)
    imuSensor.powerOff();
    gpsSensor.powerOff();
    sensorsOn = false;
    
    // Power off WiFi
    CustomWiFi::disconnectWiFi();
    
    lastPrintTime = currentTime;
    firstRun = false;
  }
  
  delay(100);
}