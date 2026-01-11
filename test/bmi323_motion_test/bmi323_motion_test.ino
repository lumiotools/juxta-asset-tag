// BMI323 Motion Detection Test using Bosch SensorAPI
// This test verifies motion detection features (any-motion and no-motion)
// 
// Hardware Connections:
// - BMI323 SDA → GPIO 0 (I2C_SDA_PIN)
// - BMI323 SCL → GPIO 1 (I2C_SCL_PIN)
// - BMI323 INT1 → GPIO 22 (for interrupt testing)
// - I2C Address: 0x69

#include <Arduino.h>
#include <Wire.h>
#include "../../libs/BMI3XY_SensorAPI-main/bmi323.h"
#include "../power_latch.h"

// I2C pin definitions (ESP32-C6)
#define I2C_SDA_PIN 0
#define I2C_SCL_PIN 1
#define IMU_I2C_ADDRESS 0x69
#define MOTION_INT_PIN 22

// Test state
bool sensorInitialized = false;
bool motionDetectionConfigured = false;
struct bmi3_dev bmi3Device = { 0 };

// Motion detection configuration
#define ANY_MOTION_SLOPE_THRES 9      // Slope threshold (0-4095)
#define NO_MOTION_SLOPE_THRES 9       // Slope threshold (0-4095)
#define MOTION_HYSTERESIS 5            // Hysteresis (0-1023)
#define MOTION_WAIT_TIME 5             // Wait time (0-7)
#define ANY_MOTION_DURATION 1          // Duration in samples (immediate)
#define NO_MOTION_DURATION 15000       // 5 minutes at 50Hz (300s * 50)

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
    return -1;
  }
  
  Wire.requestFrom(device_addr, (uint8_t)len);
  uint32_t i = 0;
  while (Wire.available() && i < len) {
    reg_data[i++] = Wire.read();
  }
  
  if (i != len) {
    return -1;
  }
  
  return 0;
}

int8_t bmi3_i2c_write_wrapper(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr) {
  uint8_t device_addr = *(uint8_t*)intf_ptr;
  
  Wire.beginTransmission(device_addr);
  Wire.write(reg_addr);
  for (uint32_t i = 0; i < len; i++) {
    Wire.write(reg_data[i]);
  }
  
  if (Wire.endTransmission() != 0) {
    return -1;
  }
  
  return 0;
}

void bmi3_delay_us_wrapper(uint32_t period, void *intf_ptr) {
  (void)intf_ptr;
  delayMicroseconds(period);
}

// Interrupt handler
volatile bool motionInterruptFlag = false;
void IRAM_ATTR handleMotionInterrupt() {
  motionInterruptFlag = true;
}

void setup() {
  Serial.begin(115200);
  // No delay on boot - start immediately
  
  // Initialize power latch (set HIGH on boot)
  initPowerLatch();
  
  Serial.println("\n========================================");
  Serial.println("BMI323 Motion Detection Test");
  Serial.println("========================================\n");
  
  // Initialize I2C
  Serial.print("Initializing I2C (SDA=");
  Serial.print(I2C_SDA_PIN);
  Serial.print(", SCL=");
  Serial.print(I2C_SCL_PIN);
  Serial.println(")...");
  
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);
  delay(100);
  
  // Check device
  Wire.beginTransmission(IMU_I2C_ADDRESS);
  if (Wire.endTransmission() != 0) {
    Serial.println("ERROR: BMI323 device not found!");
    return;
  }
  Serial.println("Device detected.");
  
  // Initialize Bosch API
  Serial.println("\nInitializing Bosch BMI323 API...");
  memset(&bmi3Device, 0, sizeof(bmi3Device));
  
  bmi3Device.intf = BMI3_I2C_INTF;
  uint8_t dev_addr = IMU_I2C_ADDRESS;
  bmi3Device.intf_ptr = &dev_addr;
  bmi3Device.read = bmi3_i2c_read_wrapper;
  bmi3Device.write = bmi3_i2c_write_wrapper;
  bmi3Device.delay_us = bmi3_delay_us_wrapper;
  bmi3Device.read_write_len = 8;
  
  int8_t rslt = bmi323_init(&bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: Initialization failed! Code: ");
    Serial.println(rslt);
    return;
  }
  
  Serial.print("BMI323 initialized! Chip ID: 0x");
  Serial.println(bmi3Device.chip_id, HEX);
  
  // Configure accelerometer (required for motion detection)
  Serial.println("\nConfiguring accelerometer...");
  struct bmi3_sens_config config = { 0 };
  config.type = BMI323_ACCEL;
  
  rslt = bmi323_get_sensor_config(&config, 1, &bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: Failed to get accel config: ");
    Serial.println(rslt);
    return;
  }
  
  config.cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;
  config.cfg.acc.odr = BMI3_ACC_ODR_50HZ;  // 50Hz for motion detection
  config.cfg.acc.range = BMI3_ACC_RANGE_2G;
  config.cfg.acc.bwp = BMI3_ACC_BW_ODR_QUARTER;
  config.cfg.acc.avg_num = BMI3_ACC_AVG4;
  
  rslt = bmi323_set_sensor_config(&config, 1, &bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: Failed to set accel config: ");
    Serial.println(rslt);
    return;
  }
  
  // Accelerometer is enabled by setting acc_mode above
  Serial.println("Accelerometer configured and enabled.");
  
  // Configure motion detection
  Serial.println("\nConfiguring motion detection...");
  
  struct bmi3_sens_config motion_config[2] = { { 0 } };
  motion_config[0].type = BMI323_ANY_MOTION;
  motion_config[1].type = BMI323_NO_MOTION;
  
  rslt = bmi323_get_sensor_config(motion_config, 2, &bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: Failed to get motion config: ");
    Serial.println(rslt);
    return;
  }
  
  // Configure any-motion
  motion_config[0].cfg.any_motion.slope_thres = ANY_MOTION_SLOPE_THRES;
  motion_config[0].cfg.any_motion.hysteresis = MOTION_HYSTERESIS;
  motion_config[0].cfg.any_motion.duration = ANY_MOTION_DURATION;
  motion_config[0].cfg.any_motion.acc_ref_up = 1;
  motion_config[0].cfg.any_motion.wait_time = MOTION_WAIT_TIME;
  
  // Configure no-motion
  motion_config[1].cfg.no_motion.slope_thres = NO_MOTION_SLOPE_THRES;
  motion_config[1].cfg.no_motion.hysteresis = MOTION_HYSTERESIS;
  motion_config[1].cfg.no_motion.duration = NO_MOTION_DURATION;
  motion_config[1].cfg.no_motion.acc_ref_up = 1;
  motion_config[1].cfg.no_motion.wait_time = MOTION_WAIT_TIME;
  
  rslt = bmi323_set_sensor_config(motion_config, 2, &bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: Failed to set motion config: ");
    Serial.println(rslt);
    return;
  }
  
  Serial.print("Any-motion: slope_thres=");
  Serial.print(ANY_MOTION_SLOPE_THRES);
  Serial.print(", duration=");
  Serial.println(ANY_MOTION_DURATION);
  
  Serial.print("No-motion: slope_thres=");
  Serial.print(NO_MOTION_SLOPE_THRES);
  Serial.print(", duration=");
  Serial.print(NO_MOTION_DURATION);
  Serial.println(" samples (5 minutes at 50Hz)");
  
  // Enable motion features
  struct bmi3_feature_enable feature = { 0 };
  feature.any_motion_x_en = BMI323_ENABLE;
  feature.any_motion_y_en = BMI323_ENABLE;
  feature.any_motion_z_en = BMI323_ENABLE;
  feature.no_motion_x_en = BMI323_ENABLE;
  feature.no_motion_y_en = BMI323_ENABLE;
  feature.no_motion_z_en = BMI323_ENABLE;
  
  rslt = bmi323_select_sensor(&feature, &bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: Failed to enable motion features: ");
    Serial.println(rslt);
    return;
  }
  
  Serial.println("Motion features enabled.");
  
  // Configure INT1 pin
  Serial.println("\nConfiguring INT1 pin...");
  struct bmi3_int_pin_config int_cfg = { 0 };
  int_cfg.pin_type = BMI3_INT1;
  int_cfg.int_latch = BMI3_INT_NON_LATCH;
  int_cfg.pin_cfg[0].lvl = BMI3_INT_ACTIVE_HIGH;
  int_cfg.pin_cfg[0].od = BMI3_INT_PUSH_PULL;
  int_cfg.pin_cfg[0].output_en = BMI3_INT_OUTPUT_ENABLE;
  
  rslt = bmi3_set_int_pin_config(&int_cfg, &bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: Failed to configure INT1: ");
    Serial.println(rslt);
    return;
  }
  
  Serial.println("INT1 configured: active HIGH, push-pull, non-latched");
  
  // Map interrupts to INT1
  struct bmi3_map_int map_int = { 0 };
  map_int.any_motion_out = BMI3_INT1;
  map_int.no_motion_out = BMI3_INT1;
  
  rslt = bmi323_map_interrupt(map_int, &bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: Failed to map interrupts: ");
    Serial.println(rslt);
    return;
  }
  
  Serial.println("Interrupts mapped to INT1");
  
  // Setup interrupt pin on ESP32
  Serial.print("\nSetting up interrupt on GPIO ");
  Serial.print(MOTION_INT_PIN);
  Serial.println("...");
  pinMode(MOTION_INT_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(MOTION_INT_PIN), handleMotionInterrupt, RISING);
  Serial.println("Interrupt handler attached.");
  
  sensorInitialized = true;
  motionDetectionConfigured = true;
  
  Serial.println("\n========================================");
  Serial.println("Motion Detection Test Ready!");
  Serial.println("========================================");
  Serial.println("Instructions:");
  Serial.println("1. Move the device to trigger any-motion interrupt");
  Serial.println("2. Keep device still for 5 minutes to trigger no-motion interrupt");
  Serial.println("3. Monitor interrupt status below");
  Serial.println("========================================\n");
}

void loop() {
  // Update button handler (check for long press to power off)
  updateButtonHandler();
  
  if (!sensorInitialized || !motionDetectionConfigured) {
    delay(1000);
    return;
  }
  
  // Check for interrupt
  if (motionInterruptFlag) {
    motionInterruptFlag = false;
    
    // Small delay to allow sensor to update interrupt status register
    delay(500);
    
    // Read interrupt status with retry logic
    uint16_t int_status = 0;
    int8_t rslt = -2; // BMI3_E_COM_FAIL
    int retry_count = 0;
    const int max_retries = 3;
    
    // Retry reading interrupt status in case of communication failure
    while (rslt != BMI323_OK && retry_count < max_retries) {
      rslt = bmi323_get_int1_status(&int_status, &bmi3Device);
      if (rslt != BMI323_OK) {
        retry_count++;
        if (retry_count < max_retries) {
          delay(1); // Small delay before retry
        }
      }
    }
    
    if (rslt == BMI323_OK) {
      Serial.println("\n*** INTERRUPT DETECTED ***");
      Serial.print("Interrupt Status: 0x");
      Serial.println(int_status, HEX);
      
      if (int_status & BMI3_INT_STATUS_ANY_MOTION) {
        Serial.println(">>> ANY-MOTION INTERRUPT <<<");
        Serial.println("Device movement detected!");
      }
      
      if (int_status & BMI3_INT_STATUS_NO_MOTION) {
        Serial.println(">>> NO-MOTION INTERRUPT <<<");
        Serial.println("Device has been still for 5 minutes!");
      }
      
      // Check for other interrupt types for debugging
      if (int_status & BMI3_INT_STATUS_ERR) {
        Serial.println("WARNING: Error status interrupt detected!");
      }
      
      Serial.println();
    } else {
      Serial.print("ERROR reading interrupt status (error ");
      Serial.print(rslt);
      Serial.print(") after ");
      Serial.print(retry_count);
      Serial.println(" retries");
      Serial.print("Interrupt pin state: ");
      Serial.println(digitalRead(MOTION_INT_PIN) ? "HIGH" : "LOW");
    }
  }
  
  // Read and display accelerometer data for reference
  static unsigned long lastRead = 0;
  if (millis() - lastRead > 1000) {  // Update every second
    lastRead = millis();
    
    struct bmi3_sensor_data sensor_data = { 0 };
    sensor_data.type = BMI323_ACCEL;
    
    int8_t rslt = bmi323_get_sensor_data(&sensor_data, 1, &bmi3Device);
    if (rslt == BMI323_OK) {
      const float accelSensitivity = 16384.0f;
      const float gToM2S = 9.80665f;
      
      float accelX = ((float)sensor_data.sens_data.acc.x / accelSensitivity) * gToM2S;
      float accelY = ((float)sensor_data.sens_data.acc.y / accelSensitivity) * gToM2S;
      float accelZ = ((float)sensor_data.sens_data.acc.z / accelSensitivity) * gToM2S;
      
      float magnitude = sqrt(accelX*accelX + accelY*accelY + accelZ*accelZ);
      
      Serial.print("Accel [m/s²]: X=");
      Serial.print(accelX, 2);
      Serial.print(" Y=");
      Serial.print(accelY, 2);
      Serial.print(" Z=");
      Serial.print(accelZ, 2);
      Serial.print(" | Magnitude=");
      Serial.print(magnitude, 2);
      Serial.print(" (");
      Serial.print(magnitude / gToM2S, 2);
      Serial.println("g)");
    }
  }
  
  delay(10); // Small delay to prevent overwhelming serial output
}

