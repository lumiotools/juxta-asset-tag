// Motion Sleep Manager - Handles motion detection and deep sleep power management
// Integrates BMI323 motion detection with ESP32-C6 deep sleep

#ifndef MOTION_SLEEP_MANAGER_H
#define MOTION_SLEEP_MANAGER_H

#include <Arduino.h>
#include "esp_sleep.h"
#include "imu_sensor.h"
#include "time_sync.h"
#include "ble_config.h"
#include "customwifi.h"
#include "driver/gpio.h"
// Include Bosch BMI323 library from local libs folder
#include "libs/BMI3XY_SensorAPI-main/bmi323.h"

// ============================================================================
// ESP32-C6 RTC GPIO PINS (for deep sleep wake-up)
// ============================================================================
// IMPORTANT: ESP32-C6 can ONLY wake from deep sleep using RTC GPIOs!
// Only RTC GPIOs (Low-Power GPIOs) can be used with esp_sleep_enable_ext1_wakeup()
//
// RTC GPIOs on ESP32-C6 (LP_GPIOs): GPIO 0, 1, 2, 3, 4, 5, 6, 7
// These are the ONLY GPIOs that can wake from deep sleep!
//
// NON-RTC GPIOs (CANNOT wake from deep sleep): GPIO 8-30
// GPIO 8 and above will NOT work for deep sleep wake-up and will give error!
// ============================================================================

// GPIO pin definitions
#define MOTION_INT_PIN 5      // BMI323 INT1 → ESP32-C6 GPIO 5 (RTC-capable ✓)
#define POWER_LATCH_PIN 4     // Power latch control pin (RTC-capable ✓)

// Timing constants
#define NO_MOTION_SLEEP_MS 300000                  // 5 minutes in milliseconds
#define NO_MOTION_COUNTDOWN_INTERVAL_MS 30000      // Print countdown every 30 seconds

// Motion detection thresholds (adjustable)
// slope_thres range: 0-4095 (higher = less sensitive)
// Using moderate sensitivity values from Bosch examples
#define ANY_MOTION_SLOPE_THRES 9                   // Slope threshold for any-motion
#define NO_MOTION_SLOPE_THRES 9                    // Slope threshold for no-motion
#define MOTION_HYSTERESIS 5                        // Hysteresis (0-1023)
#define MOTION_WAIT_TIME 5                         // Wait time (0-7)

// Duration settings (at 50Hz ODR, each sample = 20ms)
// Hardware maximum: 8191 samples = 163.82 seconds (~2.73 minutes)
// For 5 minutes, we use max hardware duration + software tracking
#define NO_MOTION_DURATION_SAMPLES 8191            // Max hardware duration
#define ANY_MOTION_DURATION_SAMPLES 1              // Immediate detection

// Global motion tracking variables (must be defined in main .ino file)
extern volatile bool motionInterruptFlag;
extern unsigned long lastMotionTime;
extern unsigned long noMotionStartTime;
extern bool noMotionTracking;

class MotionSleepManager {
private:
  static bool interruptsConfigured;
  static bool motionISRAttached;
  static unsigned long lastCountdownPrint;  // Track countdown print timing
  
  // Helper: Check if IMU and Bosch API are ready
  static bool isIMUReady(IMUSensor* imu, struct bmi3_dev** dev = nullptr) {
    if (!imu || !imu->isBoschApiInitialized()) {
      return false;
    }
    if (dev) {
      *dev = imu->getBoschDevice();
      return (*dev != nullptr);
    }
    return true;
  }
  
public:
  // Configure BMI323 interrupts for motion detection using Bosch API
  static bool configureBMI323Interrupts(IMUSensor* imu) {
    struct bmi3_dev* dev;
    if (!isIMUReady(imu, &dev)) {
      Serial.println("ERROR: IMU sensor not available or Bosch API not initialized");
      return false;
    }
    
    Serial.println("Configuring BMI323 interrupts for motion detection...");
    int8_t rslt;
    
    // Configure INT1 pin: active HIGH, push-pull, non-latched
    struct bmi3_int_pin_config int_cfg = { 0 };
    rslt = bmi323_get_int_pin_config(&int_cfg, dev);
    if (rslt != BMI323_OK) {
      Serial.println("ERROR: Failed to get INT1 pin config");
      return false;
    }
    
    // Modify only the fields we need (preserves other settings)
    int_cfg.pin_type = BMI3_INT1;
    int_cfg.int_latch = BMI3_INT_NON_LATCH;  // Non-latched (pulsed) for immediate detection
    int_cfg.pin_cfg[0].lvl = BMI3_INT_ACTIVE_HIGH;
    int_cfg.pin_cfg[0].od = BMI3_INT_PUSH_PULL;
    int_cfg.pin_cfg[0].output_en = BMI3_INT_OUTPUT_ENABLE;
    
    rslt = bmi323_set_int_pin_config(&int_cfg, dev);
    if (rslt != BMI323_OK) {
      Serial.println("ERROR: Failed to configure INT1 pin");
      return false;
    }
    Serial.println("✓ INT1 pin configured");
    
    // Configure accelerometer and motion detection
    struct bmi3_sens_config config[3] = { { 0 } };
    config[0].type = BMI323_ACCEL;
    config[1].type = BMI323_ANY_MOTION;
    config[2].type = BMI323_NO_MOTION;
    
    rslt = bmi323_get_sensor_config(config, 3, dev);
    if (rslt != BMI323_OK) {
      Serial.println("ERROR: Failed to get sensor config");
      return false;
    }
    
    // Accelerometer: 50Hz, 2G range, normal mode
    config[0].cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;
    config[0].cfg.acc.odr = BMI3_ACC_ODR_50HZ;
    config[0].cfg.acc.range = BMI3_ACC_RANGE_2G;
    config[0].cfg.acc.bwp = BMI3_ACC_BW_ODR_QUARTER;
    config[0].cfg.acc.avg_num = BMI3_ACC_AVG4;
    
    // Any-motion: immediate detection
    config[1].cfg.any_motion.slope_thres = ANY_MOTION_SLOPE_THRES;
    config[1].cfg.any_motion.hysteresis = MOTION_HYSTERESIS;
    config[1].cfg.any_motion.duration = ANY_MOTION_DURATION_SAMPLES;
    config[1].cfg.any_motion.acc_ref_up = 1;  // Always update reference
    config[1].cfg.any_motion.wait_time = MOTION_WAIT_TIME;
    
    // No-motion: max hardware duration (~2.73 min), software extends to 5 min
    config[2].cfg.no_motion.slope_thres = NO_MOTION_SLOPE_THRES;
    config[2].cfg.no_motion.hysteresis = MOTION_HYSTERESIS;
    config[2].cfg.no_motion.duration = NO_MOTION_DURATION_SAMPLES;
    config[2].cfg.no_motion.acc_ref_up = 1;
    config[2].cfg.no_motion.wait_time = MOTION_WAIT_TIME;
    
    rslt = bmi323_set_sensor_config(config, 3, dev);
    if (rslt != BMI323_OK) {
      Serial.println("ERROR: Failed to set sensor config");
      return false;
    }
    
    Serial.println("✓ Accelerometer & motion detection configured");
    
    // Map interrupts to INT1
    struct bmi3_map_int map_int = { 0 };
    map_int.any_motion_out = BMI3_INT1;
    map_int.no_motion_out = BMI3_INT1;
    
    rslt = bmi323_map_interrupt(map_int, dev);
    if (rslt != BMI323_OK) {
      Serial.println("ERROR: Failed to map interrupts");
      return false;
    }
    Serial.println("✓ Interrupts mapped to INT1");
    
    // Enable motion features on all axes
    struct bmi3_feature_enable feature = { 0 };
    feature.any_motion_x_en = BMI323_ENABLE;
    feature.any_motion_y_en = BMI323_ENABLE;
    feature.any_motion_z_en = BMI323_ENABLE;
    feature.no_motion_x_en = BMI323_ENABLE;
    feature.no_motion_y_en = BMI323_ENABLE;
    feature.no_motion_z_en = BMI323_ENABLE;
    
    rslt = bmi323_select_sensor(&feature, dev);
    if (rslt != BMI323_OK) {
      Serial.println("ERROR: Failed to enable motion features");
      return false;
    }
    
    interruptsConfigured = true;
    Serial.println("✓ BMI323 motion detection enabled successfully");
    return true;
  }
  
  // Setup motion interrupt ISR
  static void setupMotionISR() {
    if (motionISRAttached) return;
    
    pinMode(MOTION_INT_PIN, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(MOTION_INT_PIN), handleMotionInterrupt, RISING);
    motionISRAttached = true;
    
    Serial.print("✓ Motion ISR attached to GPIO ");
    Serial.print(MOTION_INT_PIN);
    Serial.println(" (RISING edge)");
  }
  
  // ISR handler (IRAM for fast response)
  static void IRAM_ATTR handleMotionInterrupt() {
    motionInterruptFlag = true;
  }
  
  // Track no-motion duration with hardware interrupt + software tracking
  // Returns true if 5 minutes of no-motion elapsed
  static bool trackNoMotionDuration(IMUSensor* imu) {
    unsigned long currentTime = TimeSync::getCurrentTimeMillis();
    
    // Handle hardware interrupt
    if (motionInterruptFlag) {
      motionInterruptFlag = false;
      
      struct bmi3_dev* dev;
      if (isIMUReady(imu, &dev)) {
        uint16_t int_status = 0;
        if (bmi323_get_int1_status(&int_status, dev) == BMI323_OK) {
          if (int_status & BMI3_INT_STATUS_ANY_MOTION) {
            // Motion detected - reset timer
            lastMotionTime = currentTime;
            noMotionStartTime = 0;
            noMotionTracking = false;
            lastCountdownPrint = 0;
            Serial.println("Motion detected - timer reset");
            return false;
          } else if (int_status & BMI3_INT_STATUS_NO_MOTION) {
            // Hardware no-motion period complete
            Serial.println("Hardware no-motion period complete - checking software timer");
            // Continue to software tracking below
          }
        }
      } else {
        // Fallback: treat interrupt as motion
        lastMotionTime = currentTime;
        noMotionStartTime = 0;
        noMotionTracking = false;
        lastCountdownPrint = 0;
        return false;
      }
    }
    
    // Software tracking for full 5-minute period
    if (lastMotionTime > 0 && (currentTime - lastMotionTime) > 1000) {
      if (!noMotionTracking) {
        noMotionStartTime = currentTime;
        noMotionTracking = true;
        lastCountdownPrint = 0;
        Serial.println("No motion - starting 5-minute countdown");
      }
      
      unsigned long noMotionElapsed = currentTime - noMotionStartTime;
      
      // Print countdown every 30 seconds
      if (noMotionElapsed - lastCountdownPrint >= NO_MOTION_COUNTDOWN_INTERVAL_MS) {
        lastCountdownPrint = noMotionElapsed;
        Serial.print("No motion: ");
        Serial.print(noMotionElapsed / 60000);
        Serial.print("/");
        Serial.print(NO_MOTION_SLEEP_MS / 60000);
        Serial.println(" minutes");
      }
      
      // Check if 5 minutes elapsed
      if (noMotionElapsed >= NO_MOTION_SLEEP_MS) {
        Serial.println("5 minutes of no motion - entering deep sleep");
        return true;
      }
    }
    
    return false;
  }
  
  // Enter deep sleep with motion wake-up
  static void enterDeepSleep(IMUSensor* imu) {
    Serial.println("\n========== ENTERING DEEP SLEEP ==========");
    
    // Set BMI323 to low-power mode (motion detection stays active)
    struct bmi3_dev* dev;
    if (isIMUReady(imu, &dev)) {
      struct bmi3_sens_config config = { 0 };
      config.type = BMI323_ACCEL;
      
      if (bmi323_get_sensor_config(&config, 1, dev) == BMI323_OK) {
        config.cfg.acc.odr = BMI3_ACC_ODR_50HZ;
        config.cfg.acc.acc_mode = BMI3_ACC_MODE_LOW_PWR;
        
        if (bmi323_set_sensor_config(&config, 1, dev) == BMI323_OK) {
          Serial.println("✓ BMI323 set to low-power mode");
        }
      }
    }
    
    // Disable WiFi/BLE
    Serial.println("✓ Disabling WiFi and BLE");
    if (BLEConfig::isEnabled()) BLEConfig::stop();
    CustomWiFi::disconnectWiFi();
    
    // Configure power latch (hold HIGH during sleep)
    gpio_set_level((gpio_num_t)POWER_LATCH_PIN, 1);
    gpio_hold_en((gpio_num_t)POWER_LATCH_PIN);
    Serial.println("✓ Power latch held HIGH");
    
    // Configure wake pin (release hold, set as input with pull-down)
    gpio_hold_dis((gpio_num_t)MOTION_INT_PIN);
    gpio_reset_pin((gpio_num_t)MOTION_INT_PIN);
    gpio_set_direction((gpio_num_t)MOTION_INT_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)MOTION_INT_PIN, GPIO_PULLDOWN_ONLY);
    Serial.print("✓ Wake pin GPIO ");
    Serial.print(MOTION_INT_PIN);
    Serial.println(" configured");
    
    // Validate RTC GPIO
    if (MOTION_INT_PIN < 0 || MOTION_INT_PIN > 7) {
      Serial.println("ERROR: Wake pin is not RTC-capable!");
      Serial.println("ESP32-C6 RTC GPIOs: 0-7 only");
      return;
    }
    
    // Configure EXT1 wake-up (wake on HIGH)
    esp_err_t result = esp_sleep_enable_ext1_wakeup(
      (1ULL << MOTION_INT_PIN), 
      ESP_EXT1_WAKEUP_ANY_HIGH
    );
    
    if (result != ESP_OK) {
      Serial.print("ERROR: Failed to configure wake-up: ");
      Serial.println(result);
      return;
    }
    
    Serial.print("✓ Wake-up configured on GPIO ");
    Serial.println(MOTION_INT_PIN);
    Serial.println("Entering deep sleep...");
    Serial.print("Device will wake on HIGH signal to GPIO ");
    Serial.println(MOTION_INT_PIN);
    Serial.flush(); // Ensure all messages are sent before sleep
    delay(200);  // Give time for serial to flush
    
    // Enter deep sleep
    esp_deep_sleep_start();
    // Will not reach here
  }
  
  // Handle wake-up from deep sleep
  static void handleWakeup(IMUSensor* imu) {
    Serial.println("\n========== WAKING FROM DEEP SLEEP ==========");
    
    // Release power latch hold
    gpio_hold_dis((gpio_num_t)POWER_LATCH_PIN);
    Serial.println("✓ Power latch hold released");
    
    // Check and report wake reason
    esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
    Serial.print("Wake reason: ");
    
    if (wakeReason == ESP_SLEEP_WAKEUP_EXT1) {
      uint64_t wakeup_mask = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_mask & (1ULL << MOTION_INT_PIN)) {
        Serial.print("Motion detected on GPIO ");
        Serial.println(MOTION_INT_PIN);
      } else {
        Serial.print("EXT1 (mask: 0x");
        Serial.print(wakeup_mask, HEX);
        Serial.println(")");
      }
    } else if (wakeReason == ESP_SLEEP_WAKEUP_TIMER) {
      Serial.println("Timer");
    } else {
      Serial.println("Power-on or unknown");
    }
    
    // Clear BMI323 interrupt status (clear-on-read)
    struct bmi3_dev* dev;
    if (isIMUReady(imu, &dev)) {
      uint16_t int_status = 0;
      if (bmi323_get_int1_status(&int_status, dev) == BMI323_OK) {
        Serial.print("✓ Interrupt cleared (status: 0x");
        Serial.print(int_status, HEX);
        Serial.println(")");
      }
    }
    
    // Reset tracking variables
    motionInterruptFlag = false;
    lastMotionTime = 0;
    noMotionStartTime = 0;
    noMotionTracking = false;
    lastCountdownPrint = 0;
    
    Serial.println("✓ Wake-up complete - resuming operation");
  }
  
  // Check if interrupts are configured
  static bool isConfigured() {
    return interruptsConfigured;
  }
};

// Static member initialization
bool MotionSleepManager::interruptsConfigured = false;
bool MotionSleepManager::motionISRAttached = false;
unsigned long MotionSleepManager::lastCountdownPrint = 0;



#endif // MOTION_SLEEP_MANAGER_H
