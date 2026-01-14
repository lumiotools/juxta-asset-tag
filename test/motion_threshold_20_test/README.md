# Motion Detection Test - Threshold 20

This test validates BMI323 motion detection functionality with slope threshold set to 20 for both any-motion and no-motion detection.

## Purpose
- Test any-motion detection with threshold = 20
- Test no-motion detection with threshold = 20
- Serial print motion events in real-time
- Display accelerometer and gyroscope data

## Hardware Requirements
- ESP32-C6 board
- BMI323 IMU sensor
- Connections:
  - BMI323 SDA → GPIO 0
  - BMI323 SCL → GPIO 1
  - BMI323 INT1 → GPIO 5 (RTC-capable GPIO)
  - I2C Address: 0x69

## Configuration
- **Any-motion slope threshold**: 20
- **No-motion slope threshold**: 20
- **Hysteresis**: 5
- **Wait time**: 5
- **Any-motion duration**: 1 sample (~20ms)
- **No-motion duration**: 8191 samples (~163 seconds)

## Test Instructions
1. Upload the sketch to ESP32-C6
2. Open Serial Monitor (115200 baud)
3. Wait for initialization messages
4. **Test ANY-MOTION**:
   - Move the device (shake, tilt, or rotate)
   - Watch for "ANY-MOTION DETECTED" message
5. **Test NO-MOTION**:
   - Keep device completely still
   - Wait for ~2.73 minutes
   - Watch for "NO-MOTION DETECTED" message

## Expected Output
```
========================================
Motion Detection Test - Threshold 20
========================================
Initializing IMU sensor...
BMI323 initialized successfully using Bosch API
...
╔════════════════════════════════════════════╗
║   MOTION DETECTION CONFIGURATION          ║
╠════════════════════════════════════════════╣
║ Any-motion slope_thres:  20                ║
║ Any-motion duration:     1 samples         ║
╠════════════════════════════════════════════╣
║ No-motion slope_thres:   20                ║
║ No-motion duration:      8191 samples      ║
║ (~163 seconds at 50Hz)                     ║
╚════════════════════════════════════════════╝
...
*** INTERRUPT DETECTED ***
╔════════════════════════════════════╗
║   >>> ANY-MOTION DETECTED <<<      ║
║   Device movement detected!        ║
╚════════════════════════════════════╝
```

## Files
- `motion_threshold_20_test.ino` - Main test sketch
- `motion_sleep_manager.h` - Motion detection manager (threshold 20)
- `imu_sensor.h` - BMI323 sensor wrapper

## Notes
- The test continuously prints accelerometer and gyroscope data every 2 seconds
- Motion interrupts are printed immediately when detected
- Threshold 20 is more sensitive than the default value of 9
