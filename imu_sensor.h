/*
 * IMU Sensor Module - BNO085
 * Handles all IMU sensor initialization and data reading
 */

#ifndef IMU_SENSOR_H
#define IMU_SENSOR_H

#include <Wire.h>
#include "SparkFun_BNO08x_Arduino_Library.h"

// IMU pin definitions
#define I2C_SDA_PIN 8      // IMU_SDA
#define I2C_SCL_PIN 9      // IMU_SCL
#define IMU_RESET_PIN 12   // IMU_RESET pin
#define IMU_INT_PIN 13     // INT pin for interrupts (optional)

// Structure to hold all IMU data
struct IMUData {
  struct {
    float i, j, k, real, accuracy;
  } quaternion;
  
  struct {
    float roll, pitch, yaw;
  } euler;
  
  struct {
    float x, y, z;
  } accelerometer;
  
  struct {
    float x, y, z;
  } gyroscope;
  
  struct {
    float x, y, z;
  } magnetometer;
  
  unsigned long timestamp;
  bool hasQuaternion = false;
  bool hasAccel = false;
  bool hasGyro = false;
  bool hasMag = false;
};

class IMUSensor {
private:
  BNO08x imu;
  IMUData data;

public:
  bool begin() {
    // Initialize I2C with custom pins
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000); // 400kHz I2C speed
    
    // Setup IMU reset pin
    pinMode(IMU_RESET_PIN, OUTPUT);
    digitalWrite(IMU_RESET_PIN, HIGH);
    delay(10);
    digitalWrite(IMU_RESET_PIN, LOW);
    delay(10);
    digitalWrite(IMU_RESET_PIN, HIGH);
    delay(100);
    
    // Initialize BNO085
    Serial.println("Initializing BNO085...");
    
    if (!imu.begin()) {
      Serial.println("BNO085 not detected! Check wiring.");
      return false;
    }
    
    Serial.println("BNO085 found!");
    
    // Enable reports
    if (imu.enableRotationVector() == true) {
      Serial.println("Rotation vector enabled");
    }
    
    if (imu.enableAccelerometer() == true) {
      Serial.println("Accelerometer enabled");
    }
    
    if (imu.enableGyro() == true) {
      Serial.println("Gyroscope enabled");
    }
    
    if (imu.enableMagnetometer() == true) {
      Serial.println("Magnetometer enabled");
    }
    
    return true;
  }
  
  void update() {
    // Check if IMU data is available
    if (imu.wasReset()) {
      Serial.println("{\"status\":\"imu_reset\"}");
    }
    
    if (imu.getSensorEvent() == true) {
      
      // Rotation Vector (Quaternion)
      if (imu.getSensorEventID() == SENSOR_REPORTID_ROTATION_VECTOR) {
        data.quaternion.i = imu.getQuatI();
        data.quaternion.j = imu.getQuatJ();
        data.quaternion.k = imu.getQuatK();
        data.quaternion.real = imu.getQuatReal();
        data.quaternion.accuracy = imu.getQuatRadianAccuracy();
        data.hasQuaternion = true;
        
        // Convert to Euler angles
        float roll = atan2(2.0 * (data.quaternion.real * data.quaternion.i + data.quaternion.j * data.quaternion.k), 
                           1.0 - 2.0 * (data.quaternion.i * data.quaternion.i + data.quaternion.j * data.quaternion.j));
        float pitch = asin(2.0 * (data.quaternion.real * data.quaternion.j - data.quaternion.k * data.quaternion.i));
        float yaw = atan2(2.0 * (data.quaternion.real * data.quaternion.k + data.quaternion.i * data.quaternion.j), 
                          1.0 - 2.0 * (data.quaternion.j * data.quaternion.j + data.quaternion.k * data.quaternion.k));
        
        data.euler.roll = roll * 180.0 / PI;
        data.euler.pitch = pitch * 180.0 / PI;
        data.euler.yaw = yaw * 180.0 / PI;
      }
      
      // Accelerometer
      if (imu.getSensorEventID() == SENSOR_REPORTID_ACCELEROMETER) {
        data.accelerometer.x = imu.getAccelX();
        data.accelerometer.y = imu.getAccelY();
        data.accelerometer.z = imu.getAccelZ();
        data.hasAccel = true;
      }
      
      // Gyroscope
      if (imu.getSensorEventID() == SENSOR_REPORTID_GYROSCOPE_CALIBRATED) {
        data.gyroscope.x = imu.getGyroX();
        data.gyroscope.y = imu.getGyroY();
        data.gyroscope.z = imu.getGyroZ();
        data.hasGyro = true;
      }
      
      // Magnetometer
      if (imu.getSensorEventID() == SENSOR_REPORTID_MAGNETIC_FIELD) {
        data.magnetometer.x = imu.getMagX();
        data.magnetometer.y = imu.getMagY();
        data.magnetometer.z = imu.getMagZ();
        data.hasMag = true;
      }
    }
    
    data.timestamp = millis();
  }
  
  IMUData getIMUData() {
    return data;
  }
};

#endif
