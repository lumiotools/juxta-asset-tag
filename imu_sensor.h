// IMU Sensor Module - BMI323 (I2C) wrapper
// Integrated with Bosch BMI323 SensorAPI

#ifndef IMU_SENSOR_H
#define IMU_SENSOR_H

#include <Wire.h>
// Include Bosch BMI323 library from local libs folder
#include "libs/BMI3XY_SensorAPI-main/bmi323.h"

// IMU pin definitions
#define I2C_SDA_PIN 0     // IMU_SDA
#define I2C_SCL_PIN 1     // IMU_SCL

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

// I2C constants
#define IMU_I2C_ADDRESS 0x69

// Forward declarations for I2C interface functions
extern "C" {
  int8_t bmi3_i2c_read_wrapper(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr);
  int8_t bmi3_i2c_write_wrapper(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr);
  void bmi3_delay_us_wrapper(uint32_t period, void *intf_ptr);
}

class IMUSensor {
private:
  IMUData data;
  struct bmi3_dev bmi3Device;  // Bosch API device structure
  bool boschApiInitialized = false;
  
  // Storage for converted values
  float accelX_m_s2 = 0.0f;
  float accelY_m_s2 = 0.0f;
  float accelZ_m_s2 = 0.0f;
  float gyroX_dps = 0.0f;
  float gyroY_dps = 0.0f;
  float gyroZ_dps = 0.0f;
  float temperature_c = 0.0f;

  // Initialize Bosch API device structure with I2C interface
  bool initializeBoschAPI() {
    // Initialize I2C with custom pins
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000); // 400kHz I2C speed
    delay(100);

    // Initialize Bosch API device structure
    memset(&bmi3Device, 0, sizeof(bmi3Device));
    
    // Set interface type
    bmi3Device.intf = BMI3_I2C_INTF;
    
    // Set I2C address
    uint8_t dev_addr = IMU_I2C_ADDRESS;
    bmi3Device.intf_ptr = &dev_addr;
    
    // Set function pointers
    bmi3Device.read = bmi3_i2c_read_wrapper;
    bmi3Device.write = bmi3_i2c_write_wrapper;
    bmi3Device.delay_us = bmi3_delay_us_wrapper;
    
    // Set read/write length
    bmi3Device.read_write_len = 8;
    
    // Initialize BMI323 using Bosch API
    int8_t rslt = bmi323_init(&bmi3Device);
    if (rslt != BMI323_OK) {
      Serial.print("ERROR: BMI323 initialization failed with code: ");
      Serial.println(rslt);
      return false;
    }
    
    Serial.println("BMI323 initialized successfully using Bosch API");
    boschApiInitialized = true;
    return true;
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

    // Initialize Bosch API (which includes I2C connection check and chip ID verification)
    if (!initializeBoschAPI()) {
      Serial.println("ERROR: BMI323 initialization failed!");
      Serial.println("Check: 1) I2C wiring (SDA/SCL) 2) Power supply 3) Chip type (must be BMI323)");
      return false;
    }
    
    // Verify chip ID (should be 0x43 for BMI323)
    Serial.print("BMI323 detected! Chip ID: 0x");
    Serial.print(bmi3Device.chip_id, HEX);
    if (bmi3Device.chip_id == 0x43) {
      Serial.println(" (valid BMI323)");
    } else {
      Serial.println(" (WARNING: unexpected chip ID!)");
    }

    // Configure accelerometer and gyroscope using Bosch API
    struct bmi3_sens_config config[2] = { { 0 } };
    
    config[0].type = BMI323_ACCEL;
    config[1].type = BMI323_GYRO;
    
    // Get default configurations
    int8_t rslt = bmi323_get_sensor_config(config, 2, &bmi3Device);
    if (rslt != BMI323_OK) {
      Serial.print("ERROR: Failed to get sensor config: ");
      Serial.println(rslt);
      return false;
    }
    
    // Configure accelerometer: Normal mode, 100Hz ODR, ±2g range
    config[0].cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;  // Enable accel by setting mode
    config[0].cfg.acc.odr = BMI3_ACC_ODR_100HZ;
    config[0].cfg.acc.range = BMI3_ACC_RANGE_2G;
    config[0].cfg.acc.bwp = BMI3_ACC_BW_ODR_QUARTER;
    config[0].cfg.acc.avg_num = BMI3_ACC_AVG4;
    
    // Configure gyroscope: Normal mode, 100Hz ODR, ±125dps range
    config[1].cfg.gyr.gyr_mode = BMI3_GYR_MODE_NORMAL;  // Enable gyro by setting mode
    config[1].cfg.gyr.odr = BMI3_GYR_ODR_100HZ;
    config[1].cfg.gyr.range = BMI3_GYR_RANGE_125DPS;
    config[1].cfg.gyr.bwp = BMI3_GYR_BW_ODR_HALF;
    config[1].cfg.gyr.avg_num = BMI3_GYR_AVG1;
    
    // Set configurations
    rslt = bmi323_set_sensor_config(config, 2, &bmi3Device);
    if (rslt != BMI323_OK) {
      Serial.print("ERROR: Failed to set sensor config: ");
      Serial.println(rslt);
      return false;
    }
    
    // Sensors are enabled by setting their mode (acc_mode/gyr_mode) above
    // No need to call bmi323_select_sensor for basic accel/gyro
    
    delay(100); // Allow sensor to stabilize
    
    Serial.println("IMU sensor initialized successfully");
    return true;
  }
  
  // Get Bosch API device structure (for motion detection configuration)
  struct bmi3_dev* getBoschDevice() {
    if (boschApiInitialized) {
      return &bmi3Device;
    }
    return nullptr;
  }
  
  // Check if Bosch API is initialized
  bool isBoschApiInitialized() {
    return boschApiInitialized;
  }
  
  // This function is used to update the IMU data and read data from the IMU.
  void update() {
    if (!boschApiInitialized) {
      return;
    }

    readAllSensors();

    // Populate the public IMUData structure
    data.accelerometer.x = accelX_m_s2;
    data.accelerometer.y = accelY_m_s2;
    data.accelerometer.z = accelZ_m_s2;

    data.gyroscope.x = gyroX_dps;
    data.gyroscope.y = gyroY_dps;
    data.gyroscope.z = gyroZ_dps;

    // Temperature not stored (disabled for CSV format)
    // data.temperature = temperature_c;
  }
  
  IMUData getIMUData() {
    return data;
  }

private:
  // Read accel/gyro/temp using Bosch API
  void readAllSensors() {
    if (!boschApiInitialized) {
      return;
    }

    struct bmi3_sensor_data sensor_data[2] = { { 0 } };
    
    sensor_data[0].type = BMI323_ACCEL;
    sensor_data[1].type = BMI323_GYRO;
    
    int8_t rslt = bmi323_get_sensor_data(sensor_data, 2, &bmi3Device);
    if (rslt != BMI323_OK) {
      // Error reading sensor data
      return;
    }
    
    // Convert accelerometer data (LSB to m/s^2)
    // For ±2g range: 16384 LSB/g
    const float accelSensitivity = 16384.0f; // LSB/g for ±2g
    const float gToM2S = 9.80665f;
    
    accelX_m_s2 = ((float)sensor_data[0].sens_data.acc.x / accelSensitivity) * gToM2S;
    accelY_m_s2 = ((float)sensor_data[0].sens_data.acc.y / accelSensitivity) * gToM2S;
    accelZ_m_s2 = ((float)sensor_data[0].sens_data.acc.z / accelSensitivity) * gToM2S;
    
    // Convert gyroscope data (LSB to dps)
    // For ±125dps range: 262.144 LSB/dps
    const float gyroSensitivity = 262.144f; // LSB/dps for ±125dps
    
    gyroX_dps = (float)sensor_data[1].sens_data.gyr.x / gyroSensitivity;
    gyroY_dps = (float)sensor_data[1].sens_data.gyr.y / gyroSensitivity;
    gyroZ_dps = (float)sensor_data[1].sens_data.gyr.z / gyroSensitivity;
    
    // Read temperature if needed (as part of sensor data)
    struct bmi3_sensor_data temp_sensor = { 0 };
    temp_sensor.type = BMI323_TEMP;
    int8_t temp_rslt = bmi323_get_sensor_data(&temp_sensor, 1, &bmi3Device);
    if (temp_rslt == BMI323_OK) {
      temperature_c = (float)temp_sensor.sens_data.temp.temp_data / 512.0f + 23.0f;
    }
  }
};

// I2C interface wrapper functions for Bosch API
// These functions bridge Arduino Wire library with Bosch API

extern "C" {
  int8_t bmi3_i2c_read_wrapper(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr) {
    uint8_t device_addr = *(uint8_t*)intf_ptr;
    
    Wire.beginTransmission(device_addr);
    Wire.write(reg_addr);
    if (Wire.endTransmission(false) != 0) {
      return -1; // Communication error
    }
    
    Wire.requestFrom(device_addr, (uint8_t)len);
    uint32_t i = 0;
    while (Wire.available() && i < len) {
      reg_data[i++] = Wire.read();
    }
    
    if (i != len) {
      return -1; // Didn't read all bytes
    }
    
    return 0; // Success
  }
  
  int8_t bmi3_i2c_write_wrapper(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr) {
    uint8_t device_addr = *(uint8_t*)intf_ptr;
    
    Wire.beginTransmission(device_addr);
    Wire.write(reg_addr);
    for (uint32_t i = 0; i < len; i++) {
      Wire.write(reg_data[i]);
    }
    
    if (Wire.endTransmission() != 0) {
      return -1; // Communication error
    }
    
    return 0; // Success
  }
  
  void bmi3_delay_us_wrapper(uint32_t period, void *intf_ptr) {
    (void)intf_ptr; // Unused
    delayMicroseconds(period);
  }
}

#endif
