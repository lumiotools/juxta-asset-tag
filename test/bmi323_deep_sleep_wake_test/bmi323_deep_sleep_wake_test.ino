// BMI323 Deep Sleep and Wake Test
// This test verifies deep sleep functionality with motion-based wake-up
// Uses Bosch BMI323 SensorAPI directly from libs folder
// 
// Hardware Connections:
// - BMI323 SDA → GPIO 0 (I2C_SDA_PIN)
// - BMI323 SCL → GPIO 1 (I2C_SCL_PIN)
// - BMI323 INT1 → GPIO 22 (MOTION_INT_PIN)
// - I2C Address: 0x69
//
// Test Flow:
// 1. Initialize BMI323 sensor using Bosch API
// 2. Configure motion detection interrupts
// 3. Enter deep sleep (wake on motion interrupt)
// 4. Wake up from motion interrupt and print message

#include <Arduino.h>
#include <Wire.h>
#include "../../libs/BMI3XY_SensorAPI-main/bmi323.h"
#include "esp_sleep.h"
#include "../power_latch.h"

// I2C pin definitions (ESP32-C6)
#define I2C_SDA_PIN 0
#define I2C_SCL_PIN 1
#define IMU_I2C_ADDRESS 0x69
#define MOTION_INT_PIN 22

// Motion detection configuration
#define ANY_MOTION_SLOPE_THRES 9      // Slope threshold (0-4095)
#define NO_MOTION_SLOPE_THRES 9       // Slope threshold (0-4095)
#define MOTION_HYSTERESIS 5            // Hysteresis (0-1023)
#define MOTION_WAIT_TIME 5             // Wait time (0-7)
#define ANY_MOTION_DURATION 1          // Duration in samples (immediate)
#define NO_MOTION_DURATION 15000       // 5 minutes at 50Hz (300s * 50)

// Test state
bool sensorInitialized = false;
bool motionDetectionConfigured = false;
struct bmi3_dev bmi3Device = { 0 };
int wakeCount = 0;

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

// Configure BMI323 interrupts for motion detection
bool configureBMI323Interrupts() {
  Serial.println("Configuring BMI323 interrupts for motion detection...");
  
  int8_t rslt;
  
  // Configure INT1 pin: active HIGH, push-pull, non-latched mode
  struct bmi3_int_pin_config int_cfg = { 0 };
  rslt = bmi323_get_int_pin_config(&int_cfg, &bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: Failed to get INT1 pin config: ");
    Serial.println(rslt);
    return false;
  }
  
  // Modify only the fields we need
  int_cfg.pin_type = BMI3_INT1;
  int_cfg.int_latch = BMI3_INT_NON_LATCH;  // Non-latched (pulsed) for immediate detection
  int_cfg.pin_cfg[0].lvl = BMI3_INT_ACTIVE_HIGH;
  int_cfg.pin_cfg[0].od = BMI3_INT_PUSH_PULL;
  int_cfg.pin_cfg[0].output_en = BMI3_INT_OUTPUT_ENABLE;
  
  rslt = bmi323_set_int_pin_config(&int_cfg, &bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: Failed to configure INT1 pin: ");
    Serial.println(rslt);
    return false;
  }
  Serial.println("INT1 pin configured: active HIGH, push-pull, non-latched");
  
  // Configure accelerometer (required for motion detection)
  struct bmi3_sens_config config[3] = { { 0 } };
  config[0].type = BMI323_ACCEL;
  config[1].type = BMI323_ANY_MOTION;
  config[2].type = BMI323_NO_MOTION;
  
  // Get default configurations
  rslt = bmi323_get_sensor_config(config, 3, &bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: Failed to get sensor config: ");
    Serial.println(rslt);
    return false;
  }
  
  // Configure accelerometer (ensure it's enabled)
  config[0].cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;  // Enable accel
  config[0].cfg.acc.odr = BMI3_ACC_ODR_50HZ;  // 50Hz for motion detection
  config[0].cfg.acc.range = BMI3_ACC_RANGE_2G;
  config[0].cfg.acc.bwp = BMI3_ACC_BW_ODR_QUARTER;
  config[0].cfg.acc.avg_num = BMI3_ACC_AVG4;
  
  // Configure any-motion detection
  config[1].cfg.any_motion.slope_thres = ANY_MOTION_SLOPE_THRES;
  config[1].cfg.any_motion.hysteresis = MOTION_HYSTERESIS;
  config[1].cfg.any_motion.duration = ANY_MOTION_DURATION;
  config[1].cfg.any_motion.acc_ref_up = 1;
  config[1].cfg.any_motion.wait_time = MOTION_WAIT_TIME;
  
  // Configure no-motion detection
  config[2].cfg.no_motion.slope_thres = NO_MOTION_SLOPE_THRES;
  config[2].cfg.no_motion.hysteresis = MOTION_HYSTERESIS;
  config[2].cfg.no_motion.duration = NO_MOTION_DURATION;
  config[2].cfg.no_motion.acc_ref_up = 1;
  config[2].cfg.no_motion.wait_time = MOTION_WAIT_TIME;
  
  // Set configurations
  rslt = bmi323_set_sensor_config(config, 3, &bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: Failed to set sensor config: ");
    Serial.println(rslt);
    return false;
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
  
  // Map interrupts to INT1
  struct bmi3_map_int map_int = { 0 };
  map_int.any_motion_out = BMI3_INT1;
  map_int.no_motion_out = BMI3_INT1;
  
  rslt = bmi323_map_interrupt(map_int, &bmi3Device);
  if (rslt != BMI323_OK) {
    Serial.print("ERROR: Failed to map interrupts: ");
    Serial.println(rslt);
    return false;
  }
  Serial.println("Interrupts mapped to INT1");
  
  // Enable any-motion and no-motion features
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
    return false;
  }
  Serial.println("Motion features enabled");
  
  motionDetectionConfigured = true;
  Serial.println("BMI323 interrupt configuration complete");
  return true;
}

// Enter deep sleep with motion wake-up
void enterDeepSleep() {
  Serial.println("\n========== ENTERING DEEP SLEEP ==========");
  
  // Configure ESP32-C6 wake sources
  // ESP32-C6 only supports EXT1 wakeup (not EXT0)
  // Note: GPIO 22 is used for motion interrupt wakeup
  Serial.println("Configuring wake source: GPIO 22 (motion interrupt)");
  esp_sleep_enable_ext1_wakeup((1ULL << GPIO_NUM_22), ESP_EXT1_WAKEUP_ANY_HIGH); // Wake on HIGH (motion interrupt)
  
  Serial.println("Entering deep sleep...");
  Serial.flush(); // Ensure all messages are sent before sleep
  delay(100);
  
  // Enter deep sleep
  esp_deep_sleep_start();
  // Will not reach here - device will wake from motion interrupt
}

// Handle wake-up from deep sleep
void handleWakeup() {
  wakeCount++;
  
  Serial.println("\n========== WAKING FROM DEEP SLEEP ==========");
  Serial.print("Wake cycle #");
  Serial.println(wakeCount);
  
  // Check wake reason
  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  Serial.print("Wake reason: ");
  switch (wakeReason) {
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("Motion interrupt (GPIO 22)");
      break;
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("External signal (EXT0)");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Timer");
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      Serial.println("Touchpad");
      break;
    case ESP_SLEEP_WAKEUP_ULP:
      Serial.println("ULP program");
      break;
    default:
      Serial.println("Unknown or power-on reset");
      break;
  }
  
  // Clear BMI323 interrupt status using Bosch API
  uint16_t int_status = 0;
  int8_t rslt = bmi323_get_int1_status(&int_status, &bmi3Device);
  if (rslt == BMI323_OK) {
    if (int_status & BMI3_INT_STATUS_ANY_MOTION) {
      Serial.println(">>> ANY-MOTION INTERRUPT DETECTED <<<");
      Serial.println("Device movement detected - wake-up successful!");
    }
    
    if (int_status & BMI3_INT_STATUS_NO_MOTION) {
      Serial.println(">>> NO-MOTION INTERRUPT DETECTED <<<");
      Serial.println("Device has been still for configured duration");
    }
  }
  
  Serial.println("Wake-up handling complete");
  Serial.println("========================================\n");
}

void setup() {
  Serial.begin(115200);
  // No delay on boot - start immediately
  
  // Initialize power latch (set HIGH on boot)
  initPowerLatch();
  
  Serial.println("\n========================================");
  Serial.println("BMI323 Deep Sleep and Wake Test");
  Serial.println("========================================\n");
  
  // Check if waking from deep sleep
  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  bool wokeFromDeepSleep = (wakeReason == ESP_SLEEP_WAKEUP_EXT1 || wakeReason == ESP_SLEEP_WAKEUP_EXT0);
  
  if (wokeFromDeepSleep) {
    // Waking from deep sleep - handle wake-up first
    Serial.println("Waking from deep sleep - handling wake-up...");
    
    // Initialize I2C first
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);
    delay(100);
    
    // Reinitialize Bosch API device structure
    memset(&bmi3Device, 0, sizeof(bmi3Device));
    bmi3Device.intf = BMI3_I2C_INTF;
    uint8_t dev_addr = IMU_I2C_ADDRESS;
    bmi3Device.intf_ptr = &dev_addr;
    bmi3Device.read = bmi3_i2c_read_wrapper;
    bmi3Device.write = bmi3_i2c_write_wrapper;
    bmi3Device.delay_us = bmi3_delay_us_wrapper;
    bmi3Device.read_write_len = 8;
    
    int8_t rslt = bmi323_init(&bmi3Device);
    if (rslt == BMI323_OK) {
      sensorInitialized = true;
      motionDetectionConfigured = true;  // Assume it was configured before sleep
      handleWakeup();
    } else {
      Serial.print("ERROR: Failed to reinitialize BMI323 after wake-up: ");
      Serial.println(rslt);
      // powerOff(); // Power off with 5 second delay
      return;
    }
  } else {
    // Normal boot - initialize everything
    Serial.println("Normal boot - initializing test...");
    
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
    
    // Configure motion detection
    if (!configureBMI323Interrupts()) {
      Serial.println("ERROR: Failed to configure motion detection interrupts");
      return;
    }
    
    // Setup interrupt pin on ESP32
    Serial.print("\nSetting up interrupt on GPIO ");
    Serial.print(MOTION_INT_PIN);
    Serial.println("...");
    pinMode(MOTION_INT_PIN, INPUT_PULLDOWN);
    Serial.println("Interrupt pin configured (will be triggered by motion)");
    
    sensorInitialized = true;
    
    Serial.println("\n========================================");
    Serial.println("Test Setup Complete!");
    Serial.println("========================================");
    Serial.println("Test will enter deep sleep in 3 seconds...");
    Serial.println("Move the device to wake it up from deep sleep");
    Serial.println("========================================\n");
    
    delay(3000);
  }
  
  // Enter deep sleep (will wake on motion interrupt)
  if (sensorInitialized && motionDetectionConfigured) {
    enterDeepSleep();
  }
}

void loop() {
  // Update button handler (check for long press to power off)
  updateButtonHandler();
  
  // This should not be reached during normal test operation
  // Device should be in deep sleep most of the time
  // If we reach here, re-enter deep sleep
  Serial.println("Re-entering deep sleep in 2 seconds...");
  delay(2000);
  if (sensorInitialized && motionDetectionConfigured) {
    enterDeepSleep();
  }
}

