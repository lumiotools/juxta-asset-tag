// Motion Sleep Manager - Handles motion detection and deep sleep power management
// TEST VERSION - Modified for threshold 20 testing
// Integrates BMI323 motion detection with ESP32-C6 deep sleep

#ifndef MOTION_SLEEP_MANAGER_H
#define MOTION_SLEEP_MANAGER_H

#include <Arduino.h>
#include "esp_sleep.h"
#include "imu_sensor.h"
#include "driver/gpio.h"
// Include Bosch BMI323 library from local libs folder
#include "../../libs/BMI3XY_SensorAPI-main/bmi323.h"

// ============================================================================
// ESP32-C6 RTC GPIO PINS (for deep sleep wake-up)
// ============================================================================
// GPIO pin definitions
#define MOTION_INT_PIN 5     // BMI323 INT1 → ESP32-C6 GPIO 5 (RTC-capable)

// Timing constants
#define NO_MOTION_SLEEP_MS 300000  // 5 minutes in milliseconds (300 seconds)
#define NO_MOTION_COUNTDOWN_INTERVAL_MS 30000  // Print countdown every 30 seconds

// ============================================================================
// MOTION DETECTION THRESHOLDS - SET TO 20 FOR THIS TEST
// ============================================================================
// Note: These are slope_thres values for Bosch API
// slope_thres range: 0-4095 (higher = more sensitive)
#define ANY_MOTION_SLOPE_THRES 20      // Slope threshold for any-motion (TEST: 20)
#define NO_MOTION_SLOPE_THRES 20       // Slope threshold for no-motion (TEST: 20)
#define MOTION_HYSTERESIS 5            // Hysteresis (0-1023)
#define MOTION_WAIT_TIME 5             // Wait time (0-7)
// Duration: Range = 0 to 8191 samples (at 50Hz ODR, each sample = 20ms)
#define NO_MOTION_DURATION_SAMPLES 8191   // Maximum hardware duration (~2.73 minutes at 50Hz)
#define ANY_MOTION_DURATION_SAMPLES 1     // Immediate detection (1 sample = 20ms)

// Global motion tracking variables
extern volatile bool motionInterruptFlag;
extern unsigned long lastMotionTime;
extern unsigned long noMotionStartTime;
extern bool noMotionTracking;

class MotionSleepManager {
private:
  static bool interruptsConfigured;
  static bool motionISRAttached;
  
public:
  // Configure BMI323 interrupts for motion detection using Bosch API
  // Returns true if successful, false otherwise
  static bool configureBMI323Interrupts(IMUSensor* imu) {
    if (imu == nullptr || !imu->isBoschApiInitialized()) {
      Serial.println("ERROR: IMU sensor not available or Bosch API not initialized");
      return false;
    }
    
    struct bmi3_dev* dev = imu->getBoschDevice();
    if (dev == nullptr) {
      Serial.println("ERROR: Failed to get Bosch device structure");
      return false;
    }
    
    Serial.println("Configuring BMI323 interrupts for motion detection using Bosch API...");
    Serial.println("*** TEST MODE: Threshold = 20 for both any-motion and no-motion ***");
    
    int8_t rslt;
    
    // Configure INT1 pin: active HIGH, push-pull, non-latched mode
    struct bmi3_int_pin_config int_cfg = { 0 };
    rslt = bmi323_get_int_pin_config(&int_cfg, dev);
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
    
    rslt = bmi323_set_int_pin_config(&int_cfg, dev);
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
    rslt = bmi323_get_sensor_config(config, 3, dev);
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
    
    // Configure any-motion detection with THRESHOLD 20
    config[1].cfg.any_motion.slope_thres = ANY_MOTION_SLOPE_THRES;
    config[1].cfg.any_motion.hysteresis = MOTION_HYSTERESIS;
    config[1].cfg.any_motion.duration = ANY_MOTION_DURATION_SAMPLES;
    config[1].cfg.any_motion.acc_ref_up = 1;  // Always update reference
    config[1].cfg.any_motion.wait_time = MOTION_WAIT_TIME;
    
    // Configure no-motion detection with THRESHOLD 20
    config[2].cfg.no_motion.slope_thres = NO_MOTION_SLOPE_THRES;
    config[2].cfg.no_motion.hysteresis = MOTION_HYSTERESIS;
    config[2].cfg.no_motion.duration = NO_MOTION_DURATION_SAMPLES;
    config[2].cfg.no_motion.acc_ref_up = 1;  // Always update reference
    config[2].cfg.no_motion.wait_time = MOTION_WAIT_TIME;
    
    // Set configurations
    rslt = bmi323_set_sensor_config(config, 3, dev);
    if (rslt != BMI323_OK) {
      Serial.print("ERROR: Failed to set sensor config: ");
      Serial.println(rslt);
      return false;
    }
    
    Serial.println("╔════════════════════════════════════════════╗");
    Serial.println("║   MOTION DETECTION CONFIGURATION          ║");
    Serial.println("╠════════════════════════════════════════════╣");
    Serial.print("║ Any-motion slope_thres:  ");
    Serial.print(ANY_MOTION_SLOPE_THRES);
    Serial.println("                 ║");
    Serial.print("║ Any-motion duration:     ");
    Serial.print(ANY_MOTION_DURATION_SAMPLES);
    Serial.println(" samples        ║");
    Serial.println("╠════════════════════════════════════════════╣");
    Serial.print("║ No-motion slope_thres:   ");
    Serial.print(NO_MOTION_SLOPE_THRES);
    Serial.println("                 ║");
    Serial.print("║ No-motion duration:      ");
    Serial.print(NO_MOTION_DURATION_SAMPLES);
    Serial.println(" samples      ║");
    Serial.print("║ (~");
    Serial.print((NO_MOTION_DURATION_SAMPLES * 20) / 1000);
    Serial.println(" seconds at 50Hz)             ║");
    Serial.println("╚════════════════════════════════════════════╝");
    
    // Map interrupts to INT1 (do this before enabling features)
    struct bmi3_map_int map_int = { 0 };
    map_int.any_motion_out = BMI3_INT1;
    map_int.no_motion_out = BMI3_INT1;
    
    rslt = bmi323_map_interrupt(map_int, dev);
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
    
    rslt = bmi323_select_sensor(&feature, dev);
    if (rslt != BMI323_OK) {
      Serial.print("ERROR: Failed to enable motion features: ");
      Serial.println(rslt);
      return false;
    }
    Serial.println("Motion features enabled (X, Y, Z axes)");
    
    interruptsConfigured = true;
    Serial.println("✓ BMI323 interrupt configuration complete using Bosch API");
    return true;
  }
  
  // Setup motion interrupt ISR
  static void setupMotionISR() {
    if (motionISRAttached) {
      return; // Already attached
    }
    
    Serial.print("Setting up motion interrupt ISR on GPIO ");
    Serial.print(MOTION_INT_PIN);
    Serial.println("...");
    pinMode(MOTION_INT_PIN, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(MOTION_INT_PIN), handleMotionInterrupt, RISING);
    motionISRAttached = true;
    Serial.print("✓ Motion ISR attached to GPIO ");
    Serial.print(MOTION_INT_PIN);
    Serial.println(" (RISING edge)");
  }
  
  // ISR handler for motion interrupt (called from interrupt context)
  static void IRAM_ATTR handleMotionInterrupt() {
    motionInterruptFlag = true;
  }
  
  // Check if interrupts are configured
  static bool isConfigured() {
    return interruptsConfigured;
  }
};

// Static member definitions
bool MotionSleepManager::interruptsConfigured = false;
bool MotionSleepManager::motionISRAttached = false;

// Global motion tracking variables (defined in main .ino file)
extern volatile bool motionInterruptFlag;
extern unsigned long lastMotionTime;
extern unsigned long noMotionStartTime;
extern bool noMotionTracking;

#endif
