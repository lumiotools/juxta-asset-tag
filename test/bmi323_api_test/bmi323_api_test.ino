// BMI323 Sensor Test using Bosch SensorAPI
// This test verifies the BMI323 sensor initialization and data reading using the official Bosch API
// 
// Hardware Connections:
// - BMI323 SDA → GPIO 0 (I2C_SDA_PIN)
// - BMI323 SCL → GPIO 1 (I2C_SCL_PIN)
// - BMI323 INT1 → GPIO 22 (optional, for interrupt testing)
// - I2C Address: 0x69

#include <Arduino.h>
#include <Wire.h>
#include "../../libs/BMI3XY_SensorAPI-main/bmi323.h"

// Note: The library source files are compiled via separate .cpp wrapper files
// (bmi3_library.cpp and bmi323_library.cpp) in this same directory.
// Arduino IDE will automatically compile them as separate translation units,
// avoiding redefinition conflicts.

// I2C pin definitions (ESP32-C6)
#define I2C_SDA_PIN 0
#define I2C_SCL_PIN 1
#define IMU_I2C_ADDRESS 0x69

// Test state
bool sensorInitialized = false;
struct bmi3_dev bmi3Device = { 0 };

// I2C interface wrapper functions for Bosch API
extern "C" {
  int8_t bmi3_i2c_read_wrapper(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr);
  int8_t bmi3_i2c_write_wrapper(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr);
  void bmi3_delay_us_wrapper(uint32_t period, void *intf_ptr);
}

// Implementation of wrapper functions (outside extern "C" to ensure C++ linkage)
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

void setup() {
  Serial.begin(115200);
  delay(2000); // Wait for serial monitor
  
  Serial.println("\n========================================");
  Serial.println("BMI323 Sensor API Test");
  Serial.println("========================================\n");
  
  // Initialize I2C
  Serial.print("Initializing I2C (SDA=");
  Serial.print(I2C_SDA_PIN);
  Serial.print(", SCL=");
  Serial.print(I2C_SCL_PIN);
  Serial.print(", Address=0x");
  Serial.print(IMU_I2C_ADDRESS, HEX);
  Serial.println(")...");
  
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000); // 400kHz I2C speed
  delay(100);
  
  // Check if device is present
  Wire.beginTransmission(IMU_I2C_ADDRESS);
  uint8_t error = Wire.endTransmission();
  if (error != 0) {
    Serial.println("ERROR: BMI323 device not found on I2C bus!");
    Serial.println("Check wiring and I2C address.");
    return;
  }
  
  Serial.println("Device detected on I2C bus.");
  
  // Initialize Bosch API device structure
  Serial.println("\nInitializing Bosch BMI323 API...");
  memset(&bmi3Device, 0, sizeof(bmi3Device));
  
  bmi3Device.intf = BMI3_I2C_INTF;
  uint8_t dev_addr = IMU_I2C_ADDRESS;
  bmi3Device.intf_ptr = &dev_addr;
  bmi3Device.read = bmi3_i2c_read_wrapper;
  bmi3Device.write = bmi3_i2c_write_wrapper;
  bmi3Device.delay_us = bmi3_delay_us_wrapper;
  bmi3Device.read_write_len = 8;
  
  // Initialize BMI323
  int8_t rslt = bmi323_init(&bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: BMI323 initialization failed! Error code: ");
    Serial.println(rslt);
    printErrorCode(rslt);
    return;
  }
  
  Serial.print("BMI323 initialized successfully! Chip ID: 0x");
  Serial.println(bmi3Device.chip_id, HEX);
  
  // Configure accelerometer and gyroscope
  Serial.println("\nConfiguring sensors...");
  
  struct bmi3_sens_config config[2] = { { 0 } };
  config[0].type = BMI323_ACCEL;
  config[1].type = BMI323_GYRO;
  
  rslt = bmi323_get_sensor_config(config, 2, &bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: Failed to get sensor config: ");
    Serial.println(rslt);
    return;
  }
  
  // Configure accelerometer: Normal mode, 100Hz ODR, ±2g range
  config[0].cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;
  config[0].cfg.acc.odr = BMI3_ACC_ODR_100HZ;
  config[0].cfg.acc.range = BMI3_ACC_RANGE_2G;
  config[0].cfg.acc.bwp = BMI3_ACC_BW_ODR_QUARTER;
  config[0].cfg.acc.avg_num = BMI3_ACC_AVG4;
  
  // Configure gyroscope: Normal mode, 100Hz ODR, ±125dps range
  config[1].cfg.gyr.gyr_mode = BMI3_GYR_MODE_NORMAL;
  config[1].cfg.gyr.odr = BMI3_GYR_ODR_100HZ;
  config[1].cfg.gyr.range = BMI3_GYR_RANGE_125DPS;
  config[1].cfg.gyr.bwp = BMI3_GYR_BW_ODR_HALF;
  config[1].cfg.gyr.avg_num = BMI3_GYR_AVG1;
  
  rslt = bmi323_set_sensor_config(config, 2, &bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: Failed to set sensor config: ");
    Serial.println(rslt);
    return;
  }
  
  Serial.println("Sensor configuration set successfully.");
  
  // Sensors are enabled by setting their mode (acc_mode/gyr_mode) above
  Serial.println("Sensors enabled successfully.");
  delay(100); // Allow sensor to stabilize
  
  sensorInitialized = true;
  
  Serial.println("\n========================================");
  Serial.println("Test Setup Complete!");
  Serial.println("Reading sensor data every 500ms...");
  Serial.println("========================================\n");
}

void loop() {
  if (!sensorInitialized) {
    delay(1000);
    return;
  }
  
  // Read sensor data
  struct bmi3_sensor_data sensor_data[2] = { { 0 } };
  sensor_data[0].type = BMI323_ACCEL;
  sensor_data[1].type = BMI323_GYRO;
  
  int8_t rslt = bmi323_get_sensor_data(sensor_data, 2, &bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR reading sensor data: ");
    Serial.println(rslt);
    delay(500);
    return;
  }
  
  // Convert accelerometer data (LSB to m/s^2)
  const float accelSensitivity = 16384.0f; // LSB/g for ±2g
  const float gToM2S = 9.80665f;
  
  float accelX = ((float)sensor_data[0].sens_data.acc.x / accelSensitivity) * gToM2S;
  float accelY = ((float)sensor_data[0].sens_data.acc.y / accelSensitivity) * gToM2S;
  float accelZ = ((float)sensor_data[0].sens_data.acc.z / accelSensitivity) * gToM2S;
  
  // Convert gyroscope data (LSB to dps)
  const float gyroSensitivity = 262.144f; // LSB/dps for ±125dps
  
  float gyroX = (float)sensor_data[1].sens_data.gyr.x / gyroSensitivity;
  float gyroY = (float)sensor_data[1].sens_data.gyr.y / gyroSensitivity;
  float gyroZ = (float)sensor_data[1].sens_data.gyr.z / gyroSensitivity;
  
  // Read temperature
  struct bmi3_sensor_data temp_sensor = { 0 };
  temp_sensor.type = BMI323_TEMP;
  int8_t temp_rslt = bmi323_get_sensor_data(&temp_sensor, 1, &bmi3Device);
  float temperature = 0.0f;
  if (temp_rslt == BMI323_OK) {
    temperature = (float)temp_sensor.sens_data.temp.temp_data / 512.0f + 23.0f;
  }
  
  // Print sensor data
  Serial.println("--- Sensor Data ---");
  Serial.print("Accelerometer [m/s²]: X=");
  Serial.print(accelX, 3);
  Serial.print("  Y=");
  Serial.print(accelY, 3);
  Serial.print("  Z=");
  Serial.println(accelZ, 3);
  
  Serial.print("Gyroscope [dps]:      X=");
  Serial.print(gyroX, 3);
  Serial.print("  Y=");
  Serial.print(gyroY, 3);
  Serial.print("  Z=");
  Serial.println(gyroZ, 3);
  
  Serial.print("Temperature [°C]:     ");
  Serial.println(temperature, 2);
  
  // Calculate magnitude for motion detection test
  float accelMagnitude = sqrt(accelX*accelX + accelY*accelY + accelZ*accelZ);
  Serial.print("Accel Magnitude [m/s²]: ");
  Serial.print(accelMagnitude, 3);
  Serial.print(" (");
  Serial.print(accelMagnitude / gToM2S, 3);
  Serial.println("g)");
  
  Serial.println();
  
  delay(500);
}

void printErrorCode(int8_t error) {
  Serial.print("Error description: ");
  switch (error) {
    case BMI3_OK:
      Serial.println("Success");
      break;
    case BMI3_E_NULL_PTR:
      Serial.println("Null pointer error");
      break;
    case BMI3_E_COM_FAIL:
      Serial.println("Communication failure");
      break;
    case BMI3_E_DEV_NOT_FOUND:
      Serial.println("Device not found");
      break;
    case BMI3_E_INVALID_SENSOR:
      Serial.println("Invalid sensor");
      break;
    case BMI3_E_INVALID_INT_PIN:
      Serial.println("Invalid interrupt pin");
      break;
    case BMI3_E_ACC_INVALID_CFG:
      Serial.println("Invalid accelerometer configuration");
      break;
    case BMI3_E_GYRO_INVALID_CFG:
      Serial.println("Invalid gyroscope configuration");
      break;
    case BMI3_E_INVALID_INPUT:
      Serial.println("Invalid input");
      break;
    default:
      Serial.print("Unknown error code: ");
      Serial.println(error);
      break;
  }
}

