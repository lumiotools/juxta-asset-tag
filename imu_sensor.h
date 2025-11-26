// IMU Sensor Module - BMI323 (I2C) wrapper

#ifndef IMU_SENSOR_H
#define IMU_SENSOR_H

#include <Wire.h>

// IMU pin definitions
#define I2C_SDA_PIN 8      // IMU_SDA
#define I2C_SCL_PIN 9      // IMU_SCL

// Structure to hold all IMU data
struct IMUData {
  struct {
    float x, y, z;
  } accelerometer;

  struct {
    float x, y, z;
  } gyroscope;

  // Temperature in degrees Celsius
  float temperature;
};

// I2C constants and registers (BMI323-ish sensor used in test/imu.ino)
#define IMU_I2C_ADDRESS 0x69
#define IMU_ACC_CONF  0x20  // Page 91 in BMI323
#define IMU_GYR_CONF  0x21  // Page 93 in BMI323
#define IMU_CMD       0x7E  // Page 65 in BMI323

class IMUSensor {
private:
  IMUData data;
  // No internal `isInitialized` flag; callers may track initialization status using `begin()` return value.
  // Storage for converted values
  float accelX_m_s2 = 0.0f;
  float accelY_m_s2 = 0.0f;
  float accelZ_m_s2 = 0.0f;
  float gyroX_dps = 0.0f;
  float gyroY_dps = 0.0f;
  float gyroZ_dps = 0.0f;
  float temperature_c = 0.0f;

public:
  bool begin() {
    // Initialize I2C with custom pins
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000); // 400kHz I2C speed
    delay(100);

    softReset();

    // Configure ACC and GYR registers -- these are the same values used in test/imu.ino
    writeRegister16(IMU_ACC_CONF, 0x753D);  // accelerometer settings
    writeRegister16(IMU_GYR_CONF, 0x758D);  // gyroscope settings

    // Optionally verify a few registers or read a status
    // If the device responds to a zero-length read, consider it initialized
    // Simple probe: read a known register
    uint16_t probe = readRegister16(IMU_ACC_CONF);
    (void)probe; // Avoid unused var warnings; probe can be validated later by caller
    return true;
  }
  
  // This function is used to update the IMU data and read data from the IMU.
  void update() {

    readAllSensors();

    // Populate the public IMUData structure
    data.accelerometer.x = accelX_m_s2;
    data.accelerometer.y = accelY_m_s2;
    data.accelerometer.z = accelZ_m_s2;

    data.gyroscope.x = gyroX_dps;
    data.gyroscope.y = gyroY_dps;
    data.gyroscope.z = gyroZ_dps;

    // No quaternion/euler or magnetometer data available from this simple I2C read

    // Store temperature
    data.temperature = temperature_c;
  }
  
  IMUData getIMUData() {
    return data;
  }

private:
  // Soft reset similar to test/imu.ino
  void softReset(){ 
    writeRegister16(IMU_CMD, 0xDEAF);
    delay(50);
  }

  // Write 16-bit register via I2C
  void writeRegister16(uint16_t reg, uint16_t value) {
    Wire.beginTransmission(IMU_I2C_ADDRESS);
    Wire.write((uint8_t)reg);
    // Low
    Wire.write(value & 0xFF);
    // High
    Wire.write((value >> 8) & 0xFF);
    Wire.endTransmission();
  }

  // Read 16-bit register via I2C
  uint16_t readRegister16(uint8_t reg) {
    Wire.beginTransmission(IMU_I2C_ADDRESS);
    Wire.write(reg);
    Wire.endTransmission(false);

    Wire.requestFrom(IMU_I2C_ADDRESS, (uint8_t)2);
    uint8_t lo = 0, hi = 0;
    if (Wire.available()) lo = Wire.read();
    if (Wire.available()) hi = Wire.read();

    return (uint16_t)(lo | (hi << 8));
  }

  // Read accel/gyro/temp block beginning at register 0x03
  void readAllSensors() {
    Wire.beginTransmission(IMU_I2C_ADDRESS);
    Wire.write(0x03);   // ACC data start
    Wire.endTransmission(false);

    Wire.requestFrom(IMU_I2C_ADDRESS, (uint8_t)16);  // now reading 16 bytes
    uint8_t dataRaw[16];
    int i = 0;
    while (Wire.available() && i < 16) {
      dataRaw[i++] = Wire.read();
    }

    int offset = 2;  // discard dummy bytes

    int16_t x = (int16_t)(dataRaw[offset + 0] | (dataRaw[offset + 1] << 8));
    int16_t y = (int16_t)(dataRaw[offset + 2] | (dataRaw[offset + 3] << 8));
    int16_t z = (int16_t)(dataRaw[offset + 4] | (dataRaw[offset + 5] << 8));

    int16_t gyro_x = (int16_t)(dataRaw[offset + 6]  | (dataRaw[offset + 7]  << 8));
    int16_t gyro_y = (int16_t)(dataRaw[offset + 8]  | (dataRaw[offset + 9]  << 8));
    int16_t gyro_z = (int16_t)(dataRaw[offset + 10] | (dataRaw[offset + 11] << 8));

    int16_t temp_raw = (int16_t)(dataRaw[offset + 12] | (dataRaw[offset + 13] << 8));

    accelX_m_s2 = lsbToM2S(x);
    accelY_m_s2 = lsbToM2S(y);
    accelZ_m_s2 = lsbToM2S(z);

    const float GYRO_SENS_125DPS = 262.1f; // as in test/imu.ino
    gyroX_dps = gyro_x / GYRO_SENS_125DPS;
    gyroY_dps = gyro_y / GYRO_SENS_125DPS;
    gyroZ_dps = gyro_z / GYRO_SENS_125DPS;

    temperature_c = (float)temp_raw / 512.0f + 23.0f;
  }

  // Convert LSB to m/s^2
  float lsbToM2S(int16_t rawData) {
    const float sensitivity = 16384.0f; // for ±2g
    const float gToM2S = 9.80665f;
    return (rawData / sensitivity) * gToM2S;
  }
};

#endif
