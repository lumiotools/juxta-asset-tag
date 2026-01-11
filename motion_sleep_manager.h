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
//
// ⚠️ WARNING: MOTION_INT_PIN is currently set to GPIO 25
// GPIO 25 is NOT an RTC GPIO and CANNOT wake from deep sleep!
// Deep sleep wake-up will FAIL with current configuration!
// 
// TO FIX: Change MOTION_INT_PIN to an RTC GPIO (0-7)
// Recommended RTC GPIOs: 2, 3, 6, 7 (avoid 0,1,4,5 due to other functions)
// ============================================================================

// GPIO pin definitions
#define MOTION_INT_PIN 25     // BMI323 INT1 → ESP32-C6 GPIO 25
                              // ⚠️ WARNING: GPIO 25 is NOT RTC-capable!
                              // Change to GPIO 2, 3, 6, or 7 for deep sleep wake-up
// Note: MOTION_INT2_PIN removed - not required
#define POWER_LATCH_PIN 4     // Power latch control pin (IO4)

// Timing constants
#define NO_MOTION_SLEEP_MS 300000  // 5 minutes in milliseconds (300 seconds)
#define NO_MOTION_COUNTDOWN_INTERVAL_MS 30000  // Print countdown every 30 seconds

// Motion detection thresholds (adjustable)
// Note: These are converted to slope_thres values for Bosch API
// slope_thres range: 0-4095 (higher = more sensitive)
// Approximate conversion: 0.1g ≈ 1638 LSB, but slope_thres is different
// Using values from example: slope_thres = 9 for moderate sensitivity
#define ANY_MOTION_SLOPE_THRES 9      // Slope threshold for any-motion (0-4095)
#define NO_MOTION_SLOPE_THRES 9       // Slope threshold for no-motion (0-4095)
#define MOTION_HYSTERESIS 5            // Hysteresis (0-1023)
#define MOTION_WAIT_TIME 5             // Wait time (0-7)
// Duration: Range = 0 to 8191 samples (at 50Hz ODR, each sample = 20ms)
// Maximum duration: 8191 * 20ms = 163.82 seconds = ~2.73 minutes
// For 5 minutes, we need to use software tracking with hardware interrupt as trigger
// Using maximum hardware duration, then tracking in software
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
    
    int8_t rslt;
    
    // Configure INT1 pin: active HIGH, push-pull, non-latched mode
    // Get current pin configuration first (best practice from MCU examples)
    struct bmi3_int_pin_config int_cfg = { 0 };
    rslt = bmi323_get_int_pin_config(&int_cfg, dev);
    if (rslt != BMI323_OK) {
      Serial.print("ERROR: Failed to get INT1 pin config: ");
      Serial.println(rslt);
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
    
    // Configure any-motion detection
    config[1].cfg.any_motion.slope_thres = ANY_MOTION_SLOPE_THRES;
    config[1].cfg.any_motion.hysteresis = MOTION_HYSTERESIS;
    config[1].cfg.any_motion.duration = ANY_MOTION_DURATION_SAMPLES;
    config[1].cfg.any_motion.acc_ref_up = 1;  // Always update reference
    config[1].cfg.any_motion.wait_time = MOTION_WAIT_TIME;
    
    // Configure no-motion detection
    // Note: We'll use a shorter hardware duration and track the full 5 minutes in software
    // This allows us to get interrupts for any-motion to reset the timer
    config[2].cfg.no_motion.slope_thres = NO_MOTION_SLOPE_THRES;
    config[2].cfg.no_motion.hysteresis = MOTION_HYSTERESIS;
    config[2].cfg.no_motion.duration = NO_MOTION_DURATION_SAMPLES;  // 5 minutes at 50Hz
    config[2].cfg.no_motion.acc_ref_up = 1;  // Always update reference
    config[2].cfg.no_motion.wait_time = MOTION_WAIT_TIME;
    
    // Set configurations
    rslt = bmi323_set_sensor_config(config, 3, dev);
    if (rslt != BMI323_OK) {
      Serial.print("ERROR: Failed to set sensor config: ");
      Serial.println(rslt);
      return false;
    }
    
    Serial.print("Any-motion configured: slope_thres=");
    Serial.print(ANY_MOTION_SLOPE_THRES);
    Serial.print(", duration=");
    Serial.println(ANY_MOTION_DURATION_SAMPLES);
    
    Serial.print("No-motion configured: slope_thres=");
    Serial.print(NO_MOTION_SLOPE_THRES);
    Serial.print(", duration=");
    Serial.print(NO_MOTION_DURATION_SAMPLES);
    Serial.print(" samples (~");
    Serial.print((NO_MOTION_DURATION_SAMPLES * 20) / 1000);
    Serial.println(" seconds at 50Hz)");
    Serial.println("Note: Hardware max is ~2.73 minutes, software tracks full 5 minutes");
    
    // Map interrupts to INT1 (do this before enabling features, matching example order)
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
    
    // Enable any-motion and no-motion features (after mapping interrupts)
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
    Serial.println("Motion features enabled");
    
    interruptsConfigured = true;
    Serial.println("BMI323 interrupt configuration complete using Bosch API");
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
    Serial.print("Motion ISR attached to GPIO ");
    Serial.print(MOTION_INT_PIN);
    Serial.println(" (RISING edge)");
  }
  
  // ISR handler for motion interrupt (called from interrupt context)
  static void IRAM_ATTR handleMotionInterrupt() {
    motionInterruptFlag = true;
  }
  
  // Track no-motion duration and print countdown
  // Returns true if 5 minutes of no-motion elapsed, false otherwise
  // Now uses hardware no-motion interrupt from BMI323
  static bool trackNoMotionDuration(IMUSensor* imu) {
    unsigned long currentTime = TimeSync::getCurrentTimeMillis();
    
    // Check if motion interrupt was detected (any-motion or no-motion)
    if (motionInterruptFlag) {
      motionInterruptFlag = false; // Clear flag
      
      // Read interrupt status to determine which interrupt occurred
      if (imu != nullptr && imu->isBoschApiInitialized()) {
        struct bmi3_dev* dev = imu->getBoschDevice();
        if (dev != nullptr) {
          uint16_t int_status = 0;
          int8_t rslt = bmi323_get_int1_status(&int_status, dev);
          
          if (rslt == BMI323_OK) {
            if (int_status & BMI3_INT_STATUS_ANY_MOTION) {
              // Any-motion detected - reset no-motion timer
              lastMotionTime = currentTime;
              noMotionStartTime = 0;
              noMotionTracking = false;
              Serial.println("Any-motion detected - resetting no-motion timer");
            } else if (int_status & BMI3_INT_STATUS_NO_MOTION) {
              // Hardware no-motion interrupt - device has been still for configured duration
              Serial.println("Hardware no-motion interrupt detected - ready for deep sleep");
              return true;
            }
          }
        }
      } else {
        // Fallback: treat any interrupt as motion
        lastMotionTime = currentTime;
        noMotionStartTime = 0;
        noMotionTracking = false;
        Serial.println("Motion interrupt detected - resetting no-motion timer");
      }
      return false;
    }
    
    // Software-based tracking as backup (if hardware interrupt doesn't fire)
    // Start tracking no-motion if enough time has passed since last motion
    if (lastMotionTime > 0 && (currentTime - lastMotionTime) > 1000) { // 1 second threshold
      if (!noMotionTracking) {
        noMotionStartTime = currentTime;
        noMotionTracking = true;
        Serial.println("No motion detected - starting 5-minute countdown");
      }
      
      // Calculate elapsed no-motion time
      unsigned long noMotionElapsed = currentTime - noMotionStartTime;
      unsigned long minutesElapsed = noMotionElapsed / 60000;
      unsigned long totalMinutes = NO_MOTION_SLEEP_MS / 60000;
      
      // Print countdown every 30 seconds
      static unsigned long lastCountdownPrint = 0;
      if (noMotionElapsed - lastCountdownPrint >= NO_MOTION_COUNTDOWN_INTERVAL_MS) {
        lastCountdownPrint = noMotionElapsed;
        Serial.print("No motion: ");
        Serial.print(minutesElapsed);
        Serial.print("/");
        Serial.print(totalMinutes);
        Serial.println(" minutes");
      }
      
      // Check if 5 minutes elapsed (software fallback)
      if (noMotionElapsed >= NO_MOTION_SLEEP_MS) {
        Serial.println("5 minutes of no motion (software timer) - ready for deep sleep");
        return true;
      }
    }
    
    return false;
  }
  
  // Enter deep sleep with motion wake-up
  static void enterDeepSleep(IMUSensor* imu) {
    Serial.println("\n========== ENTERING DEEP SLEEP ==========");
    
    // Set BMI323 to low-power mode (keep motion detection active)
    if (imu != nullptr && imu->isBoschApiInitialized()) {
      Serial.println("Setting BMI323 to low-power mode...");
      struct bmi3_dev* dev = imu->getBoschDevice();
      if (dev != nullptr) {
        // Note: BMI323 motion detection should remain active in low-power modes
        // The accelerometer needs to stay active for motion detection
        // We can reduce ODR but keep accel enabled
        struct bmi3_sens_config config = { 0 };
        config.type = BMI323_ACCEL;
        
        int8_t rslt = bmi323_get_sensor_config(&config, 1, dev);
        if (rslt == BMI323_OK) {
          // Reduce ODR to save power while keeping motion detection
          config.cfg.acc.odr = BMI3_ACC_ODR_50HZ;  // Lower ODR for power saving
          config.cfg.acc.acc_mode = BMI3_ACC_MODE_LOW_PWR;  // Low power mode
          
          rslt = bmi323_set_sensor_config(&config, 1, dev);
          if (rslt == BMI323_OK) {
            Serial.println("BMI323 set to low-power mode (motion detection active)");
          } else {
            Serial.print("Warning: Failed to set low-power mode: ");
            Serial.println(rslt);
          }
        }
      }
    }
    
    // Disable WiFi/BLE before sleep
    Serial.println("Disabling WiFi and BLE...");
    if (BLEConfig::isEnabled()) {
      BLEConfig::stop();
    }
    CustomWiFi::disconnectWiFi();
    
    // Configure power latch (IO4) - CRITICAL for maintaining power during sleep
    // Note: setPowerLatchPin(true) should be called in main file before enterDeepSleep()
    // This ensures pin is configured as OUTPUT with pull-up and set HIGH
    // Then we use gpio_hold_en to maintain the state during deep sleep
    Serial.println("Configuring power latch (IO4) for deep sleep...");
    // Ensure pin is HIGH before holding (should already be set by setPowerLatchPin in main)
    gpio_set_level(GPIO_NUM_4, 1);  // Ensure HIGH state
    gpio_hold_en(GPIO_NUM_4);  // Hold IO4 HIGH during deep sleep
    Serial.println("Power latch held HIGH - power will remain on during deep sleep");
    
    // CRITICAL: Release any GPIO hold on wake pin before configuring wake-up
    // GPIO hold can prevent wake-up from working
    Serial.println("Releasing GPIO hold on wake pin...");
    gpio_hold_dis((gpio_num_t)MOTION_INT_PIN);
    
    // Configure wake pin properly before sleep
    // For wake-up to work, pin must be configured as INPUT with appropriate pull
    Serial.print("Configuring wake pin GPIO ");
    Serial.print(MOTION_INT_PIN);
    Serial.println(" (motion interrupt)...");
    
    // Ensure pin is in correct state - release from any hold first
    gpio_reset_pin((gpio_num_t)MOTION_INT_PIN);
    pinMode(MOTION_INT_PIN, INPUT_PULLDOWN);  // Pull-down: wakes on HIGH signal
    gpio_set_direction((gpio_num_t)MOTION_INT_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)MOTION_INT_PIN, GPIO_PULLDOWN_ONLY);
    
    // Verify pin state
    Serial.print("GPIO ");
    Serial.print(MOTION_INT_PIN);
    Serial.print(" state: ");
    Serial.println(digitalRead(MOTION_INT_PIN));
    
    // Configure ESP32-C6 wake sources
    // ESP32-C6 only supports EXT1 wakeup (not EXT0)
    // IMPORTANT: Only RTC GPIOs (LP_GPIOs) can wake from deep sleep!
    // RTC GPIOs on ESP32-C6: GPIO 0, 1, 2, 3, 4, 5, 6, 7 ONLY!
    Serial.println("Configuring wake source (EXT1):");
    Serial.print("  - GPIO ");
    Serial.print(MOTION_INT_PIN);
    Serial.print(" (motion interrupt) - ");
    if (MOTION_INT_PIN >= 0 && MOTION_INT_PIN <= 7) {
      Serial.println("RTC-capable ✓");
    } else {
      Serial.println("NOT RTC-capable ✗");
      Serial.println("ERROR: GPIO is not RTC-capable! Use GPIO 0-7 only");
      Serial.println("RTC GPIOs on ESP32-C6: 0, 1, 2, 3, 4, 5, 6, 7");
      Serial.println("Current MOTION_INT_PIN will NOT work for deep sleep wake-up!");
      return;
    }
    
    // EXT1 wakeup: wake on GPIO going HIGH
    esp_err_t wakeup_result = esp_sleep_enable_ext1_wakeup(
      (1ULL << MOTION_INT_PIN), 
      ESP_EXT1_WAKEUP_ANY_HIGH
    );
    
    if (wakeup_result != ESP_OK) {
      Serial.print("ERROR: Failed to configure wake-up sources! Error: ");
      Serial.println(wakeup_result);
      Serial.println("Make sure you're using RTC GPIOs (GPIO 0-7 on ESP32-C6)");
      Serial.println("RTC GPIOs (LP_GPIOs): 0, 1, 2, 3, 4, 5, 6, 7");
      Serial.println("GPIO 8 and above are NOT RTC-capable and will not work!");
      return;
    }
    Serial.println("Wake sources configured successfully");
    
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
    
    // Release GPIO hold on IO4 FIRST - CRITICAL before using pin normally
    Serial.println("Releasing GPIO hold on IO4...");
    gpio_hold_dis(GPIO_NUM_4);
    Serial.println("GPIO hold released");
    
    // Check wake reason
    esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
    Serial.print("Wake reason: ");
    switch (wakeReason) {
      case ESP_SLEEP_WAKEUP_EXT1: {
        Serial.print("EXT1 wake-up from GPIO ");
        uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
        if (wakeup_pin_mask & (1ULL << MOTION_INT_PIN)) {
          Serial.print(MOTION_INT_PIN);
          Serial.println(" (motion interrupt)");
        } else {
          Serial.print("unknown (mask: 0x");
          Serial.print(wakeup_pin_mask, HEX);
          Serial.println(")");
        }
        break;
      }
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
    if (imu != nullptr && imu->isBoschApiInitialized()) {
      Serial.println("Clearing BMI323 interrupt status...");
      struct bmi3_dev* dev = imu->getBoschDevice();
      if (dev != nullptr) {
        uint16_t int_status = 0;
        // Reading interrupt status clears it (clear-on-read)
        int8_t rslt = bmi323_get_int1_status(&int_status, dev);
        if (rslt == BMI323_OK) {
          Serial.print("Interrupt status: 0x");
          Serial.println(int_status, HEX);
        } else {
          Serial.print("Warning: Failed to read interrupt status: ");
          Serial.println(rslt);
        }
      }
    }
    
    // Reset motion tracking variables
    motionInterruptFlag = false;
    lastMotionTime = 0;
    noMotionStartTime = 0;
    noMotionTracking = false;
    
    Serial.println("Wake-up handling complete - resuming normal operation");
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

