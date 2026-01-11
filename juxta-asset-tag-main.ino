// ESP32-S3 Asset Tag - IMU & GPS data transmission

// Note: BMI3XY_SensorAPI-main library is installed in Arduino libraries folder
// Arduino IDE will automatically compile it - no wrapper needed

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
#include "gps_scenario_handler.h"
#include "button_handler.h"
#include "motion_sleep_manager.h"
#include "device_id.h"
#include <Adafruit_NeoPixel.h>
#include <esp_system.h>
#include "esp_sleep.h"
#include <Ticker.h>
#include "driver/gpio.h"

// Power latch pin (IO4) configuration
#define POWER_LATCH_PIN 4

// Device ID and Version Configuration
const char* DEVICE_VERSION = "v2.0.0";   // Device firmware/hardware version

// Device ID buffer (generated from MAC address)
static char deviceIdBuffer[32];  // "ASSET_TAG_" (10) + MAC (12) + null terminator = 23 chars max

// Initialize device ID at global scope (before setup)
static void initializeDeviceId() {
  DeviceID::getDeviceId(deviceIdBuffer, sizeof(deviceIdBuffer));
}
static bool _deviceIdInitialized = (initializeDeviceId(), true);

const char* DEVICE_ID = deviceIdBuffer;

const int STATUS_LED_PIN = 22;
const int STATUS_LED_COUNT = 2; // 2 pixels: pixel 0 for device status, pixel 1 for battery status

// Status LED instance
Adafruit_NeoPixel statusLED(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

// Status LED blinking for transmission
Ticker statusLedTicker;
volatile bool statusLedBlinkState = false;
volatile uint8_t blinkR = 0, blinkG = 0, blinkB = 0;
uint8_t restoreR = 0, restoreG = 0, restoreB = 0; // Default to off

// IMU reading ticker for 100Hz sampling
Ticker imuReadTicker;
volatile bool imuDataReady = false; // Flag set by ISR when it's time to read IMU

// Battery Indicator LED instance (shares statusLED NeoPixel, uses pixel 1)
BatteryIndicatorLED batteryIndicatorLED;

// Transmission handler for failed data retry logic
TransmissionHandler transmissionHandler;

// Cycle handler for data transmission operations
CycleHandler* cycleHandler = nullptr;

// GPS Scenario Handler
GPSScenarioHandler* gpsScenarioHandler = nullptr;

// Create sensor instances
IMUSensor imuSensor;
GPSSensor gpsSensor;
SPIFlashHandler spiFlash;
UnifiedCSVStorage unifiedCSVStorage;

// Sensor status flags
bool imuInitialized = false;
bool gpsInitialized = false;
bool flashInitialized = false;

// Motion detection variables (for deep sleep)
volatile bool motionInterruptFlag = false;
unsigned long lastMotionTime = 0;
unsigned long noMotionStartTime = 0;
bool noMotionTracking = false;

// Helper function to set GPIO 4 (power latch) with proper pull-up/pull-down configuration
void setPowerLatchPin(bool high) {
  if (high) {
    // Set HIGH: Configure as OUTPUT with pull-up
    pinMode(POWER_LATCH_PIN, OUTPUT);
    gpio_set_pull_mode(GPIO_NUM_4, GPIO_PULLUP_ONLY);
    digitalWrite(POWER_LATCH_PIN, HIGH);
    Serial.println("Power latch pin (IO4) set HIGH with pull-up");
  } else {
    // Set LOW: Configure as OUTPUT with pull-down
    pinMode(POWER_LATCH_PIN, OUTPUT);
    gpio_set_pull_mode(GPIO_NUM_4, GPIO_PULLDOWN_ONLY);
    digitalWrite(POWER_LATCH_PIN, LOW);
    Serial.println("Power latch pin (IO4) set LOW with pull-down");
  }
}

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

// USB detection function for ESP32-C6
// Checks if USB is connected by reading GPIO 11
bool isUSBConnected() {
  // On ESP32-C6, GPIO 11 is used for USB detection
  // Returns true if USB is connected
  int v = digitalRead(11);
  return v?true: false;  // Returns true if USB is connected
}

// Helper function to set pixel color and display
void setPixelAndShow(uint8_t pixel, uint8_t r, uint8_t g, uint8_t b) {
  statusLED.setPixelColor(pixel, statusLED.Color(r, g, b));
  statusLED.show();
}

void updateStatusLED() {
  // Off if all sensors (IMU, GPS, and Flash) initialized, Red if any failed
  if (imuInitialized && gpsInitialized && flashInitialized) {
    restoreR = 0;
    restoreG = 0;
    restoreB = 0;
    setPixelAndShow(0, 0, 0, 0); // Off on pixel 0
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
  delay(100);
  
  // Device ID already initialized at global scope
  Serial.print("Device ID: ");
  Serial.println(DEVICE_ID);
  
  statusLED.begin();  // Initialize NeoPixel first
  setPixelAndShow(0, 255, 0, 255); // Magenta/Purple on pixel 0 (unique boot color)
  
  // Check if waking from deep sleep - handle wake-up FIRST
  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  bool wokeFromDeepSleep = (wakeReason == ESP_SLEEP_WAKEUP_EXT1 || wakeReason == ESP_SLEEP_WAKEUP_EXT0);
  if (wokeFromDeepSleep) {
    // Waking from deep sleep due to motion interrupt
    // Set power latch HIGH immediately to keep device powered on
    setPowerLatchPin(true);
    Serial.println("Waking from deep sleep - power latch set HIGH");
    
    // Initialize IMU first (needed for wake-up handler to clear interrupt status)
    Serial.println("Waking from deep sleep - initializing IMU for wake-up handling...");
    imuInitialized = imuSensor.begin();
    if (imuInitialized) {
      MotionSleepManager::handleWakeup(&imuSensor);
    } else {
      Serial.println("WARNING: IMU initialization failed on wake-up - calling handleWakeup with nullptr");
      MotionSleepManager::handleWakeup(nullptr); // Still release GPIO hold
    }
  } else {
    // Normal boot - wait for button press and set power latch
    // Initialize Button Handler early (before button check)
    ButtonHandler::begin();
    
    delay(5000); //wait 5 seconds for button to be pressed
    if (ButtonHandler::isPressed()) {
      Serial.println("Button pressed - restarting ESP...");
      setPowerLatchPin(true); // Set HIGH with pull-up to keep device power on
    }
  }
  // pinMode(POWER_LATCH_PIN,INPUT);
  
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
  // // Reset read/write pointers after erasing flash chip
  // if (x && unifiedCSVStorage.isInitialized()) {
  //   unifiedCSVStorage.clear();
  //   Serial.println("CSV storage pointers reset after chip erase");
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
  bool isPowerOn = (resetReason == ESP_RST_POWERON);
  
  // Initialize Battery Monitor ADC
  BatteryMonitor::initializeADC();
  delay(100);
  
  // Initialize Status LED first (both pixels on same pin)
  // statusLED.begin();  // Initialize GPIO first!
  statusLED.setBrightness(70);
  statusLED.show();
  updateStatusLED(); // Show red initially on pixel 0 (not initialized)
  
  // Initialize Battery Indicator LED (RGB NeoPixel, uses pixel 1)
  batteryIndicatorLED.begin(&statusLED, 1); // Pass NeoPixel pointer and pixel index 1
  batteryIndicatorLED.updateBatteryLED(); // Initialize state
  delay(100);
  
  // Initialize IMU sensor (skip if already initialized from wake-up handling)
  if (!imuInitialized) {
    Serial.println("Initializing IMU sensor...");
    imuInitialized = imuSensor.begin();
  } else {
    Serial.println("IMU already initialized (from deep sleep wake-up)");
  }
  
  if (imuInitialized) {
    Serial.println("IMU initialized successfully");
    
    // Configure BMI323 interrupts for motion detection
    Serial.println("Configuring BMI323 motion detection interrupts...");
    if (MotionSleepManager::configureBMI323Interrupts(&imuSensor)) {
      Serial.println("BMI323 interrupts configured successfully");
      MotionSleepManager::setupMotionISR();
    } else {
      Serial.println("WARNING: BMI323 interrupt configuration failed - deep sleep motion detection disabled");
    }
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
  
  // Initialize GPS Scenario Handler
  Serial.println("Initializing GPS Scenario Handler...");
  gpsScenarioHandler = new GPSScenarioHandler(&gpsSensor);
  gpsScenarioHandler->begin();
  
  // Reset scenario state on power_on (not deep sleep)
  if (isPowerOn) {
    gpsScenarioHandler->resetOnPowerOn();
  }
  
  // Start GPS fix acquisition period (2 minutes) on power-on
  if (isPowerOn && gpsInitialized) {
    gpsScenarioHandler->startGPSFixAcquisition();
    Serial.println("Starting 2-minute GPS fix acquisition period...");
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
    // Disconnect WiFi after time sync to save power
    CustomWiFi::disconnectWiFi();
    Serial.println("WiFi disconnected after time sync");
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
  bleStartTime = TimeSync::getCurrentTimeMillis();
  BLEConfig::setBLEStartTime(bleStartTime); // Set BLE start time for countdown timer
  BLEConfig::begin();
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
  
  // Set GPS scenario handler in cycle handler
  if (gpsScenarioHandler != nullptr) {
    cycleHandler->setGPSScenarioHandler(gpsScenarioHandler);
  }
  
  // Set restore color based on sensor status
  if (imuInitialized && gpsInitialized && flashInitialized) {
    cycleHandler->setStatusLEDRestoreColor(0, 0, 0); // Off
  } else {
    cycleHandler->setStatusLEDRestoreColor(255, 0, 0); // Red
  }
  
  // IMU data is stored in external flash (5MB) - no RAM buffer needed
  // DO NOT start IMU reading ticker yet - will start after configuration time is complete
  // IMU reading will be started in loop() after first cycle/configuration period completes
  Serial.println("IMU ticker will start after configuration time completes");
  
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
  
  // ========== BUTTON HANDLER UPDATE ==========
  // Handle button press events (long press for power off, double press for restart)
  ButtonHandler::update();
  
  // ========== BATTERY LED UPDATE ==========
  // Update battery LED blinking state (non-blocking)
  batteryIndicatorLED.update();
  
  // Check battery threshold crossing periodically (every 5 seconds)
  static unsigned long lastBatteryCheck = 0;
  if (currentTime - lastBatteryCheck >= 5000) {
    batteryIndicatorLED.updateBatteryLED(); // Check for threshold crossing
    lastBatteryCheck = currentTime;
  }
  
  // ========== GPS SCENARIO HANDLING ==========
  // Handle GPS scenarios and fix acquisition
  if (gpsScenarioHandler != nullptr && gpsInitialized) {
    // Check if 2-minute GPS fix acquisition period is complete
    if (!firstCycleComplete && gpsScenarioHandler->isGPSFixAcquisitionComplete()) {
      // Determine scenario based on GPS fix status
      GPSScenario scenario = gpsScenarioHandler->determineScenario();
      Serial.print("GPS fix acquisition complete - Scenario determined: ");
      Serial.println(scenario);
      
      // Handle Scenario 3: No fix found - deep sleep
      if (scenario == SCENARIO_3_NO_FIX) {
        gpsScenarioHandler->handleScenario3();
        
        Serial.println("Entering deep sleep for 30 seconds...");
        esp_sleep_enable_timer_wakeup(30000000); // 30 seconds in microseconds
        esp_deep_sleep_start();
        return; // Will not reach here
      }
      
      // NEW: Handle Scenario 2 transition - turn off GPS and start continuous cycle
      if (scenario == SCENARIO_2_LOW_ACCURACY) {
        // GPS has been ON for 2 minutes (acquisition period)
        // Turn it OFF now to start the continuous cycle
        GPSSensor::powerOff();
        gpsScenarioHandler->recordGPSOffTime();  // Record when GPS was turned off
        Serial.println("GPS turned off after 2-minute acquisition period");
        Serial.println("Starting continuous GPS cycle (wait 'GPS on after' seconds, then 1-minute fix attempts)");
      }
      
      // Scenario 1: GPS stays ON (high accuracy fix found)
      // Scenario 4: GPS already OFF (UI position provided)
    }
    
    // Handle continuous GPS fix attempt cycle (Scenario 2)
    static unsigned long long lastGPSReadTime = 0;
    uint32_t gpsReadCycleTime = NVSConfig::getGPSReadCycleTime();
    
    if (gpsScenarioHandler->getCurrentScenario() == SCENARIO_2_LOW_ACCURACY) {
      // Check if it's time to start next GPS fix attempt (after "GPS on after" delay)
      gpsScenarioHandler->checkAndStartGPSFixAttempt();
      
      // Check if GPS fix attempt is in progress and complete
      if (gpsScenarioHandler->isGPSFixAttemptComplete()) {
        // Fix attempt complete - check for fix and switch scenarios
        GPSScenario newScenario = gpsScenarioHandler->determineScenario();
        if (newScenario == SCENARIO_1_HIGH_ACCURACY) {
          Serial.println("High accuracy fix found during fix attempt - switching to Scenario 1");
        } else {
          Serial.println("No high accuracy fix found - continuing Scenario 2");
        }
        gpsScenarioHandler->endGPSFixAttempt();  // This will schedule the next attempt
      }
    }
    
    // GPS read cycle time (separate from IMU cycle and transmission cycle)
    // Only read GPS at configured cycle time (Scenario 1)
    if (gpsScenarioHandler->getCurrentScenario() == SCENARIO_1_HIGH_ACCURACY && gpsReadCycleTime > 0) {
      if (lastGPSReadTime == 0) {
        lastGPSReadTime = currentTime;
      }
      
      unsigned long long gpsReadCycleMs = (unsigned long long)gpsReadCycleTime * 1000ULL;
      if (currentTime - lastGPSReadTime >= gpsReadCycleMs) {
        // Read GPS data at configured cycle time
        if (gpsInitialized) {
          gpsSensor.update();
          GPSData gpsData = gpsSensor.getGPSData();
          if (gpsData.hasValidFix) {
            // Store high accuracy GPS in reference position (internal flash/NVS)
            gpsScenarioHandler->setReferencePosition(gpsData.latitude, gpsData.longitude);
            Serial.print("GPS read at cycle time: (");
            Serial.print(gpsData.latitude, 7);
            Serial.print(", ");
            Serial.print(gpsData.longitude, 7);
            Serial.println(")");
          }
        }
        lastGPSReadTime = currentTime;
      }
    } else {
      lastGPSReadTime = 0; // Reset if not in Scenario 1
    }
  }
  
  // ========== IMU READING (INTERRUPT-DRIVEN) ==========
  // IMU data reading triggered by ticker ISR at 100Hz (10ms intervals)
  // Start IMU based on GPS scenario:
  // - Scenario 1: Start when GPS fix is found
  // - Scenario 2: Start after 2-minute GPS fix acquisition period
  // - Scenario 4: Can start immediately (UI position provided)
  static bool imuTickerStarted = false;
  if (!imuTickerStarted && imuInitialized && unifiedCSVStorage.isInitialized()) {
    bool shouldStartIMU = false;
    
    if (gpsScenarioHandler != nullptr && gpsInitialized) {
      GPSScenario currentScenario = gpsScenarioHandler->getCurrentScenario();
      
      if (currentScenario == SCENARIO_1_HIGH_ACCURACY) {
        // Scenario 1: Start when GPS has valid fix (already determined as high accuracy)
        GPSData gpsData = gpsSensor.getGPSData();
        if (gpsData.hasValidFix && gpsSensor.isHighAccuracy()) {
          shouldStartIMU = true;
          Serial.println("Scenario 1: High accuracy GPS fix found - starting IMU sampling");
        }
      } else if (currentScenario == SCENARIO_NONE) {
        // Still in acquisition period - check if high accuracy fix found early
        GPSData gpsData = gpsSensor.getGPSData();
        if (gpsData.hasValidFix && gpsSensor.isHighAccuracy()) {
          // High accuracy fix found during acquisition - start IMU (will be Scenario 1)
          shouldStartIMU = true;
          Serial.println("High accuracy GPS fix found during acquisition - starting IMU sampling (Scenario 1)");
        }
      } else if (currentScenario == SCENARIO_2_LOW_ACCURACY) {
        // Scenario 2: Start after 2-minute GPS fix acquisition period is complete
        if (gpsScenarioHandler->isGPSFixAcquisitionComplete()) {
          shouldStartIMU = true;
          Serial.println("Scenario 2: 2-minute GPS acquisition period complete - starting IMU sampling");
        }
      } else if (currentScenario == SCENARIO_4_UI_POSITION) {
        // Scenario 4: Can start immediately (UI position provided)
        shouldStartIMU = true;
        Serial.println("Scenario 4: UI position provided - starting IMU sampling");
      }
      // Scenario 3 (NO_FIX) will not start IMU - device goes to deep sleep
    } else {
      // If GPS is not initialized, start IMU after first cycle (fallback behavior)
      if (firstCycleComplete) {
        shouldStartIMU = true;
        Serial.println("GPS not available - starting IMU sampling after first cycle (fallback)");
      }
    }
    
    if (shouldStartIMU) {
      imuReadTicker.attach_ms(10, imuReadISR); // 10ms = 100Hz
      imuTickerStarted = true;
      Serial.println("IMU sampling started: 100Hz (stored as CSV to flash)");
    }
  }
  
  // Check if ISR flagged that it's time to read IMU
  if (imuDataReady && flashInitialized && imuTickerStarted) {
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
  
  // ========== BLE UPDATE ==========
  // Update BLE to handle connections and process received data
  if (BLEConfig::isEnabled()) {
    BLEConfig::update();
    
    // Check if initial position was received via BLE
    // This is handled in BLE callback, but we need to ensure GPS is off and scenario is updated
    if (NVSConfig::hasInitialPosition() && gpsScenarioHandler != nullptr && gpsInitialized) {
      // Turn off GPS immediately (already done in BLE callback, but ensure it's off)
      GPSSensor::powerOff();
      
      // Update scenario to Scenario 4
      GPSScenario scenario = gpsScenarioHandler->determineScenario();
      if (scenario == SCENARIO_4_UI_POSITION) {
        Serial.println("Initial position from UI active - Scenario 4");
      }
    }
  }
  
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
    // Wait for connection time + 1 minute (if connected), or 1 minute from start (if not connected)
    
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
      // Get extension time from BLE config (adds 30s each time button is pressed)
      unsigned long long extensionTime = BLEConfig::getConfigTimeExtension();
      unsigned long long connectionPlusThirty = bleConnectionTime + 60000 + extensionTime; // +1 min + extension
      unsigned long long minimumOneMinute = bleStartTime + 60000 + extensionTime; // 1 min from start + extension
      
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
      // No connection yet - default 1 minute wait + extension time
      unsigned long long extensionTime = BLEConfig::getConfigTimeExtension();
      firstCycleEndTime = bleStartTime + 60000 + extensionTime;
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
      
      // Determine GPS scenario if not already determined
      if (gpsScenarioHandler != nullptr && gpsInitialized) {
        if (!gpsScenarioHandler->isGPSFixAcquisitionComplete()) {
          // If 2-minute period not complete, complete it now
          gpsScenarioHandler->startGPSFixAcquisition();
          // Wait for remaining time or complete immediately
          // Only call determineScenario() once at the end to avoid repeated logs
          while (!gpsScenarioHandler->isGPSFixAcquisitionComplete()) {
            delay(100);
            gpsSensor.update();
            // Don't call determineScenario() in loop - it causes repeated logs
            // Will be called once after loop completes
          }
        }
        // Determine scenario once after acquisition period is complete
        GPSScenario scenario = gpsScenarioHandler->determineScenario();
        Serial.print("GPS Scenario after first cycle: ");
        Serial.println(scenario);
      }
      
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
  
  // ========== MOTION DETECTION & DEEP SLEEP ==========
  // Only enable deep sleep motion detection after first cycle completes
  // Don't sleep during BLE configuration window
  if (firstCycleComplete && imuInitialized && MotionSleepManager::isConfigured()) {
    // Track no-motion duration (now uses Bosch API)
    bool shouldSleep = MotionSleepManager::trackNoMotionDuration(&imuSensor);
    
    if (shouldSleep) {
      // 5 minutes of no motion - enter deep sleep
      // First, attempt to transmit any pending data before entering deep sleep
      if (flashInitialized && unifiedCSVStorage.isInitialized() && unifiedCSVStorage.hasDataToRead()) {
        Serial.println("\n========== PRE-SLEEP DATA TRANSMISSION ==========");
        Serial.println("Pending data detected - attempting transmission before deep sleep...");
        
        // Attempt to transmit pending data
        CycleResult result = executeCycleTransmission();
        
        // Log transmission result
        switch(result) {
          case CYCLE_SUCCESS_BLE:
            Serial.println("Pre-sleep transmission completed via BLE");
            break;
          case CYCLE_SUCCESS_WIFI:
            Serial.println("Pre-sleep transmission completed via WiFi");
            break;
          case CYCLE_SUCCESS_STORED:
            Serial.println("Pre-sleep data stored to flash (no connection available)");
            break;
          case CYCLE_FAILED:
            Serial.println("Pre-sleep transmission failed - data remains in flash");
            break;
        }
        
        Serial.println("Pre-sleep transmission attempt complete");
        Serial.println("========================================\n");
      } else {
        Serial.println("No pending data to transmit before deep sleep");
      }
      
      // Configure GPIO 14 HIGH with pull-up before entering deep sleep
      setPowerLatchPin(true);
      MotionSleepManager::enterDeepSleep(&imuSensor);
      // Will not reach here - device will wake from motion interrupt
    }
  }
  
  // ========== LIGHT SLEEP (POWER SAVING) ==========
  // Enter light sleep to save power when no immediate tasks
  // Timer wakeup every 10ms ensures ticker interrupt is processed
  // Only after first cycle to keep BLE responsive during initial connection
  // Skip light sleep if motion detection is active (to allow motion tracking)
  if (firstCycleComplete && !imuDataReady && (!imuInitialized || !MotionSleepManager::isConfigured() || !noMotionTracking)) {
    esp_sleep_enable_timer_wakeup(10000); // Wake every 10ms (10,000 microseconds)
    esp_light_sleep_start();
  } else if (!firstCycleComplete) {
    // During first cycle, keep CPU responsive for BLE with minimal delay
    delay(1);
  } else if (noMotionTracking) {
    // During no-motion tracking, use small delay instead of light sleep
    // This ensures motion interrupt flag is checked frequently
    delay(10);
  }
}