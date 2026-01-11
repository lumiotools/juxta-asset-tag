// BMI323 Deep Sleep and Wake Test
// This test verifies deep sleep functionality with motion-based wake-up
// Uses Bosch BMI323 SensorAPI directly from libs folder
// 
// ============================================================================
// ESP32-C6 RTC GPIO PINS (for deep sleep wake-up)
// ============================================================================
// IMPORTANT: ESP32-C6 can ONLY wake from deep sleep using RTC GPIOs!
// Only RTC GPIOs (Low-Power GPIOs) can be used with esp_sleep_enable_ext1_wakeup()
//
// RTC GPIOs on ESP32-C6 (LP_GPIOs): GPIO 0, 1, 2, 3, 4, 5, 6, 7
// These are the ONLY GPIOs that can wake from deep sleep!
//
// NON-RTC GPIOs (CANNOT wake from deep sleep): GPIO 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, etc.
// GPIO 8 and above will NOT work for deep sleep wake-up and will give error 258!
//
// Pin Usage in this code:
// - GPIO 0: I2C SDA (RTC-capable, but used for I2C - avoid for wake-up)
// - GPIO 1: I2C SCL (RTC-capable, but used for I2C - avoid for wake-up)
// - GPIO 2: Motion interrupt (MOTION_INT_PIN) - RTC-capable ✓ (recommended)
// - GPIO 3: Available for wake-up - RTC-capable ✓
// - GPIO 4: Power latch control (RTC-capable, but used for power management - avoid for wake-up)
// - GPIO 5: Available for wake-up - RTC-capable ✓ (strapping pin - be careful)
// - GPIO 6: Available for wake-up - RTC-capable ✓ (recommended)
// - GPIO 7: Available for wake-up - RTC-capable ✓ (recommended)
// - GPIO 10: Button (BUTTON_PIN) - NOT RTC-capable ✗ (cannot wake from deep sleep)
//
// To change wake-up pin: Modify MOTION_INT_PIN definition below
// Recommended RTC GPIOs for wake-up: 2, 3, 6, 7 (avoid 0,1,4,5 due to other functions)
// ============================================================================
//
// Hardware Connections:
// - BMI323 SDA → GPIO 0 (I2C_SDA_PIN)
// - BMI323 SCL → GPIO 1 (I2C_SCL_PIN)
// - BMI323 INT1 → GPIO 2 (MOTION_INT_PIN) - MUST be RTC GPIO (0-7) for deep sleep wake-up
// - Button → GPIO 10 (BUTTON_PIN) - NOT used for deep sleep wake-up
// - I2C Address: 0x69
//
// Test Flow:
// 1. Initialize BMI323 sensor using Bosch API
// 2. Configure motion detection interrupts
// 3. Send '1' through serial monitor to enter deep sleep
// 4. Wake up from motion interrupt (GPIO 2) when HIGH signal is applied
// 5. Print wake reason and handle wake-up

#include <Arduino.h>
#include <Wire.h>
#include "../../libs/BMI3XY_SensorAPI-main/bmi323.h"
#include "esp_sleep.h"
#include "../power_latch.h"

// I2C pin definitions (ESP32-C6)
#define I2C_SDA_PIN 0
#define I2C_SCL_PIN 1
#define IMU_I2C_ADDRESS 0x69

// Wake-up pin configuration
// IMPORTANT: ESP32-C6 can only wake from deep sleep using RTC GPIOs (LP_GPIOs)
// RTC GPIOs on ESP32-C6: GPIO 0, 1, 2, 3, 4, 5, 6, 7 ONLY!
// GPIO 8 and above are NOT RTC GPIOs and will give error 258!
// 
// Available RTC GPIOs: 0, 1, 2, 3, 4, 5, 6, 7
// Note: GPIO 0,1 used for I2C; GPIO 4 used for power latch
// Recommended for wake-up: GPIO 2, 3, 6, 7 (avoid 0,1,4,5 due to other functions)
// Note: GPIO 5 is a strapping pin - be careful with external signals during boot
#define MOTION_INT_PIN 5  // RTC-capable GPIO - must be 0-7

// Motion detection configuration
#define ANY_MOTION_SLOPE_THRES 9      // Slope threshold (0-4095)
#define NO_MOTION_SLOPE_THRES 9       // Slope threshold (0-4095)
#define MOTION_HYSTERESIS 5            // Hysteresis (0-1023)
#define MOTION_WAIT_TIME 5             // Wait time (0-7)
#define ANY_MOTION_DURATION 1          // Duration in samples (immediate)
#define NO_MOTION_DURATION 15000       // 5 minutes at 50Hz (300s * 50)

// LED pin for wake-up indication
#define LED_PIN 11

// Test state
bool sensorInitialized = false;
bool motionDetectionConfigured = false;
struct bmi3_dev bmi3Device = { 0 };
int wakeCount = 0;

// Wait for serial input '1' to trigger deep sleep
// Returns true when '1' is received
bool waitForSerialCommand() {
  Serial.println("Send '1' through serial monitor to enter deep sleep...");
  
  while (true) {
    if (Serial.available() > 0) {
      char input = Serial.read();
      if (input == '1') {
        Serial.println("Command '1' received - entering deep sleep...");
        // Clear any remaining serial buffer
        while (Serial.available() > 0) {
          Serial.read();
        }
        return true;
      } else if (input != '\n' && input != '\r') {
        Serial.print("Received: '");
        Serial.print(input);
        Serial.println("' - send '1' to enter deep sleep");
      }
    }
    delay(10);  // Small delay to avoid busy waiting
  }
}

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
  
  // Turn off LED before entering deep sleep (GPIO 11 is not RTC-capable, will lose state)
  digitalWrite(LED_PIN, LOW);
  Serial.println("LED turned OFF (will turn on again after wake-up)");
  
  // Configure control pin (GPIO 4 - power latch) HIGH before deep sleep
  // This ensures power latch remains ON during deep sleep
  Serial.println("Configuring power latch (GPIO 4) for deep sleep...");
  setPowerLatchPin(true);  // Set HIGH with pull-up
  gpio_set_level(GPIO_NUM_4, 1);  // Ensure HIGH state
  gpio_hold_en(GPIO_NUM_4);  // Hold GPIO 4 HIGH during deep sleep
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
  
  // Configure ESP32-C6 wake source
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
    return;
  }
  
  // EXT1 wakeup: wake on GPIO going HIGH
  // Note: Even for single pin, ESP32-C6 uses EXT1
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
  Serial.println("Apply HIGH signal (3.3V) to wake pins to wake device");
  Serial.flush(); // Ensure all messages are sent before sleep
  delay(200);  // Give time for serial to flush
  
  // Enter deep sleep
  esp_deep_sleep_start();
  // Will not reach here - device will wake from interrupt
}

// Handle wake-up from deep sleep
void handleWakeup() {
  wakeCount++;
  
  Serial.println("\n========== WAKING FROM DEEP SLEEP ==========");
  Serial.print("Wake cycle #");
  Serial.println(wakeCount);
  
  // Release GPIO hold FIRST (critical before using GPIO 4)
  // Serial.println("Releasing GPIO hold on power latch pin...");
  // gpio_hold_dis(GPIO_NUM_4);  // Release hold on GPIO 4
  // Serial.println("GPIO hold released");
  
  // Check wake reason
  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  Serial.print("Wake reason: ");
  switch (wakeReason) {
    case ESP_SLEEP_WAKEUP_EXT1: {
      // EXT1 wake-up from motion interrupt pin
      uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
      Serial.print("EXT1 wake-up pin mask: 0x");
      Serial.println(wakeup_pin_mask, HEX);
      
      if (wakeup_pin_mask & (1ULL << MOTION_INT_PIN)) {
        Serial.print("Motion interrupt (GPIO ");
        Serial.print(MOTION_INT_PIN);
        Serial.println(")");
      } else {
        Serial.print("EXT1 interrupt (unknown GPIO, mask: 0x");
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
  
  // Initialize LED pin (GPIO 11) - set HIGH immediately
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  Serial.println("LED pin (GPIO 11) initialized and set HIGH");
  
  if (wokeFromDeepSleep) {
    // Waking from deep sleep - handle wake-up first
    Serial.println("Waking from deep sleep - handling wake-up...");
    
    // Turn LED on to indicate wake-up (it was off during deep sleep)
    digitalWrite(LED_PIN, HIGH);
    Serial.println("LED turned ON after wake-up");
    
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
    
    // After wake-up, stay awake - do NOT immediately go back to sleep
    // The device will remain awake and LED will stay on
    Serial.println("\n========================================");
    Serial.println("Device is now AWAKE after wake-up");
    Serial.println("LED should remain ON");
    Serial.println("Send '1' through serial monitor to enter deep sleep again");
    Serial.println("========================================\n");
    return;  // Exit setup() - do NOT call enterDeepSleep() immediately
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
    // IMPORTANT: Only RTC GPIOs (0-7 on ESP32-C6) can wake from deep sleep
    Serial.print("\nSetting up interrupt pin (RTC GPIO for deep sleep wake-up)...");
    Serial.print("\n  GPIO ");
    Serial.print(MOTION_INT_PIN);
    Serial.print(" (motion interrupt) - ");
    if (MOTION_INT_PIN >= 0 && MOTION_INT_PIN <= 7) {
      Serial.println("RTC-capable ✓");
    } else {
      Serial.println("NOT RTC-capable ✗ - will not wake from deep sleep!");
      Serial.println("ERROR: Use GPIO 0-7 only! GPIO 8+ will not work for deep sleep wake-up");
    }
    pinMode(MOTION_INT_PIN, INPUT_PULLDOWN);
    
    // Button pin (not used for deep sleep wake-up, only for normal operation)
    Serial.print("  GPIO ");
    Serial.print(BUTTON_PIN);
    Serial.println(" (button - not used for deep sleep wake-up)");
    pinMode(BUTTON_PIN, INPUT_PULLDOWN);
    Serial.println("Interrupt pin configured");
    
    sensorInitialized = true;
    
    Serial.println("\n========================================");
    Serial.println("Test Setup Complete!");
    Serial.println("========================================");
    Serial.println("Send '1' through serial monitor to enter deep sleep");
    Serial.print("Wake source: Motion interrupt (GPIO ");
    Serial.print(MOTION_INT_PIN);
    Serial.println(")");
    Serial.println("NOTE: Apply HIGH signal (3.3V) to GPIO ");
    Serial.print(MOTION_INT_PIN);
    Serial.println(" to wake device");
    Serial.println("========================================\n");
    
    // Wait for serial command '1' to enter deep sleep
    waitForSerialCommand();
    
    // Enter deep sleep (will wake on motion interrupt)
    if (sensorInitialized && motionDetectionConfigured) {
      enterDeepSleep();
    }
  }
}

void loop() {
  // Update button handler (check for long press to power off)
  updateButtonHandler();
  
  // This should not be reached during normal test operation
  // Device should be in deep sleep most of the time
  // If we reach here, wait for serial command to re-enter deep sleep
  if (sensorInitialized && motionDetectionConfigured) {
    waitForSerialCommand();
    enterDeepSleep();
  }
}

