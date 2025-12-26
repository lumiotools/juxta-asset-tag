// IMU Sensor Module - BMI323 (I2C) wrapper

#ifndef IMU_SENSOR_H
#define IMU_SENSOR_H

#include <Wire.h>

// IMU pin definitions
#define I2C_SDA_PIN D4     // IMU_SDA
#define I2C_SCL_PIN D5     // IMU_SCL

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

  // Check if IMU device is connected on I2C bus
  bool checkDeviceConnection() {
    Wire.beginTransmission(IMU_I2C_ADDRESS);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
      // Device responded, verify by reading a register
      uint16_t chipId = readRegister16(0x00); // Try to read chip ID register (if available)
      // If we got here without I2C error, device is present
      return true;
    } else {
      // Device not found (NACK)
      return false;
    }
  }

public:
  bool begin() {
    Serial.print("Initializing IMU sensor (I2C address 0x");
    Serial.print(IMU_I2C_ADDRESS, HEX);
    Serial.print(", SDA=");
    Serial.print(I2C_SDA_PIN);
    Serial.print(", SCL=");
    Serial.print(I2C_SCL_PIN);
    Serial.println(")...");

    // Initialize I2C with custom pins
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000); // 400kHz I2C speed
    delay(100);

    // Check if device is connected
    if (!checkDeviceConnection()) {
      Serial.println("ERROR: IMU device not detected on I2C bus!");
      Serial.print("Expected address: 0x");
      Serial.println(IMU_I2C_ADDRESS, HEX);
      Serial.println("Check: 1) I2C wiring (SDA/SCL) 2) Power supply 3) Device address");
      return false;
    }

    Serial.println("IMU device detected, configuring...");
    softReset();

    // Configure ACC and GYR registers -- these are the same values used in test/imu.ino
    writeRegister16(IMU_ACC_CONF, 0x753D);  // accelerometer settings
    writeRegister16(IMU_GYR_CONF, 0x758D);  // gyroscope settings
    delay(500); 

    // Verify configuration by reading back
    uint16_t accConf = readRegister16(IMU_ACC_CONF);
    uint16_t gyrConf = readRegister16(IMU_GYR_CONF);

    Serial.print("ACC Conf: ");
    Serial.println(accConf, HEX);
    Serial.print("GYR Conf: ");
    Serial.println(gyrConf, HEX);

    // if (accConf == 0x0000 && gyrConf == 0x0000) {
    //   Serial.println("WARNING: IMU register readback failed - device may not be responding correctly");
    //   return false;
    // }

    Serial.println("IMU sensor initialized successfully");
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

    // Temperature not stored (disabled for CSV format)
    // data.temperature = temperature_c;
  }
  
  IMUData getIMUData() {
    return data;
  }
  
  // Public access to register read/write for motion detection configuration
  // These methods allow motion_sleep_manager to configure interrupts
  void writeRegister16Public(uint8_t reg, uint16_t value) {
    writeRegister16(reg, value);
  }
  
  uint16_t readRegister16Public(uint8_t reg) {
    return readRegister16(reg);
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
