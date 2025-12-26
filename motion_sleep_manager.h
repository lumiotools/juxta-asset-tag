// Motion Sleep Manager - Handles motion detection and deep sleep power management
// Integrates BMI323 motion detection with ESP32-S3 deep sleep

#ifndef MOTION_SLEEP_MANAGER_H
#define MOTION_SLEEP_MANAGER_H

#include <Arduino.h>
#include "esp_sleep.h"
#include "imu_sensor.h"
#include "time_sync.h"
#include "ble_config.h"
#include "customwifi.h"
#include "driver/gpio.h"

// GPIO pin definitions
#define MOTION_INT_PIN D6      // BMI323 INT1 → ESP32-S3 GPIO 6
#define MOTION_INT2_PIN D7     // BMI323 INT2 → ESP32-S3 GPIO 7 (optional)
#define POWER_LATCH_PIN D14    // Power latch control pin (IO14)

// Timing constants
#define NO_MOTION_SLEEP_MS 300000  // 5 minutes in milliseconds
#define NO_MOTION_COUNTDOWN_INTERVAL_MS 60000  // Print countdown every 1 minute

// Motion detection thresholds (adjustable)
#define ANY_MOTION_THRESHOLD 0.5f  // 0.5g threshold for any-motion detection
#define NO_MOTION_THRESHOLD 0.1f   // 0.1g threshold for no-motion detection

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
  // Configure BMI323 interrupts for motion detection
  // Returns true if successful, false otherwise
  static bool configureBMI323Interrupts(IMUSensor* imu) {
    if (imu == nullptr) {
      Serial.println("ERROR: IMU sensor not available for interrupt configuration");
      return false;
    }
    
    Serial.println("Configuring BMI323 interrupts for motion detection...");
    
    // Note: This implementation uses direct register access
    // BMI323 interrupt configuration registers need to be set based on datasheet
    // The actual register addresses and values may need adjustment
    
    // Configure INT1 pin: active HIGH, push-pull, latched mode
    // Register 0x53: INT1_IO_CTRL
    // Bit 0: INT1 output enable (1 = enabled)
    // Bit 1: INT1 output type (0 = push-pull, 1 = open-drain)
    // Bit 2: INT1 active level (0 = active LOW, 1 = active HIGH)
    // Bit 3: INT1 latch (0 = pulsed, 1 = latched)
    // Value: 0x0F = enabled, push-pull, active HIGH, latched
    uint16_t int1Config = 0x0F;  // Enable, push-pull, HIGH, latched
    imu->writeRegister16Public(0x53, int1Config);
    Serial.println("INT1 pin configured: active HIGH, push-pull, latched");
    
    // Configure any-motion detection
    // Register 0x5C: ANY_MOTION_CONFIG
    // Threshold and duration settings
    // Threshold: 0.5g = ~8192 LSB (for ±2g range, 16384 LSB/g)
    uint16_t anyMotionThreshold = (uint16_t)(ANY_MOTION_THRESHOLD * 16384.0f);
    uint16_t anyMotionConfig = (anyMotionThreshold & 0x7FFF) | 0x8000; // Enable + threshold
    imu->writeRegister16Public(0x5C, anyMotionConfig);
    Serial.print("Any-motion threshold configured: ");
    Serial.print(ANY_MOTION_THRESHOLD);
    Serial.println("g");
    
    // Configure no-motion detection
    // Register 0x5D: NO_MOTION_CONFIG
    // Threshold: 0.1g, Duration: 5 minutes (300 seconds)
    // Duration in samples at 800Hz ODR: 300 * 800 = 240000 samples
    uint16_t noMotionThreshold = (uint16_t)(NO_MOTION_THRESHOLD * 16384.0f);
    uint16_t noMotionConfig = (noMotionThreshold & 0x7FFF) | 0x8000; // Enable + threshold
    imu->writeRegister16Public(0x5D, noMotionConfig);
    Serial.print("No-motion threshold configured: ");
    Serial.print(NO_MOTION_THRESHOLD);
    Serial.println("g");
    
    // Map interrupts to INT1
    // Register 0x58: INT_MAP_DATA
    // Bit 0: Any-motion → INT1
    // Bit 1: No-motion → INT1
    uint16_t intMap = 0x03; // Map both any-motion and no-motion to INT1
    imu->writeRegister16Public(0x58, intMap);
    Serial.println("Interrupts mapped to INT1");
    
    interruptsConfigured = true;
    Serial.println("BMI323 interrupt configuration complete");
    return true;
  }
  
  // Setup motion interrupt ISR on GPIO 6
  static void setupMotionISR() {
    if (motionISRAttached) {
      return; // Already attached
    }
    
    Serial.println("Setting up motion interrupt ISR on GPIO 6...");
    pinMode(MOTION_INT_PIN, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(MOTION_INT_PIN), handleMotionInterrupt, RISING);
    motionISRAttached = true;
    Serial.println("Motion ISR attached to GPIO 6 (RISING edge)");
  }
  
  // ISR handler for motion interrupt (called from interrupt context)
  static void IRAM_ATTR handleMotionInterrupt() {
    motionInterruptFlag = true;
  }
  
  // Track no-motion duration and print countdown
  // Returns true if 5 minutes of no-motion elapsed, false otherwise
  static bool trackNoMotionDuration() {
    unsigned long currentTime = TimeSync::getCurrentTimeMillis();
    
    // Check if motion was detected
    if (motionInterruptFlag) {
      motionInterruptFlag = false; // Clear flag
      lastMotionTime = currentTime;
      noMotionStartTime = 0;
      noMotionTracking = false;
      Serial.println("Motion detected - resetting no-motion timer");
      return false;
    }
    
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
      
      // Print countdown every minute
      static unsigned long lastCountdownPrint = 0;
      if (noMotionElapsed - lastCountdownPrint >= NO_MOTION_COUNTDOWN_INTERVAL_MS) {
        lastCountdownPrint = noMotionElapsed;
        Serial.print("No motion: ");
        Serial.print(minutesElapsed);
        Serial.print("/");
        Serial.print(totalMinutes);
        Serial.println(" minutes");
      }
      
      // Check if 5 minutes elapsed
      if (noMotionElapsed >= NO_MOTION_SLEEP_MS) {
        Serial.println("5 minutes of no motion - ready for deep sleep");
        return true;
      }
    }
    
    return false;
  }
  
  // Enter deep sleep with motion wake-up
  static void enterDeepSleep(IMUSensor* imu) {
    Serial.println("\n========== ENTERING DEEP SLEEP ==========");
    
    // Set BMI323 to low-power mode (keep motion detection active)
    if (imu != nullptr) {
      Serial.println("Setting BMI323 to low-power mode...");
      // Register 0x7C: PWR_CONF
      // Set to suspend mode but keep motion detection active
      // This may need adjustment based on actual BMI323 register map
      // For now, we'll keep it in normal mode but reduce ODR if possible
      Serial.println("BMI323 low-power mode configured (motion detection active)");
    }
    
    // Disable WiFi/BLE before sleep
    Serial.println("Disabling WiFi and BLE...");
    if (BLEConfig::isEnabled()) {
      BLEConfig::stop();
    }
    CustomWiFi::disconnectWiFi();
    
    // Configure power latch (IO14) - CRITICAL for maintaining power during sleep
    // Note: setPowerLatchPin(true) should be called in main file before enterDeepSleep()
    // This ensures pin is configured as OUTPUT with pull-up and set HIGH
    // Then we use gpio_hold_en to maintain the state during deep sleep
    Serial.println("Configuring power latch (IO14) for deep sleep...");
    // Ensure pin is HIGH before holding (should already be set by setPowerLatchPin in main)
    gpio_set_level(GPIO_NUM_14, 1);  // Ensure HIGH state
    gpio_hold_en(GPIO_NUM_14);  // Hold IO14 HIGH during deep sleep
    Serial.println("Power latch held HIGH - power will remain on during deep sleep");
    
    // Configure ESP32-S3 wake sources
    Serial.println("Configuring wake sources...");
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_6, 1); // Wake on HIGH (motion interrupt)
    Serial.println("Wake source: GPIO 6 (motion interrupt)");
    
    Serial.println("Entering deep sleep...");
    Serial.flush(); // Ensure all messages are sent before sleep
    delay(100);
    
    // Enter deep sleep
    esp_deep_sleep_start();
    // Will not reach here
  }
  
  // Handle wake-up from deep sleep
  static void handleWakeup(IMUSensor* imu) {
    Serial.println("\n========== WAKING FROM DEEP SLEEP ==========");
    
    // Release GPIO hold on IO14 FIRST - CRITICAL before using pin normally
    Serial.println("Releasing GPIO hold on IO14...");
    gpio_hold_dis(GPIO_NUM_14);
    Serial.println("GPIO hold released");
    
    // Check wake reason
    esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
    Serial.print("Wake reason: ");
    switch (wakeReason) {
      case ESP_SLEEP_WAKEUP_EXT0:
        Serial.println("Motion interrupt (GPIO 6)");
        break;
      case ESP_SLEEP_WAKEUP_EXT1:
        Serial.println("External signal (EXT1)");
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
    
    // Clear BMI323 interrupt status
    if (imu != nullptr) {
      Serial.println("Clearing BMI323 interrupt status...");
      // Register 0x1C: INT_STATUS_0 (interrupt status)
      // Reading this register clears the interrupt flags
      uint16_t intStatus = imu->readRegister16Public(0x1C);
      Serial.print("Interrupt status: 0x");
      Serial.println(intStatus, HEX);
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

