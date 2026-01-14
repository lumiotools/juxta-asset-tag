// Motion Detection Test with Threshold 20
// Based on motion_sleep_manager.h
// Tests any-motion and no-motion detection with slope threshold = 20
//
// Hardware Connections:
// - BMI323 SDA → GPIO 0 (I2C_SDA_PIN)
// - BMI323 SCL → GPIO 1 (I2C_SCL_PIN)
// - BMI323 INT1 → GPIO 5 (MOTION_INT_PIN)
// - I2C Address: 0x69

#include <Arduino.h>
#include "imu_sensor.h"
#include "motion_sleep_manager.h"
#include "power_latch.h"

// Global motion tracking variables (required by motion_sleep_manager.h)
volatile bool motionInterruptFlag = false;
unsigned long lastMotionTime = 0;
unsigned long noMotionStartTime = 0;
bool noMotionTracking = false;

// IMU sensor instance
IMUSensor imu;

void setup() {
  Serial.begin(115200);
  delay(200);  // Give time for serial monitor to connect
  initPowerLatch();
  
  Serial.println("\n========================================");
  Serial.println("Motion Detection Test - Threshold 20");
  Serial.println("========================================");
  Serial.println("Testing any-motion and no-motion detection");
  Serial.println("Slope threshold: 20 (both any-motion and no-motion)");
  Serial.println("========================================\n");
  
  // Initialize IMU sensor
  Serial.println("Initializing IMU sensor...");
  if (!imu.begin()) {
    Serial.println("ERROR: IMU initialization failed!");
    Serial.println("Check wiring and power. Halting.");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("IMU initialized successfully\n");
  
  // Configure BMI323 interrupts for motion detection
  Serial.println("Configuring motion detection interrupts...");
  if (!MotionSleepManager::configureBMI323Interrupts(&imu)) {
    Serial.println("ERROR: Failed to configure motion interrupts!");
    Serial.println("Halting.");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("Motion interrupts configured\n");
  
  // Setup motion interrupt ISR
  MotionSleepManager::setupMotionISR();
  
  // Initialize motion tracking
  lastMotionTime = millis();
  
  Serial.println("\n========================================");
  Serial.println("Test Ready!");
  Serial.println("========================================");
  Serial.println("Instructions:");
  Serial.println("1. Move the device to trigger ANY-MOTION");
  Serial.println("2. Keep device still to trigger NO-MOTION");
  Serial.println("3. Monitor serial output for detection events");
  Serial.println("========================================\n");
}

void loop() {
  // Check if motion interrupt was detected
  Serial.print("motion flag: ");
  Serial.println(motionInterruptFlag);
  if (motionInterruptFlag) {
    motionInterruptFlag = false; // Clear flag
    
    // Small delay to allow sensor to update interrupt status
    delay(50);
    
    // Read interrupt status to determine which interrupt occurred
    if (imu.isBoschApiInitialized()) {
      struct bmi3_dev* dev = imu.getBoschDevice();
      if (dev != nullptr) {
        uint16_t int_status = 0;
        int8_t rslt = bmi323_get_int1_status(&int_status, dev);
        
        if (rslt == BMI323_OK) {
          Serial.println("\n*** INTERRUPT DETECTED ***");
          Serial.print("Timestamp: ");
          Serial.print(millis());
          Serial.println(" ms");
          Serial.print("Interrupt Status: 0x");
          Serial.println(int_status, HEX);
          
          if (int_status & BMI3_INT_STATUS_ANY_MOTION) {
            Serial.println("╔════════════════════════════════════╗");
            Serial.println("║   >>> ANY-MOTION DETECTED <<<      ║");
            Serial.println("║   Device movement detected!        ║");
            Serial.println("╚════════════════════════════════════╝");
            
            // Reset no-motion timer
            lastMotionTime = millis();
            noMotionStartTime = 0;
            noMotionTracking = false;
          }
          
          if (int_status & BMI3_INT_STATUS_NO_MOTION) {
            Serial.println("╔════════════════════════════════════╗");
            Serial.println("║   >>> NO-MOTION DETECTED <<<       ║");
            Serial.println("║   Device has been still!           ║");
            Serial.println("╚════════════════════════════════════╝");
          }
          
          // Check for error status
          if (int_status & BMI3_INT_STATUS_ERR) {
            Serial.println("⚠️  WARNING: Error status interrupt detected!");
          }
          
          Serial.println();
        } else {
          Serial.print("ERROR: Failed to read interrupt status. Error code: ");
          Serial.println(rslt);
        }
      }
    }
  }
  
  // Read and display accelerometer data for reference
  static unsigned long lastDataPrint = 0;
  if (millis() - lastDataPrint > 2000) {  // Update every 2 seconds
    lastDataPrint = millis();
    
    // Update IMU data
    imu.update();
    IMUData data = imu.getIMUData();
    
    // Calculate magnitude
    float magnitude = sqrt(
      data.accelerometer.x * data.accelerometer.x + 
      data.accelerometer.y * data.accelerometer.y + 
      data.accelerometer.z * data.accelerometer.z
    );
    
    // Print accelerometer data
    Serial.print("Accel [m/s²]: X=");
    Serial.print(data.accelerometer.x, 2);
    Serial.print(" Y=");
    Serial.print(data.accelerometer.y, 2);
    Serial.print(" Z=");
    Serial.print(data.accelerometer.z, 2);
    Serial.print(" | Mag=");
    Serial.print(magnitude, 2);
    Serial.print(" m/s² (");
    Serial.print(magnitude / 9.80665f, 2);
    Serial.println("g)");
    
    // Print gyroscope data
    Serial.print("Gyro [°/s]:   X=");
    Serial.print(data.gyroscope.x, 2);
    Serial.print(" Y=");
    Serial.print(data.gyroscope.y, 2);
    Serial.print(" Z=");
    Serial.println(data.gyroscope.z, 2);
    
    Serial.println();
  }
  
  delay(10); // Small delay to prevent overwhelming CPU
}
