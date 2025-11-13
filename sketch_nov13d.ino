/*
 * ESP32-S3 WROOM with BNO085 IMU Sensor and NEO-M9N GPS
 * Reads and prints IMU and GPS data as JSON
 */

#include "imu_sensor.h"
#include "gps_sensor.h"

// Create sensor instances
IMUSensor imuSensor;
GPSSensor gpsSensor;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("ESP32-S3 BNO085 IMU + NEO-M9N GPS Sensor Test");
  Serial.println("=============================================");
  
  // Initialize IMU sensor
  imuSensor.begin();
  
  // Initialize GPS sensor
  gpsSensor.begin();
  
  Serial.println("\nStarting data stream...\n");
  delay(100);
}

void printIMUJSON(IMUData imuData) {
  Serial.print("{\"imu\":{");
  
  if (imuData.hasQuaternion) {
    Serial.print("\"quaternion\":{");
    Serial.print("\"i\":"); Serial.print(imuData.quaternion.i, 4); Serial.print(",");
    Serial.print("\"j\":"); Serial.print(imuData.quaternion.j, 4); Serial.print(",");
    Serial.print("\"k\":"); Serial.print(imuData.quaternion.k, 4); Serial.print(",");
    Serial.print("\"real\":"); Serial.print(imuData.quaternion.real, 4); Serial.print(",");
    Serial.print("\"accuracy\":"); Serial.print(imuData.quaternion.accuracy, 4);
    Serial.print("},");
    
    Serial.print("\"euler\":{");
    Serial.print("\"roll\":"); Serial.print(imuData.euler.roll, 2); Serial.print(",");
    Serial.print("\"pitch\":"); Serial.print(imuData.euler.pitch, 2); Serial.print(",");
    Serial.print("\"yaw\":"); Serial.print(imuData.euler.yaw, 2);
    Serial.print("},");
  }
  
  if (imuData.hasAccel) {
    Serial.print("\"accelerometer\":{");
    Serial.print("\"x\":"); Serial.print(imuData.accelerometer.x, 3); Serial.print(",");
    Serial.print("\"y\":"); Serial.print(imuData.accelerometer.y, 3); Serial.print(",");
    Serial.print("\"z\":"); Serial.print(imuData.accelerometer.z, 3);
    Serial.print("},");
  }
  
  if (imuData.hasGyro) {
    Serial.print("\"gyroscope\":{");
    Serial.print("\"x\":"); Serial.print(imuData.gyroscope.x, 3); Serial.print(",");
    Serial.print("\"y\":"); Serial.print(imuData.gyroscope.y, 3); Serial.print(",");
    Serial.print("\"z\":"); Serial.print(imuData.gyroscope.z, 3);
    Serial.print("},");
  }
  
  if (imuData.hasMag) {
    Serial.print("\"magnetometer\":{");
    Serial.print("\"x\":"); Serial.print(imuData.magnetometer.x, 3); Serial.print(",");
    Serial.print("\"y\":"); Serial.print(imuData.magnetometer.y, 3); Serial.print(",");
    Serial.print("\"z\":"); Serial.print(imuData.magnetometer.z, 3);
    Serial.print("}");
  }
  
  Serial.print(",\"timestamp\":");
  Serial.print(imuData.timestamp);
  Serial.println("}}");
}

void printGPSJSON(GPSData gpsData) {
  Serial.print("{\"gps\":{");
  Serial.print("\"fix\":"); Serial.print(gpsData.hasValidFix ? "true" : "false"); Serial.print(",");
  Serial.print("\"fixType\":"); Serial.print(gpsData.fixType); Serial.print(",");
  Serial.print("\"satellites\":"); Serial.print(gpsData.satellites); Serial.print(",");
  
  if (gpsData.hasValidFix) {
    Serial.print("\"latitude\":"); Serial.print(gpsData.latitude, 7); Serial.print(",");
    Serial.print("\"longitude\":"); Serial.print(gpsData.longitude, 7); Serial.print(",");
    Serial.print("\"altitude\":"); Serial.print(gpsData.altitude, 2); Serial.print(",");
    Serial.print("\"speed\":"); Serial.print(gpsData.speed, 2); Serial.print(",");
    Serial.print("\"heading\":"); Serial.print(gpsData.heading, 2); Serial.print(",");
    Serial.print("\"hdop\":"); Serial.print(gpsData.hdop, 2);
  } else {
    Serial.print("\"latitude\":0,");
    Serial.print("\"longitude\":0,");
    Serial.print("\"altitude\":0,");
    Serial.print("\"speed\":0,");
    Serial.print("\"heading\":0,");
    Serial.print("\"hdop\":0");
  }
  
  Serial.print(",\"timestamp\":");
  Serial.print(gpsData.timestamp);
  Serial.println("}}");
}

void loop() {
  static unsigned long lastPrintTime = 0;
  unsigned long currentTime = millis();
  
  // Update sensors
  imuSensor.update();
  gpsSensor.update();
  
  // Print JSON at regular intervals (50ms = 20Hz)
  if (currentTime - lastPrintTime >= 50) {
    // Get data from sensors
    IMUData imuData = imuSensor.getIMUData();
    GPSData gpsData = gpsSensor.getGPSData();
    
    // Print separate JSON objects for each sensor
    printIMUJSON(imuData);
    printGPSJSON(gpsData);
    
    lastPrintTime = currentTime;
  }
}