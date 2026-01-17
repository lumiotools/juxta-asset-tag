// Motion Sleep Manager Test
// Tests motion detection and sleep functionality using MotionSleepManager class
// 
// Hardware Connections:
// - BMI323 SDA → GPIO 0 (I2C_SDA_PIN)
// - BMI323 SCL → GPIO 1 (I2C_SCL_PIN)
// - BMI323 INT1 → GPIO 5 (MOTION_INT_PIN - RTC-capable)
// - Power Latch → GPIO 4 (POWER_LATCH_PIN)
// - Button → GPIO 10 (BUTTON_PIN)
// - I2C Address: 0x69

#include <Arduino.h>
#include "../../motion_sleep_manager.h"
#include "../../imu_sensor.h"
#include "../power_latch.h"

// Pin definitions
#define BUTTON_PIN 10

// Global motion tracking variables (required by MotionSleepManager)
volatile bool motionInterruptFlag = false;
unsigned long lastMotionTime = 0;
unsigned long noMotionStartTime = 0;
bool noMotionTracking = false;

// Test state
bool systemInitialized = false;
IMUSensor imuSensor;

// Configuration
#define ENABLE_DEEP_SLEEP false  // Set to false to test without actual sleep
#define NO_MOTION_TEST_TIME_MS 60000  // Test with 1 minute for faster testing (vs 5 min)

void printBanner(const char* message) {
  Serial.println();
  Serial.println("================================================================================");
  Serial.print("    ");
  Serial.println(message);
  Serial.println("================================================================================");
  Serial.println();
}

void printMotionDetected() {
  Serial.println();
  Serial.println("╔════════════════════════════════════════════════════════════════════════════╗");
  Serial.println("║                                                                            ║");
  Serial.println("║   ███╗   ███╗ ██████╗ ████████╗██╗ ██████╗ ███╗   ██╗                    ║");
  Serial.println("║   ████╗ ████║██╔═══██╗╚══██╔══╝██║██╔═══██╗████╗  ██║                    ║");
  Serial.println("║   ██╔████╔██║██║   ██║   ██║   ██║██║   ██║██╔██╗ ██║                    ║");
  Serial.println("║   ██║╚██╔╝██║██║   ██║   ██║   ██║██║   ██║██║╚██╗██║                    ║");
  Serial.println("║   ██║ ╚═╝ ██║╚██████╔╝   ██║   ██║╚██████╔╝██║ ╚████║                    ║");
  Serial.println("║   ╚═╝     ╚═╝ ╚═════╝    ╚═╝   ╚═╝ ╚═════╝ ╚═╝  ╚═══╝                    ║");
  Serial.println("║                                                                            ║");
  Serial.println("║                      🔥 MOTION DETECTED! 🔥                                ║");
  Serial.println("║                                                                            ║");
  Serial.print("║   Timestamp: ");
  Serial.print(millis() / 1000);
  Serial.print(" seconds                                             ║");
  Serial.println();
  Serial.println("║   No-motion timer has been RESET                                           ║");
  Serial.println("║                                                                            ║");
  Serial.println("╚════════════════════════════════════════════════════════════════════════════╝");
  Serial.println();
}

void printNoMotionProgress(unsigned long elapsedMs, unsigned long totalMs) {
  unsigned long remainingMs = totalMs - elapsedMs;
  unsigned long elapsedSec = elapsedMs / 1000;
  unsigned long remainingSec = remainingMs / 1000;
  unsigned long elapsedMin = elapsedSec / 60;
  unsigned long remainingMin = remainingSec / 60;
  
  int percentComplete = (elapsedMs * 100) / totalMs;
  
  // Create progress bar
  char progressBar[52]; // 50 chars + 2 brackets
  progressBar[0] = '[';
  int filledBars = percentComplete / 2; // 50 bars for 100%
  for (int i = 0; i < 50; i++) {
    if (i < filledBars) {
      progressBar[i + 1] = '█';
    } else {
      progressBar[i + 1] = '░';
    }
  }
  progressBar[51] = ']';
  progressBar[52] = '\0';
  
  Serial.println("┌────────────────────────────────────────────────────────────────────────────┐");
  Serial.print("│ NO-MOTION TIMER: ");
  Serial.print(elapsedMin);
  Serial.print(":");
  if ((elapsedSec % 60) < 10) Serial.print("0");
  Serial.print(elapsedSec % 60);
  Serial.print(" / ");
  Serial.print(totalMs / 60000);
  Serial.print(":00 elapsed");
  Serial.print(" (");
  Serial.print(percentComplete);
  Serial.println("%)                   │");
  Serial.print("│ ");
  Serial.print(progressBar);
  Serial.println(" │");
  Serial.print("│ Time remaining: ");
  Serial.print(remainingMin);
  Serial.print(":");
  if ((remainingSec % 60) < 10) Serial.print("0");
  Serial.print(remainingSec % 60);
  Serial.println("                                                 │");
  Serial.println("│                                                                            │");
  if (percentComplete >= 75) {
    Serial.println("│ ⚠️  Almost ready for deep sleep...                                         │");
  } else if (percentComplete >= 50) {
    Serial.println("│ 💤 Halfway to sleep mode                                                   │");
  } else if (percentComplete >= 25) {
    Serial.println("│ ⏱️  Keep device still for sleep mode                                        │");
  } else {
    Serial.println("│ 🔍 Monitoring for motion...                                                │");
  }
  Serial.println("└────────────────────────────────────────────────────────────────────────────┘");
}

void setup() {
  Serial.begin(115200);
  delay(100);  // Brief delay for serial to initialize
  
  printBanner("MOTION SLEEP MANAGER TEST");
  
  // Initialize power latch
  Serial.println("Initializing power latch...");
  initPowerLatch();
  
  // Check wake-up reason
  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  if (wakeReason == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.println("Device woke from deep sleep!");
  } else {
    Serial.println("Device powered on normally");
  }
  
  // Initialize I2C and IMU
  Serial.println("\nInitializing IMU sensor...");
  Serial.print("  - I2C SDA: GPIO ");
  Serial.println(I2C_SDA_PIN);
  Serial.print("  - I2C SCL: GPIO ");
  Serial.println(I2C_SCL_PIN);
  Serial.print("  - I2C Address: 0x");
  Serial.println(IMU_I2C_ADDRESS, HEX);
  
  if (!imuSensor.begin()) {
    Serial.println("ERROR: Failed to initialize IMU sensor!");
    Serial.println("Test cannot proceed without IMU.");
    return;
  }
  Serial.println("✓ IMU initialized successfully");
  
  // Handle wake-up if coming from deep sleep
  if (wakeReason == ESP_SLEEP_WAKEUP_EXT1) {
    MotionSleepManager::handleWakeup(&imuSensor);
  }
  
  // Configure BMI323 interrupts for motion detection
  Serial.println("\nConfiguring motion detection...");
  if (!MotionSleepManager::configureBMI323Interrupts(&imuSensor)) {
    Serial.println("ERROR: Failed to configure motion interrupts!");
    Serial.println("Test cannot proceed without motion detection.");
    return;
  }
  
  // Setup motion interrupt handler
  MotionSleepManager::setupMotionISR();
  
  // Initialize motion tracking
  lastMotionTime = millis();
  
  systemInitialized = true;
  
  printBanner("TEST READY - MONITORING FOR MOTION");
  
  Serial.println("Test Configuration:");
  Serial.print("  - Motion interrupt pin: GPIO ");
  Serial.println(MOTION_INT_PIN);
  Serial.print("  - No-motion timeout: ");
  Serial.print(NO_MOTION_SLEEP_MS / 1000);
  Serial.println(" seconds");
  Serial.print("  - Deep sleep enabled: ");
  Serial.println(ENABLE_DEEP_SLEEP ? "YES" : "NO (test mode)");
  Serial.println();
  Serial.println("Instructions:");
  Serial.println("  1. Move device to trigger motion detection");
  Serial.println("  2. Keep still to see no-motion countdown");
  if (ENABLE_DEEP_SLEEP) {
    Serial.println("  3. Device will enter deep sleep after countdown completes");
    Serial.println("  4. Move device to wake from sleep");
  }
  Serial.println("  5. Press button for 2 seconds to power off");
  Serial.println();
  printBanner("MONITORING STARTED");
}

void loop() {
  // Update button handler for power-off
  updateButtonHandler();
  
  if (!systemInitialized) {
    delay(1000);
    return;
  }
  
  // Check for motion interrupt
  if (motionInterruptFlag) {
    // Clear flag immediately
    motionInterruptFlag = false;
    
    // Read interrupt status from BMI323
    if (imuSensor.isBoschApiInitialized()) {
      struct bmi3_dev* dev = imuSensor.getBoschDevice();
      if (dev != nullptr) {
        uint16_t int_status = 0;
        if (bmi323_get_int1_status(&int_status, dev) == BMI323_OK) {
          if (int_status & BMI3_INT_STATUS_ANY_MOTION) {
            // ANY-MOTION DETECTED - Show prominent log
            printMotionDetected();
            
            // Reset tracking
            lastMotionTime = millis();
            noMotionStartTime = 0;
            noMotionTracking = false;
          } else if (int_status & BMI3_INT_STATUS_NO_MOTION) {
            Serial.println();
            Serial.println("⚠️  Hardware no-motion interrupt triggered");
            Serial.println("    (BMI323 detected no motion for hardware duration)");
            Serial.println();
          }
        }
      }
    }
  }
  
  // Track no-motion duration and display progress
  unsigned long currentTime = millis();
  
  // Start no-motion tracking after 1 second of no motion
  if (lastMotionTime > 0 && (currentTime - lastMotionTime) > 1000) {
    if (!noMotionTracking) {
      noMotionStartTime = currentTime;
      noMotionTracking = true;
      Serial.println();
      Serial.println("🕐 No motion detected - starting countdown timer...");
      Serial.println();
    }
    
    unsigned long noMotionElapsed = currentTime - noMotionStartTime;
    
    // Print progress every 5 seconds
    static unsigned long lastProgressPrint = 0;
    if (noMotionElapsed - lastProgressPrint >= 5000) {
      lastProgressPrint = noMotionElapsed;
      printNoMotionProgress(noMotionElapsed, NO_MOTION_SLEEP_MS);
    }
    
    // Check if timeout reached
    if (noMotionElapsed >= NO_MOTION_SLEEP_MS) {
      Serial.println();
      printBanner("NO-MOTION TIMEOUT REACHED");
      Serial.println("Device has been still for the configured duration");
      Serial.println();
      
      if (ENABLE_DEEP_SLEEP) {
        Serial.println("Preparing to enter deep sleep in 3 seconds...");
        Serial.println("Move device now to cancel!");
        delay(3000);
        
        // Check one more time if motion occurred
        if (motionInterruptFlag) {
          Serial.println("Motion detected - sleep cancelled!");
          motionInterruptFlag = false;
          lastMotionTime = millis();
          noMotionStartTime = 0;
          noMotionTracking = false;
        } else {
          // Enter deep sleep
          MotionSleepManager::enterDeepSleep(&imuSensor);
          // Will not return here
        }
      } else {
        Serial.println("Deep sleep disabled for testing - resetting timer");
        lastMotionTime = millis();
        noMotionStartTime = 0;
        noMotionTracking = false;
        delay(2000);
      }
    }
  }
  
  // Read and display IMU data periodically for debugging
  static unsigned long lastIMURead = 0;
  if (millis() - lastIMURead >= 2000) {  // Every 2 seconds
    lastIMURead = millis();
    
    imuSensor.update();
    IMUData data = imuSensor.getIMUData();
    float magnitude = sqrt(data.accelerometer.x * data.accelerometer.x + 
                          data.accelerometer.y * data.accelerometer.y + 
                          data.accelerometer.z * data.accelerometer.z);
    
    Serial.print("📊 Accel [m/s²]: X=");
    Serial.print(data.accelerometer.x, 2);
    Serial.print(" Y=");
    Serial.print(data.accelerometer.y, 2);
    Serial.print(" Z=");
    Serial.print(data.accelerometer.z, 2);
    Serial.print(" | Mag=");
    Serial.print(magnitude, 2);
    Serial.print(" m/s² (");
    Serial.print(magnitude / 9.80665, 2);
    Serial.println("g)");
  }
  
  delay(100);  // Small delay to prevent overwhelming serial
}
