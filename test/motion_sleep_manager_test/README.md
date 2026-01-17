# Motion Sleep Manager Test

This test sketch verifies the motion detection and deep sleep functionality using the `MotionSleepManager` class.

## Features

### ✨ Visual Motion Detection
- **Prominent ASCII art banner** when motion is detected
- Clear differentiation from no-motion timer logs
- Timestamp and reset confirmation

### ⏱️ No-Motion Timer Display
- **Progress bar** showing countdown to sleep
- Elapsed time and remaining time
- Percentage complete indicator
- Status messages at different progress levels

### 💤 Deep Sleep Management
- Automatic entry into deep sleep after no-motion timeout
- Wake-up on motion detection
- Proper GPIO and power management

## Hardware Connections

| Component | GPIO Pin | Notes |
|-----------|----------|-------|
| BMI323 SDA | GPIO 0 | I2C Data |
| BMI323 SCL | GPIO 1 | I2C Clock |
| BMI323 INT1 | GPIO 5 | Motion interrupt (RTC-capable) |
| Power Latch | GPIO 4 | Power control (RTC-capable) |
| Button | GPIO 10 | Power off button |
| I2C Address | 0x69 | BMI323 device address |

## Test Configuration

### Adjustable Parameters

```cpp
#define ENABLE_DEEP_SLEEP true          // Enable/disable actual deep sleep
#define NO_MOTION_TEST_TIME_MS 60000    // Test timeout (default: 1 minute)
```

- Set `ENABLE_DEEP_SLEEP` to `false` for testing without actual sleep
- Adjust `NO_MOTION_TEST_TIME_MS` for faster testing (default 5 min in production)

## Usage Instructions

### 1. Upload the Sketch
- Open `motion_sleep_manager_test.ino` in Arduino IDE
- Select board: `ESP32C6 Dev Module`
- Upload to device

### 2. Testing Motion Detection
1. Open Serial Monitor (115200 baud)
2. **Move the device** - You'll see:
   ```
   ╔════════════════════════════════════════════════════════════╗
   ║   ███╗   ███╗ ██████╗ ████████╗██╗ ██████╗ ███╗   ██╗    ║
   ║   ...                                                      ║
   ║              🔥 MOTION DETECTED! 🔥                        ║
   ╚════════════════════════════════════════════════════════════╝
   ```
3. Timer resets automatically

### 3. Testing No-Motion Countdown
1. **Keep device still** after motion
2. After 1 second, countdown starts:
   ```
   ┌────────────────────────────────────────────────────────────┐
   │ NO-MOTION TIMER: 0:15 / 1:00 elapsed (25%)                │
   │ [████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░] │
   │ Time remaining: 0:45                                       │
   │ 🔍 Monitoring for motion...                                │
   └────────────────────────────────────────────────────────────┘
   ```
3. Progress updates every 5 seconds
4. Visual indicators change at 25%, 50%, 75%, 100%

### 4. Testing Deep Sleep
1. Keep device still for full countdown
2. Device enters deep sleep automatically
3. **Move device** to wake from sleep
4. Cycle repeats

### 5. Power Off
- **Press and hold button** for 5 seconds
- Device powers off immediately

## Expected Output

### On Startup
```
================================================================================
    MOTION SLEEP MANAGER TEST
================================================================================

Initializing power latch...
Device powered on normally
Initializing time sync...
Initializing IMU sensor...
✓ IMU initialized successfully
Configuring motion detection...
✓ INT1 pin configured
✓ Accelerometer & motion detection configured
✓ Interrupts mapped to INT1
✓ BMI323 motion detection enabled successfully
✓ Motion ISR attached to GPIO 5 (RISING edge)

================================================================================
    TEST READY - MONITORING FOR MOTION
================================================================================
```

### During Motion
```
╔════════════════════════════════════════════════════════════════════════════╗
║                      🔥 MOTION DETECTED! 🔥                                ║
║   Timestamp: 42 seconds                                                    ║
║   No-motion timer has been RESET                                           ║
╚════════════════════════════════════════════════════════════════════════════╝
```

### During No-Motion
```
┌────────────────────────────────────────────────────────────────────────────┐
│ NO-MOTION TIMER: 0:45 / 1:00 elapsed (75%)                                │
│ [█████████████████████████████████████░░░░░░░░░░░░░] 75%                  │
│ Time remaining: 0:15                                                       │
│ ⚠️  Almost ready for deep sleep...                                         │
└────────────────────────────────────────────────────────────────────────────┘
📊 Accel [m/s²]: X=-0.12 Y=0.05 Z=9.78 | Mag=9.78 m/s² (1.00g)
```

### Entering Deep Sleep
```
================================================================================
    NO-MOTION TIMEOUT REACHED
================================================================================

Device has been still for the configured duration
Preparing to enter deep sleep in 3 seconds...
Move device now to cancel!

========== ENTERING DEEP SLEEP ==========
✓ BMI323 set to low-power mode
✓ Disabling WiFi and BLE
✓ Power latch held HIGH
✓ Wake pin GPIO 5 configured
✓ Wake-up configured on GPIO 5
Entering deep sleep...
```

## Dependencies

The test imports and uses:
- `motion_sleep_manager.h` - Motion detection and sleep management
- `imu_sensor.h` - BMI323 IMU sensor wrapper
- `time_sync.h` - Time tracking utilities
- `power_latch.h` - Power management (in test folder)

## Troubleshooting

### IMU Not Detected
- Check I2C connections (GPIO 0, 1)
- Verify I2C address (0x69)
- Check power supply

### Motion Not Detected
- Verify GPIO 5 connection to BMI323 INT1
- Check interrupt configuration in output
- Try moving device more vigorously

### Won't Wake from Sleep
- GPIO 5 must be RTC-capable (it is ✓)
- Check BMI323 INT1 connection
- Verify power latch is working

### Serial Output Issues
- Ensure baud rate is 115200
- Check USB cable and connection
- Try resetting device

## Notes

- The production timeout is 5 minutes (`NO_MOTION_SLEEP_MS = 300000ms`)
- Test uses 1 minute for faster testing
- Motion threshold is moderate sensitivity (slope_thres = 9)
- Hardware no-motion duration is ~2.73 minutes max (BMI323 limitation)
- Software tracking extends to full 5 minutes (or configured time)

## Files

- `motion_sleep_manager_test.ino` - Main test sketch
- `README.md` - This documentation
- `../power_latch.h` - Power management helper (shared)
- `../../motion_sleep_manager.h` - Motion sleep manager module
- `../../imu_sensor.h` - IMU sensor wrapper
- `../../time_sync.h` - Time utilities

## License

Part of the Juxta Asset Tag project.
