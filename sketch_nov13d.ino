/*
 * ESP32-S3 WROOM with BNO085 IMU Sensor and NEO-M9N GPS
 * Reads and sends IMU and GPS data as JSON via POST request
 */

#include "imu_sensor.h"
#include "gps_sensor.h"
#include "customwifi.h"

// Create sensor instances
IMUSensor imuSensor;
GPSSensor gpsSensor;

// Configuration: Reading interval in milliseconds
const unsigned long READING_INTERVAL = 30000; // 30000ms = 30 seconds
const int MAX_RETRIES = 2; // Retry 2 times on failure

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("ESP32-S3 BNO085 IMU + NEO-M9N GPS Sensor Test");
  Serial.println("=============================================");
  
  // Initialize IMU sensor
  imuSensor.begin();
  
  // Initialize GPS sensor
  gpsSensor.begin();
  
  // Connect to WiFi
  CustomWiFi::connectWiFi();
  
  Serial.println("\nStarting data stream...\n");
  delay(100);
}

String createSensorJSON(IMUData imuData, GPSData gpsData) {
  String json = "{\"sensors\":{";
  
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
  
  json += ",\"timestamp\":" + String(imuData.timestamp);
  json += "},";
  
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
  
  json += ",\"timestamp\":" + String(gpsData.timestamp);
  json += "}}";
  
  return json;
}

bool sendSensorDataWithRetry(String jsonData) {
  int retries = 0;
  bool success = false;
  
  while (retries < MAX_RETRIES && !success) {
    Serial.println("\n--- Sending sensor data (Attempt " + String(retries + 1) + "/" + String(MAX_RETRIES) + ") ---");
    success = CustomWiFi::sendSensorData(jsonData);
    
    if (!success) {
      retries++;
      if (retries < MAX_RETRIES) {
        Serial.println("Retrying in 2 seconds...");
        delay(2000);
      }
    }
  }
  
  if (!success) {
    Serial.println("ERROR: Sending through WiFi failed after " + String(MAX_RETRIES) + " attempts\n");
    return false;
  } else {
    Serial.println("Data sent successfully\n");
    return true;
  }
}

void loop() {
  static unsigned long lastPrintTime = 0;
  static bool sensorsOn = true;
  static bool firstRun = true;
  unsigned long currentTime = millis();
  
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
    
    // Update sensors to get fresh data
    for (int i = 0; i < 20; i++) {
      imuSensor.update();
      gpsSensor.update();
      delay(50); // Wait 50ms between updates to collect data
    }
    
    // Get data from sensors
    IMUData imuData = imuSensor.getIMUData();
    GPSData gpsData = gpsSensor.getGPSData();
    
    // Create JSON and send via WiFi
    String jsonData = createSensorJSON(imuData, gpsData);
    bool dataSent = sendSensorDataWithRetry(jsonData);
    
    if (dataSent) {
      Serial.println("Data transmission successful");
    } else {
      Serial.println("Data transmission failed");
    }
    
    // Power off sensors
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