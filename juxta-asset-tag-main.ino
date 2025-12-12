// ESP32-S3 Asset Tag - IMU & GPS data transmission

#include "imu_sensor.h"
#include "gps_sensor.h"
#include "spi_flash_handler.h"
#include "customwifi.h"
#include "time_sync.h"
#include "data_queue.h"
#include "transmission_handler.h"
#include "nvs_config.h"
#include "battery_monitor.h"
#include "battery_indicator_led.h"
#include "ble_config.h"
#include "cycle_handler.h"
#include "unified_csv_storage.h"
#include <Adafruit_NeoPixel.h>
#include <esp_system.h>
#include "esp_sleep.h"
#include <Ticker.h>

// Device ID and Version Configuration (hardcoded to save memory)
const char* DEVICE_ID = "ASSET_TAG_WJ";  // Change this for each device
const char* DEVICE_VERSION = "v2.0.0";   // Device firmware/hardware version

const int STATUS_LED_PIN = D1;
const int STATUS_LED_COUNT = 2; // 2 pixels: pixel 0 for device status, pixel 1 for battery status

// Status LED instance
Adafruit_NeoPixel statusLED(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

// Status LED blinking for transmission
Ticker statusLedTicker;
volatile bool statusLedBlinkState = false;
volatile uint8_t blinkR = 0, blinkG = 0, blinkB = 0;
uint8_t restoreR = 0, restoreG = 255, restoreB = 0; // Default to green

// IMU reading ticker for 100Hz sampling
Ticker imuReadTicker;
volatile bool imuDataReady = false; // Flag set by ISR when it's time to read IMU

// Battery Indicator LED instance (shares statusLED NeoPixel, uses pixel 1)
BatteryIndicatorLED batteryIndicatorLED;

// Transmission handler for failed data retry logic
TransmissionHandler transmissionHandler;

// Cycle handler for data transmission operations
CycleHandler* cycleHandler = nullptr;

// Create sensor instances
IMUSensor imuSensor;
GPSSensor gpsSensor;
SPIFlashHandler spiFlash;
UnifiedCSVStorage unifiedCSVStorage;

// Sensor status flags
bool imuInitialized = false;
bool gpsInitialized = false;
bool flashInitialized = false;

// Configuration: Cycle duration is now configurable via BLE and stored in NVS
// Default is 900 seconds (15 minutes) if not set via BLE
// Cycle duration is stored in seconds in NVS

// IMU data collection at 100Hz (10ms intervals)
const unsigned long IMU_READ_INTERVAL_US = 10000; // 10ms = 10000 microseconds for 100Hz

// IMU data is now stored in external flash as CSV strings (entire flash as circular buffer)
// Accumulate readings in buffer, then format and store as CSV

// Timing variables for cycle and BLE management
unsigned long long bleStartTime = 0; // Time when BLE was started
unsigned long long cycleStartTime = 0; // Time when current cycle started
unsigned long lastImuReadTime = 0; // Last time IMU was read (in microseconds)

// ISR: IMU read ticker callback - triggers IMU data reading at 100Hz
// Called every 10ms by ticker interrupt
void IRAM_ATTR imuReadISR() {
  // Set flag to indicate IMU data should be read
  // Actual reading happens in main loop to avoid long operations in ISR
  imuDataReady = true;
}

// Attempt time synchronization if needed (WiFi must be connected first)
void attemptTimeSyncIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Attempting NTP time sync...");
    bool syncSuccess = TimeSync::syncTimeNTP();
    if (syncSuccess) {
      Serial.println("Time sync successful");
    } else {
      Serial.println("Time sync failed, will continue without synced time");
    }
  } else {
    Serial.println("WiFi not connected - skipping time sync");
  }
}

// USB detection function for ESP32-S3
// Checks if USB is connected by verifying USB Serial availability
bool isUSBConnected() {
  // On ESP32-S3, USB Serial JTAG controller is active when USB is connected
  // Serial object is available when USB is connected and Serial.begin() has been called
  // This is a reliable method for ESP32-S3 to detect USB connection
  int v = digitalRead(D2);
  return v?true: false;  // Returns true if USB Serial is available (USB connected)
}

// Helper function to set pixel color and display
void setPixelAndShow(uint8_t pixel, uint8_t r, uint8_t g, uint8_t b) {
  statusLED.setPixelColor(pixel, statusLED.Color(r, g, b));
  statusLED.show();
}

void updateStatusLED() {
  // Green if all sensors (IMU, GPS, and Flash) initialized, Red if any failed
  if (imuInitialized && gpsInitialized && flashInitialized) {
    restoreR = 0;
    restoreG = 255;
    restoreB = 0;
    setPixelAndShow(0, 0, 255, 0); // Green on pixel 0
  } else {
    restoreR = 255;
    restoreG = 0;
    restoreB = 0;
    setPixelAndShow(0, 255, 0, 0); // Red on pixel 0
  }
}

// Toggle function for status LED blinking
void toggleStatusLED() {
  statusLedBlinkState = !statusLedBlinkState;
  if (statusLedBlinkState) {
    setPixelAndShow(0, blinkR, blinkG, blinkB);
  } else {
    setPixelAndShow(0, 0, 0, 0); // Off
  }
}

// Start blinking status LED with specified color
long long startStatusLEDBlink(uint8_t r, uint8_t g, uint8_t b) {
  blinkR = r;
  blinkG = g;
  blinkB = b;
  statusLedBlinkState = false;
  toggleStatusLED();
  // statusLedTicker.attach_ms(20, toggleStatusLED); // 20ms = 50Hz blink
  // statusLedTicker.attach_ms(4000, toggleStatusLED); // 20ms = 50Hz blink
  return TimeSync::getCurrentTimeMillis();
}

// Stop blinking and restore to normal status color
void stopStatusLEDBlink(long long t) {
  // delay(4000 - (TimeSync::getCurrentTimeMillis() - t));
  // statusLedTicker.detach();
  toggleStatusLED(); // Ensure LED is on before restoring
  setPixelAndShow(0, restoreR, restoreG, restoreB);
}


void setup() {
  Serial.begin(115200);
  delay(1000);
  statusLED.begin();  // Initialize NeoPixel first
  setPixelAndShow(0, 255, 0, 255); // Magenta/Purple on pixel 0 (unique boot color)
  delay(1000);
  pinMode(D2,INPUT);
  
  // Initialize NVS for WiFi credentials storage
  NVSConfig::initializeNVS();
  
  // Initialize External SPI Flash
  Serial.println("Initializing SPI Flash...");
  flashInitialized = spiFlash.begin();
  if (flashInitialized) {
    Serial.println("SPI Flash initialized successfully");
    Serial.print("Flash Capacity: ");
    Serial.print(spiFlash.getCapacity());
    Serial.println(" bytes");
  } else {
    Serial.println("SPI Flash initialization failed");
  }

  // Initialize Unified CSV Storage (uses entire external flash)
  Serial.println("Initializing Unified CSV Storage...");
  if (!unifiedCSVStorage.begin(&spiFlash)) {
    Serial.println("Warning: Unified CSV Storage initialization failed!");
  }

  // bool x = spiFlash.eraseChip();
  // String y = x?"true":"false";
  // Serial.print("erased :");
  // Serial.println(y);  
  // // Reset read and write pointers to 0 after chip erase
  // if (x) {
  //   unifiedCSVStorage.clear(); // This sets both pointers to 0 and saves to NVS
  //   Serial.println("Read and write pointers reset to 0");
  // }
  // delay(-100);
  
  // Initialize transmission handler and data queue (loads from flash, calculates max size on first boot)
  Serial.println("Initializing data queue...");
  if (!transmissionHandler.begin(&spiFlash)) {
    Serial.println("Warning: Data queue initialization failed!");
  }
  
  // Check ESP reset reason to determine if this is first cycle
  esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.print("Reset reason: ");
  Serial.println(resetReason);
  
  // Determine if this is first cycle (power-on or reset button) vs subsequent (deep sleep wake-up)
  bool isFirstCycle = (resetReason == ESP_RST_POWERON || resetReason == ESP_RST_EXT);
  
  // Initialize Battery Monitor ADC
  BatteryMonitor::initializeADC();
  delay(100);
  
  // Initialize Status LED first (both pixels on same pin)
  // statusLED.begin();  // Initialize GPIO first!
  statusLED.setBrightness(70);
  statusLED.show();
  updateStatusLED(); // Show red initially on pixel 0 (not initialized)
  
  // Initialize Battery Indicator LED (uses shared statusLED instance, pixel 1)
  batteryIndicatorLED.begin(&statusLED, 1);
  batteryIndicatorLED.updateBatteryLED(); // Set initial color based on battery on pixel 1
  delay(100);
  
  // Initialize IMU sensor
  Serial.println("Initializing IMU sensor...");
  imuInitialized = imuSensor.begin();
  if (imuInitialized) {
    Serial.println("IMU initialized successfully");
  } else {
    Serial.println("IMU initialization failed - continuing without IMU");
  }
  
  // Initialize GPS sensor
  Serial.println("Initializing GPS sensor...");
  gpsInitialized = gpsSensor.begin();
  if (gpsInitialized) {
    Serial.println("GPS initialized successfully");
  } else {
    Serial.println("GPS initialization failed - continuing without GPS");
  }
  
  // One-time credential setup (comment out after first upload)
  // NVSConfig::setWiFiSSID("GarageNeo");
  // NVSConfig::setWiFiPassword("G@r@ge#123");
  
  // Connect to WiFi on first cycle (power-on/reset) to attempt time sync
  // On subsequent cycles, WiFi will be connected in loop() if needed
  if (isFirstCycle) {
    CustomWiFi::connectWiFi();
    // Attempt time sync if WiFi connected (regardless of whether it was previously synced)
    attemptTimeSyncIfNeeded();
  } else {
    Serial.println("Subsequent cycle - skipping WiFi connection in setup (will connect in loop if needed)");
  }

  // Update status LED based on sensor initialization
  updateStatusLED();
  delay(200);

  // Initialize BLE for both first and subsequent cycles
  Serial.println("Initializing BLE...");
  BLEConfig::setDeviceId(DEVICE_ID);
  BLEConfig::setDeviceVersion(DEVICE_VERSION);
  BLEConfig::begin();
  bleStartTime = TimeSync::getCurrentTimeMillis();
  // Initialize cycle timing
  cycleStartTime = TimeSync::getCurrentTimeMillis();
  lastImuReadTime = micros(); // Initialize IMU read timing
  
  delay(100);


  // Initialize Cycle Handler
  Serial.println("Initializing Cycle Handler...");
  cycleHandler = new CycleHandler(
           DEVICE_ID,
    &gpsSensor,
    &spiFlash,
    &transmissionHandler,
    &batteryIndicatorLED,
    &statusLED,
    0  // Status LED pixel 0
  );
  
  // Set restore color based on sensor status
  if (imuInitialized && gpsInitialized && flashInitialized) {
    cycleHandler->setStatusLEDRestoreColor(0, 255, 0); // Green
  } else {
    cycleHandler->setStatusLEDRestoreColor(255, 0, 0); // Red
  }
  
  // IMU data is stored in external flash (5MB) - no RAM buffer needed
  // Start IMU reading ticker at 100Hz (10ms intervals)
  if (imuInitialized && unifiedCSVStorage.isInitialized()) {
    imuReadTicker.attach_ms(10, imuReadISR); // 10ms = 100Hz
    Serial.println("IMU sampling: 100Hz (stored as CSV to flash)");
  } else {
    if (!imuInitialized) {
      Serial.println("IMU ticker not started - sensor failed");
    }
    if (!unifiedCSVStorage.isInitialized()) {
      Serial.println("IMU ticker not started - CSV storage failed");
    }
  }
  
  Serial.println("Setup complete - ready for operation");
  delay(100);
}
  
// ========== HELPER FUNCTIONS ==========

// Execute cycle transmission (call when you want to send data)
// IMU data is read from flash in chunks by cycle handler
CycleResult executeCycleTransmission() {
  if (!cycleHandler) {
    Serial.println("ERROR: Cycle handler not initialized");
    return CYCLE_FAILED;
  }
  
  // Execute cycle - cycle handler will read CSV entries from unified storage
  CycleResult result = cycleHandler->executeCycleFromUnifiedCSV(&unifiedCSVStorage);
  
  // State is already saved by cycle handler
  return result;
}

void loop() {
  // Static variables for state tracking
  static unsigned long lastBatteryUpdate = 0;
  static bool lastUSBState = isUSBConnected();
  static bool firstCycleComplete = false;
  static bool bleConnectedDuringFirstCycle = false;
  static unsigned long long bleConnectionTime = 0;
  
  // Current time
  unsigned long long currentTime = TimeSync::getCurrentTimeMillis();
  
  // ========== IMU READING (INTERRUPT-DRIVEN) ==========
  // IMU data reading triggered by ticker ISR at 100Hz (10ms intervals)
  // Check if ISR flagged that it's time to read IMU
  if (imuDataReady && flashInitialized) {
    imuDataReady = false; // Clear flag
    
    // Read IMU data and accumulate in buffer
    if (imuInitialized && unifiedCSVStorage.isInitialized()) {
      imuSensor.update();
      IMUData imuData = imuSensor.getIMUData();
      
      // Static buffer to accumulate readings
      static TimestampedIMUReading readingBuffer[100]; // Buffer 100 readings
      static int bufferIndex = 0;
      
      // Add reading to buffer with timestamp
      readingBuffer[bufferIndex].accX = imuData.accelerometer.x;
      readingBuffer[bufferIndex].accY = imuData.accelerometer.y;
      readingBuffer[bufferIndex].accZ = imuData.accelerometer.z;
      readingBuffer[bufferIndex].gyrX = imuData.gyroscope.x;
      readingBuffer[bufferIndex].gyrY = imuData.gyroscope.y;
      readingBuffer[bufferIndex].gyrZ = imuData.gyroscope.z;
      readingBuffer[bufferIndex].timestamp = TimeSync::getCurrentTimeMillis();
      
      bufferIndex++;
      
      // When buffer is full, format and store as CSV
      if (bufferIndex >= 100) {
        if (!unifiedCSVStorage.writeIMUReadings(readingBuffer, 100)) {
          Serial.println("ERROR: Failed to write IMU readings to CSV storage");
        }
        bufferIndex = 0; // Reset buffer
      }
    }
  }
  
  // Periodic status update removed for production - light sleep enabled
  
  // ========== USB STATE MONITORING ==========
  // USB state monitoring
  bool currentUSBState = isUSBConnected();
  if (currentUSBState != lastUSBState) {
    if (currentUSBState) {
      batteryIndicatorLED.disable();
      Serial.println("USB connected - Battery LED disabled");
    } else {
      batteryIndicatorLED.enable();
      Serial.println("USB disconnected - Battery LED enabled");
    }
    lastUSBState = currentUSBState;
  }
  
  // Battery LED disabled between cycles (only on during cycle execution)
  // Battery LED is turned on at cycle start and off at cycle completion
  
  // ========== CYCLE LOGIC ==========
  
  if (!firstCycleComplete) {
    // ===== FIRST CYCLE: BLE Configuration Window =====
    // Wait for 1 minute OR (connection time + 30 seconds), whichever is longer
    
    // Check if BLE just connected
    if (BLEConfig::isConnected() && !bleConnectedDuringFirstCycle) {
      bleConnectedDuringFirstCycle = true;
      bleConnectionTime = currentTime;
      Serial.print("BLE connected during first cycle at: ");
      Serial.print(bleConnectionTime);
      Serial.println(" ms");
    }
    
    // Calculate when first cycle should end
    unsigned long long firstCycleEndTime;
    unsigned long long timeElapsed = currentTime - bleStartTime;
    unsigned long long timeRemaining = 0;
    
    if (bleConnectedDuringFirstCycle) {
      // User connected - calculate both options
      unsigned long long connectionPlusThirty = bleConnectionTime + 30000; // +30s
      unsigned long long minimumOneMinute = bleStartTime + 30000; // 1 min from start
      
      // Use whichever is longer
      firstCycleEndTime = (connectionPlusThirty > minimumOneMinute) 
                          ? connectionPlusThirty 
                          : minimumOneMinute;
      
      timeRemaining = (firstCycleEndTime > currentTime) ? (firstCycleEndTime - currentTime) : 0;
      
      // Debug output every 10 seconds
      static unsigned long long lastDebugTime = 0;
      if (currentTime - lastDebugTime >= 10000) {
        lastDebugTime = currentTime;
        Serial.print("[FIRST CYCLE] Time elapsed: ");
        Serial.print(timeElapsed / 1000);
        Serial.print("s, Time remaining: ");
        Serial.print(timeRemaining / 1000);
        Serial.print("s, End time: ");
        Serial.print(firstCycleEndTime);
        Serial.print(" ms (BLE connected: ");
        Serial.print((currentTime - bleConnectionTime) / 1000);
        Serial.println("s ago)");
      }
    } else {
      // No connection yet - default 1 minute wait
      firstCycleEndTime = bleStartTime + 60000;
      timeRemaining = (firstCycleEndTime > currentTime) ? (firstCycleEndTime - currentTime) : 0;
      
      // Debug output every 10 seconds
      static unsigned long long lastDebugTime = 0;
      if (currentTime - lastDebugTime >= 10000) {
        lastDebugTime = currentTime;
        Serial.print("[FIRST CYCLE] Time elapsed: ");
        Serial.print(timeElapsed / 1000);
        Serial.print("s, Time remaining: ");
        Serial.print(timeRemaining / 1000);
        Serial.println("s (waiting for BLE connection...)");
      }
    }
    
    // Check if it's time to execute cycle
    if (currentTime >= firstCycleEndTime) {
      Serial.println("\n========================================");
      Serial.println("FIRST CYCLE: BLE wait period complete");
      Serial.print("Total wait time: ");
      Serial.print(timeElapsed / 1000);
      Serial.println(" seconds");
      Serial.println("Executing transmission...");
      Serial.println("========================================\n");
      
      CycleResult result = executeCycleTransmission();
      
      // Handle result
      switch(result) {
        case CYCLE_SUCCESS_BLE:
          Serial.println("First cycle completed via BLE");
          break;
        case CYCLE_SUCCESS_WIFI:
          Serial.println("First cycle completed via WiFi");
          break;
        case CYCLE_SUCCESS_STORED:
          Serial.println("First cycle data stored to flash");
          break;
        case CYCLE_FAILED:
          Serial.println("First cycle failed");
          break;
      }
      
      firstCycleComplete = true;
      cycleStartTime = currentTime;
      
      Serial.print("First cycle complete. Next cycle will start in ");
      Serial.print(NVSConfig::getCycleTime());
      Serial.println(" seconds");
      
      // Turn off BLE after first cycle completes (was kept on during first cycle period)
      if (BLEConfig::isEnabled()) {
        Serial.println("First cycle complete - turning off BLE");
        BLEConfig::stop();
      }
    }
    
  } else {
    // ===== SUBSEQUENT CYCLES: Normal Timing =====
    
    uint32_t cycleTimeSeconds = NVSConfig::getCycleTime();
    unsigned long long cycleTimeMs = (unsigned long long)cycleTimeSeconds * 1000ULL;
    unsigned long long timeElapsed = currentTime - cycleStartTime;
    unsigned long long timeRemaining = (timeElapsed < cycleTimeMs) ? (cycleTimeMs - timeElapsed) : 0;
    
    // Debug output every 60 seconds (or when close to cycle time)
    static unsigned long long lastDebugTime = 0;
    bool shouldDebug = false;
    
    if (timeRemaining > 0 && timeRemaining <= 10000) {
      // Less than 10 seconds remaining - debug every second
      shouldDebug = (currentTime - lastDebugTime >= 1000);
    } else if (timeRemaining > 0) {
      // More than 10 seconds remaining - debug every 60 seconds
      shouldDebug = (currentTime - lastDebugTime >= 60000);
    }
    
    if (shouldDebug && timeElapsed > 0) {
      lastDebugTime = currentTime;
      Serial.print("[CYCLE TIMING] Cycle time: ");
      Serial.print(cycleTimeSeconds);
      Serial.print("s, Elapsed: ");
      Serial.print(timeElapsed / 1000);
      Serial.print("s, Remaining: ");
      Serial.print(timeRemaining / 1000);
      Serial.print("s, Next cycle at: ");
      Serial.print((cycleStartTime + cycleTimeMs) / 1000);
      Serial.println("s");
    }
    
    if (currentTime - cycleStartTime >= cycleTimeMs) {
      Serial.println("\n========================================");
      Serial.println("NORMAL CYCLE: Cycle time reached");
      Serial.print("Cycle time: ");
      Serial.print(cycleTimeSeconds);
      Serial.print(" seconds, Elapsed: ");
      Serial.print(timeElapsed / 1000);
      Serial.println(" seconds");
      Serial.println("Executing transmission...");
      Serial.println("========================================\n");
      
      CycleResult result = executeCycleTransmission();
      
      // Handle result
      switch(result) {
        case CYCLE_SUCCESS_BLE:
          Serial.println("Cycle completed via BLE");
          break;
        case CYCLE_SUCCESS_WIFI:
          Serial.println("Cycle completed via WiFi");
          break;
        case CYCLE_SUCCESS_STORED:
          Serial.println("Cycle data stored to flash");
          break;
        case CYCLE_FAILED:
          Serial.println("Cycle failed");
          break;
      }
      
      cycleStartTime = currentTime;
      lastDebugTime = currentTime; // Reset debug timer
      
      Serial.print("Cycle complete. Next cycle will start in ");
      Serial.print(cycleTimeSeconds);
      Serial.print(" seconds (at ");
      Serial.print((cycleStartTime + cycleTimeMs) / 1000);
      Serial.println("s)");
    }
  }
  
  // ========== LIGHT SLEEP (POWER SAVING) ==========
  // Enter light sleep to save power when no immediate tasks
  // Timer wakeup every 10ms ensures ticker interrupt is processed
  // Only after first cycle to keep BLE responsive during initial connection
  if (firstCycleComplete && !imuDataReady) {
    esp_sleep_enable_timer_wakeup(10000); // Wake every 10ms (10,000 microseconds)
    esp_light_sleep_start();
  } else if (!firstCycleComplete) {
    // During first cycle, keep CPU responsive for BLE with minimal delay
    delay(1);
  }
}