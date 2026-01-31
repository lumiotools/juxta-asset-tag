#include "device_id.h"
#include "nvs_config.h"
#include "spi_flash_handler.h"
#include "imu_sensor.h"
#include "gps_sensor.h"
#include "ble_config.h"
#include "customwifi.h"
#include "battery_indicator_led.h"
#include "time_sync.h"
#include "model_server_transmission.h"
#include "position_server_transmission.h"
#include "motion_sleep_manager.h"  // Motion detection and deep sleep management
#include <Adafruit_NeoPixel.h>
#include <esp_system.h>
#include "esp_sleep.h"
#include <Ticker.h>
#include "driver/gpio.h"

#define POWER_LATCH_PIN 4
#define BUTTON_PIN 10

const char* DEVICE_VERSION = "v2.0.0"; 
static char deviceIdBuffer[32];

static void initializeDeviceId() {
  DeviceID::getDeviceId(deviceIdBuffer, sizeof(deviceIdBuffer));
}

const char* DEVICE_ID = deviceIdBuffer;

const int STATUS_LED_PIN = 11;
const int STATUS_LED_COUNT = 2; 

Adafruit_NeoPixel statusLED(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

BatteryIndicatorLED batteryIndicatorLED;
IMUSensor imuSensor;
GPSSensor gpsSensor;
SPIFlashHandler spiFlash;
UnifiedCSVStorage unifiedCSVStorage;
ModelServerTransmissionHandler modelServerTransmissionHandler;
PositionServerTransmissionHandler positionServerTransmissionHandler;

// Sensor status flags
bool imu_initialized = false;
bool gps_initialized = false;
bool flash_initialized = false;
bool csv_storage_initialized = false;

uint8_t current_scenario = SCENARIO_NONE;

bool is_first_cycle = false;
bool power_button_pressed = false;
int prev_button_state = -1;
long long prev_button_click_time = -1;

long long ble_start_time = -1;
long long ble_off_after_time = 1000 * 60 * 1 + 1000 * 5; // 1 minute + 5 seconds

bool gps_search = false;
long long gps_search_start_time = -1;

long long transmission_cycle_start_time = -1;
long long gps_cycle_start_time = -1;
int transmission_count = 0;

Ticker imuReadTicker;
bool should_read_imu = false;

// ============================================================================
// MOTION SLEEP MANAGER - Global Variables
// ============================================================================
// These variables are required by MotionSleepManager class
// They track motion detection state and are accessed by the ISR and tracking functions
volatile bool motionInterruptFlag = false;    // Set by ISR when motion interrupt occurs
unsigned long lastMotionTime = 0;             // Timestamp of last detected motion
unsigned long noMotionStartTime = 0;         // When no-motion tracking started
bool noMotionTracking = false;                // Whether currently tracking no-motion period
// ============================================================================

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

void setPixelAndShow(uint8_t pixel, uint8_t r, uint8_t g, uint8_t b) {
  statusLED.setPixelColor(pixel, statusLED.Color(r, g, b));
  statusLED.show();
}

// Attempt time synchronization if needed (WiFi must be connected first)
void attemptTimeSyncIfNeeded() {
  CustomWiFi::connectWiFi();
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
  CustomWiFi::disconnectWiFi();
}

void triggerIMURead() {
  should_read_imu = true;
  // Note: Serial logging in interrupt callbacks can cause issues, so kept minimal
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("=== Device Startup ===");

  // Initialize Device ID
  initializeDeviceId();
  Serial.print("Device ID: ");
  Serial.println(DEVICE_ID);
  Serial.print("Device Version: ");
  Serial.println(DEVICE_VERSION);

  Serial.println("Initializing status LED...");
  statusLED.begin();

  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  Serial.print("Wake reason: ");
  
  if(wakeReason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("TIMER (scheduled wake)");
    setPixelAndShow(0, 0, 255, 0); // Green on
    delay(1000);
    setPixelAndShow(0, 0, 0, 0); // Off
    gpio_hold_dis(GPIO_NUM_4); // Release GPIO hold on power latch pin
    setPowerLatchPin(true);
    is_first_cycle = false;
    gps_search = true;
    gps_search_start_time = TimeSync::getCurrentTimeMillis();
    Serial.println("Set to normal cycle mode, GPS search enabled");

  } else if (wakeReason == ESP_SLEEP_WAKEUP_EXT0 || wakeReason == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.println("EXTERNAL (EXT0/EXT1)");
    setPixelAndShow(0, 0, 255, 0); // Green on
    delay(1000);
    setPixelAndShow(0, 0, 0, 0); // Off
    gpio_hold_dis(GPIO_NUM_4); // Release GPIO hold on power latch pin
    setPowerLatchPin(true);
    Serial.println("External wake detected");
    MotionSleepManager::handleWakeup(&imuSensor);

  } else {
    Serial.println("BUTTON or FIRST BOOT");
    pinMode(BUTTON_PIN, INPUT_PULLDOWN);
    pinMode(POWER_LATCH_PIN, OUTPUT);
    power_button_pressed = true;
    Serial.println("Waiting 4 seconds for button release...");
    delay(4000);
    Serial.println("Button pressed - after delay of 4 seconds");
    setPowerLatchPin(true);
    setPixelAndShow(0, 0, 255, 0); // Green on
    delay(2000);
    setPixelAndShow(0, 0, 0, 0); // Off
    is_first_cycle = true;
    Serial.println("Set to first cycle mode");
  }

  Serial.println("Initializing battery indicator LED...");
  batteryIndicatorLED.begin(&statusLED, 1); // Pass NeoPixel pointer and pixel index 1
  batteryIndicatorLED.updateBatteryLED(); // Initialize state
  delay(100);

  Serial.println("Initializing NVS...");
  NVSConfig::initializeNVS();

  Serial.println("Initializing SPI Flash...");
  flash_initialized = spiFlash.begin();
  Serial.print("SPI Flash initialized: ");
  Serial.println(flash_initialized ? "SUCCESS" : "FAILED");

  Serial.println("Initializing Unified CSV Storage...");
  csv_storage_initialized = unifiedCSVStorage.begin(&spiFlash);
  Serial.print("CSV Storage initialized: ");
  Serial.println(csv_storage_initialized ? "SUCCESS" : "FAILED");

  Serial.println("Initializing IMU sensor...");
  imu_initialized = imuSensor.begin();
  Serial.print("IMU sensor initialized: ");
  Serial.println(imu_initialized ? "SUCCESS" : "FAILED");

  Serial.println("Initializing GPS sensor...");
  gps_initialized = gpsSensor.begin();
  Serial.print("GPS sensor initialized: ");
  Serial.println(gps_initialized ? "SUCCESS" : "FAILED");
  gps_initialized = true;
  if(!flash_initialized || !csv_storage_initialized || !imu_initialized || !gps_initialized) {
    Serial.println("ERROR: Critical initialization failed - aborting setup");
    return;
  }
  Serial.println("All sensors initialized successfully");
  
  // Configure BMI323 motion detection interrupts
  // This sets up any-motion and no-motion detection on the IMU
  // Motion detection will trigger interrupts on GPIO 5 (MOTION_INT_PIN)
  Serial.println("Configuring motion detection...");
  if (imu_initialized && MotionSleepManager::configureBMI323Interrupts(&imuSensor)) {
    Serial.println("Motion detection configured successfully");
    
    // Setup interrupt service routine (ISR) on GPIO 5 (MOTION_INT_PIN)
    // The ISR will set motionInterruptFlag when motion is detected
    // GPIO 5 is RTC-capable, so it can wake device from deep sleep
    MotionSleepManager::setupMotionISR();
    
    // Initialize motion tracking - set lastMotionTime to current time
    // This prevents immediate sleep on startup (gives device time to initialize)
    lastMotionTime = TimeSync::getCurrentTimeMillis();
    Serial.println("Motion tracking initialized - device will monitor for 5 minutes of no-motion");
    Serial.println("After 5 minutes of stillness, device will enter deep sleep");
    Serial.println("Device will wake automatically when motion is detected");
  } else {
    Serial.println("WARNING: Motion detection configuration failed - deep sleep on no-motion disabled");
    Serial.println("Device will continue operating but will not enter deep sleep automatically");
    Serial.println("Check IMU initialization and GPIO 5 connection to BMI323 INT1");
  }
  // ============================================================================

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

  Serial.println("Initializing Model Server Transmission...");
  modelServerTransmissionHandler.begin(&unifiedCSVStorage);

  Serial.println("Initializing Position Server Transmission...");
  positionServerTransmissionHandler.begin();

  uint8_t savedScenario = NVSConfig::getScenarioState();
  Serial.print("Restored scenario from NVS: ");
  Serial.println(savedScenario);
  if (savedScenario <= SCENARIO_4_GPS_OFF) {
    current_scenario = (GPSScenario)savedScenario;
    Serial.print("Current scenario set to: ");
    Serial.println(current_scenario);
  } else {
    current_scenario = SCENARIO_NONE;
    Serial.println("Invalid saved scenario - reset to SCENARIO_NONE");
  }

  if(current_scenario == SCENARIO_4_GPS_OFF) {
    gpsSensor.powerOff();
  }

  attemptTimeSyncIfNeeded();
  
  Serial.println("Initializing BLE...");
  BLEConfig::setDeviceId(DEVICE_ID);
  BLEConfig::setDeviceVersion(DEVICE_VERSION);
  if(is_first_cycle) {
    ble_start_time = TimeSync::getCurrentTimeMillis();
    ble_off_after_time = 1000 * 60 * 1 + 1000 * 5; // 1 minute + 5 seconds (5 seconds buffer for accounting delay)
    Serial.print("BLE start time: ");
    Serial.println(ble_start_time);
    BLEConfig::begin();
    BLEConfig::setBleOffAfterTime(&ble_off_after_time, ble_start_time); // Set BLE off after time reference
  }
  Serial.println("BLE initialized");
  // Green blink pattern: Green -> 300ms -> Off -> 200ms -> Green -> 300ms -> Off
  setPixelAndShow(0, 0, 255, 0); // Green on
  delay(200);
  setPixelAndShow(0, 0, 0, 0); // Off
  delay(200);
  setPixelAndShow(0, 0, 255, 0); // Green on
  delay(200);
  setPixelAndShow(0, 0, 0, 0); // Off
  Serial.println("=== Setup Complete ===");
}

void loop() {
  int current_button_state = digitalRead(BUTTON_PIN);
  if(power_button_pressed) {
    if(current_button_state == LOW) {
      Serial.println("Button released after initial press");
      power_button_pressed = false;
      prev_button_state = LOW;
      prev_button_click_time = -1;
    }
  } else {
    if(current_button_state == HIGH) {
      long long current_button_click_time = TimeSync::getCurrentTimeMillis();
      if(prev_button_state == HIGH && (current_button_click_time - prev_button_click_time) > 20000) {
         Serial.println("Factory Reset Triggered (>20s)");
         
         // 1. Ensure power remains on
         setPowerLatchPin(true);
         
         // 2. Visual indication (Blue - Reset Mode)
         setPixelAndShow(0, 0, 0, 255); 

         // 3. Erase NVS (First for safety)
         Serial.println("Erasing NVS...");
         NVSConfig::eraseAll();

         // 4. Erase SPI Flash
         Serial.println("Erasing SPI Flash...");
         spiFlash.eraseChip();
         
         // 5. Restart with Red blink indication
         Serial.println("Reset complete. Restarting...");
         setPixelAndShow(0, 255, 0, 0); // Red
         delay(500);
         setPixelAndShow(0, 0, 0, 0); // Off
         setPowerLatchPin(false);

      } else if(prev_button_state == HIGH && (current_button_click_time - prev_button_click_time) > 4000) {
        // Just show pending shutdown indication (Red)
        // Actual shutdown happens on release if duration < 20s
        if ((current_button_click_time / 250) % 2 == 0) {
            setPixelAndShow(0, 255, 0, 0); // Red
         } else {
            setPixelAndShow(0, 0, 0, 0); // Off
         }
      } else if(prev_button_state == LOW && (current_button_click_time - prev_button_click_time) < 800) {
        Serial.println("Double press detected - restarting ESP...");
        delay(200); // Brief delay before restart
        setPixelAndShow(0, 255, 0, 0); // Red on
        delay(500);
        setPixelAndShow(0, 0, 0, 0); // Off
        ESP.restart();
      } else {
        if(prev_button_state == LOW) {
          Serial.println("Button press detected");
          prev_button_click_time = current_button_click_time;
        }
        prev_button_state = HIGH;
      }
    } else {
      if(prev_button_state == HIGH) {
        long long release_time = TimeSync::getCurrentTimeMillis();
        long long duration = release_time - prev_button_click_time;
        if(duration > 4000) {
          Serial.println("Long press release detected (>4s) - powering off device...");
          setPixelAndShow(0, 255, 0, 0); // Red on
          delay(500);
          setPowerLatchPin(false);
        }
      }
      prev_button_state = LOW;
    }
  }

  if(prev_button_state == HIGH) {
    return;
  }

  batteryIndicatorLED.update();

  if(!flash_initialized || !csv_storage_initialized || !imu_initialized || !gps_initialized) {
    // Serial.println("ERROR: Sensors not initialized - showing error LED");
    setPixelAndShow(0, 255, 0, 0);
    return;
  }

  if(should_read_imu) {
    imuSensor.update();
    IMUData imuData = imuSensor.getIMUData();

    TimestampedIMUReading readingBuffer[1];

    // Add reading to buffer with timestamp
    readingBuffer[0].accX = imuData.accelerometer.x;
    readingBuffer[0].accY = imuData.accelerometer.y;
    readingBuffer[0].accZ = imuData.accelerometer.z;
    readingBuffer[0].gyrX = imuData.gyroscope.x;
    readingBuffer[0].gyrY = imuData.gyroscope.y;
    readingBuffer[0].gyrZ = imuData.gyroscope.z;
    readingBuffer[0].timestamp = TimeSync::getCurrentTimeMillis();

    bool write_success = unifiedCSVStorage.writeIMUReadings(readingBuffer, 1);
    if(!write_success) {
      Serial.println("WARNING: Failed to write IMU reading to storage");
    }
    should_read_imu = false;

    // Call this function - it handles everything internally
    bool shouldSleep = MotionSleepManager::trackNoMotionDuration(&imuSensor);
    
    // Check the return value
    if (shouldSleep) {
      // Timeout reached - enter deep sleep

      GPSData gpsData;
      if(current_scenario == SCENARIO_1_HIGH_ACCURACY) {
        gpsData = gpsSensor.getGPSData();
        
        if(!gpsData.hasValidFix || !gpsData.isHighAccuracy) {
          Serial.println("GPS accuracy degraded during sleep prep - transitioning to SCENARIO_2_CALCULATED");
          current_scenario = SCENARIO_2_CALCULATED;
          NVSConfig::setScenarioState((uint8_t)SCENARIO_2_CALCULATED);
          gpsSensor.powerOff(); 
          Serial.println("GPS powered off");
        }
      }
      Serial.println("Transmission cycle time reached - preparing data transmission");
      Serial.println("Stopping IMU ticker");
      imuReadTicker.detach();
      // lastMotionTime = 0;
      // noMotionStartTime = 0;
      // noMotionTracking = false;

      Serial.println("Sending transmissions to Model Server and Position Server");
      double lastKnownLat = 0.0;
      double lastKnownLon = 0.0;
      double lastKnownHdop = -1.0;
      NVSConfig::getLastKnownPosition(lastKnownLat, lastKnownLon, lastKnownHdop);
      Serial.print("Position - Lat: ");
      Serial.print(lastKnownLat, 6);
      Serial.print(", Lon: ");
      Serial.print(lastKnownLon, 6);
      Serial.print(", HDOP: ");
      Serial.println(lastKnownHdop, 2);
      
      Serial.println("Sending to Model Server...");
      bool modelSuccess = modelServerTransmissionHandler.sendData(lastKnownLat, lastKnownLon, lastKnownHdop);
      Serial.print("Model Server transmission result: ");
      Serial.println(modelSuccess ? "SUCCESS" : "FAILED");

      if(current_scenario == SCENARIO_1_HIGH_ACCURACY) {
        Serial.println("SCENARIO_1: Saving updated GPS position");
        NVSConfig::saveLastKnownPosition(gpsData.latitude, gpsData.longitude, gpsData.hdop);
      }
      
      if(!modelSuccess) {
        Serial.println("Model Server transmission failed - aborting Position Server transmission");
      } else {
        Serial.print("Sending to Position Server - Scenario: ");
        Serial.println(current_scenario);
        NVSConfig::getLastKnownPosition(lastKnownLat, lastKnownLon, lastKnownHdop);
        bool positionSuccess = positionServerTransmissionHandler.sendData((uint8_t)current_scenario, lastKnownLat, lastKnownLon, lastKnownHdop);
        Serial.print("Position Server transmission result: ");
        Serial.println(positionSuccess ? "SUCCESS" : "FAILED");
      }
      
      delay(200); // Brief delay before restart
      setPixelAndShow(0, 255, 0, 0); // Red on
      delay(500);
      setPixelAndShow(0, 0, 0, 0); // Off

      MotionSleepManager::enterDeepSleep(&imuSensor);
    }
  }

  long long current_time = TimeSync::getCurrentTimeMillis();

  if(is_first_cycle) {
    if((current_time - ble_start_time) > ble_off_after_time) {
      Serial.println("BLE timeout reached - ending first cycle");

      Serial.println("Connecting WiFi for time sync...");
      attemptTimeSyncIfNeeded();
      Serial.println("WiFi disconnected after time sync");

      Serial.println("Initializing Model Server Transmission...");
      modelServerTransmissionHandler.begin(&unifiedCSVStorage);

      Serial.println("Initializing Position Server Transmission...");
      positionServerTransmissionHandler.begin();

      Serial.println("Stopping BLE...");
      BLEConfig::stop();
      is_first_cycle = false;
      if(!NVSConfig::getGPSActive()) {
        Serial.println("GPS is not active - setting SCENARIO_4_GPS_OFF");
        current_scenario = SCENARIO_4_GPS_OFF;
        NVSConfig::setScenarioState((uint8_t)SCENARIO_4_GPS_OFF);
        gpsSensor.powerOff();
        Serial.println("GPS powered off (SCENARIO_4)");

        if(!NVSConfig::hasLastKnownPosition()) {
          NVSConfig::saveLastKnownPosition(0.0, 0.0, -1.0);
          Serial.println("No last known position - saved default (0.0, 0.0, -1.0)");
        }

        Serial.println("Sending transmission to Position server (SCENARIO_4_GPS_OFF)");
        double lastKnownLat = 0.0;
        double lastKnownLon = 0.0;
        double lastKnownHdop = -1.0;
        NVSConfig::getLastKnownPosition(lastKnownLat, lastKnownLon, lastKnownHdop);
        Serial.print("Position - Lat: ");
        Serial.print(lastKnownLat, 6);
        Serial.print(", Lon: ");
        Serial.print(lastKnownLon, 6);
        Serial.print(", HDOP: ");
        Serial.println(lastKnownHdop, 2);
        bool positionSuccess = positionServerTransmissionHandler.sendData((uint8_t)current_scenario, lastKnownLat, lastKnownLon, lastKnownHdop);
        Serial.print("Position transmission result: ");
        Serial.println(positionSuccess ? "SUCCESS" : "FAILED");
      } else {
        Serial.println("GPS is active - starting GPS search");
        gps_search = true;
        gps_search_start_time = TimeSync::getCurrentTimeMillis();
        Serial.print("GPS search start time: ");
        Serial.println(gps_search_start_time);
      }
    }
  } else if(gps_search && NVSConfig::getGPSActive()) {
    GPSData gpsData = gpsSensor.getGPSData();
    bool scenario_changed = false;

    if(gpsData.hasValidFix && gpsData.isHighAccuracy) {
      Serial.println("SCENARIO_1_HIGH_ACCURACY detected!");
      Serial.print("GPS Position - Lat: ");
      Serial.print(gpsData.latitude, 6);
      Serial.print(", Lon: ");
      Serial.print(gpsData.longitude, 6);
      Serial.print(", HDOP: ");
      Serial.println(gpsData.hdop, 2);
      if(current_scenario != SCENARIO_1_HIGH_ACCURACY) scenario_changed = true;
      current_scenario = SCENARIO_1_HIGH_ACCURACY;
      NVSConfig::setScenarioState((uint8_t)SCENARIO_1_HIGH_ACCURACY);
      NVSConfig::saveLastKnownPosition(gpsData.latitude, gpsData.longitude, gpsData.hdop);
      gps_search = false;
      Serial.println("GPS search completed - SCENARIO_1 active");
    } else if((current_time - gps_search_start_time) > (1000 * 60 * 2)) {
      Serial.println("GPS search timeout (2 minutes) reached");
      double last_known_lat = 0.0;
      double last_known_lon = 0.0;
      double last_known_hdop = -1.0;
      bool has_last_known_position = NVSConfig::getLastKnownPosition(last_known_lat, last_known_lon, last_known_hdop);
      if(gpsData.hasValidFix && !gpsData.isHighAccuracy) {
        Serial.println("SCENARIO_2_CALCULATED detected!");
        Serial.print("GPS Position - Lat: ");
        Serial.print(gpsData.latitude, 6);
        Serial.print(", Lon: ");
        Serial.print(gpsData.longitude, 6);
        Serial.print(", HDOP: ");
        Serial.println(gpsData.hdop, 2);
        if(current_scenario != SCENARIO_2_CALCULATED) scenario_changed = true;
        current_scenario = SCENARIO_2_CALCULATED;
        NVSConfig::setScenarioState((uint8_t)SCENARIO_2_CALCULATED);
        NVSConfig::saveLastKnownPosition(gpsData.latitude, gpsData.longitude, gpsData.hdop);
        gpsSensor.powerOff();
        Serial.println("GPS powered off (SCENARIO_2)");
      } else if(!gpsData.hasValidFix) {
        if(has_last_known_position) {
          Serial.println("Using last known position - Lat: ");
          Serial.print(last_known_lat, 6);
          Serial.print(", Lon: ");
          Serial.println(last_known_lon, 6);
          Serial.print(", HDOP: ");
          Serial.println(last_known_hdop, 2);
          if(current_scenario != SCENARIO_2_CALCULATED) scenario_changed = true;
          current_scenario = SCENARIO_2_CALCULATED;
          NVSConfig::setScenarioState((uint8_t)SCENARIO_2_CALCULATED);
          NVSConfig::saveLastKnownPosition(last_known_lat, last_known_lon, last_known_hdop);
          gpsSensor.powerOff();
          Serial.println("GPS powered off (SCENARIO_2)");
        } else{
          Serial.println("No last known position available - entering SCENARIO_3_NO_FIX");
          if(current_scenario != SCENARIO_3_NO_FIX) scenario_changed = true;
          current_scenario = SCENARIO_3_NO_FIX;
          NVSConfig::setScenarioState((uint8_t)SCENARIO_3_NO_FIX);
        }
      }
      gps_search = false;
      Serial.println("GPS search ended");
    }

    if(gps_search == false) {
      if(scenario_changed) {
        Serial.println("GPS search completed - preparing transmission");
        Serial.print("Sending transmission to Position server - Scenario: ");
        Serial.println(current_scenario);
        double lastKnownLat = 0.0;
        double lastKnownLon = 0.0;
        double lastKnownHdop = -1.0;
        NVSConfig::getLastKnownPosition(lastKnownLat, lastKnownLon, lastKnownHdop);
        Serial.print("Position - Lat: ");
        Serial.print(lastKnownLat, 6);
        Serial.print(", Lon: ");
        Serial.print(lastKnownLon, 6);
        Serial.print(", HDOP: ");
        Serial.println(lastKnownHdop, 2);
        bool positionSuccess = positionServerTransmissionHandler.sendData((uint8_t)current_scenario, lastKnownLat, lastKnownLon, lastKnownHdop);
        Serial.print("Position transmission result: ");
        Serial.println(positionSuccess ? "SUCCESS" : "FAILED");
      } else {
        Serial.println("GPS search completed - Scenario unchanged, skipping transmission");
      }
    }
  } else {
    if(current_scenario == SCENARIO_3_NO_FIX) {
      Serial.println("Scenario 3 detected - entering deep sleep immediately");
      // Configure power latch (IO4) HIGH with hold to keep power on during deep sleep
      Serial.println("Configuring power latch (IO4) for deep sleep...");
      setPowerLatchPin(true);  // Set HIGH with pull-up
      gpio_set_level(GPIO_NUM_4, 1);  // Ensure HIGH state
      gpio_hold_en(GPIO_NUM_4);  // Hold IO4 HIGH during deep sleep
      Serial.println("Power latch held HIGH - power will remain on during deep sleep");
      
      Serial.println("Entering deep sleep for 30 seconds...");
      delay(200); // Brief delay before restart
      setPixelAndShow(0, 255, 0, 0); // Red on
      delay(500);
      setPixelAndShow(0, 0, 0, 0); // Off
      esp_sleep_enable_timer_wakeup(1000 * 1000 * 30); // 30 seconds in microseconds
      esp_deep_sleep_start();
      return; // Will not reach here
    }

    if(current_scenario == SCENARIO_NONE) {
      Serial.println("Scenario none detected - should never happen");
      delay(200); // Brief delay before restart
      setPixelAndShow(0, 255, 0, 0); // Red on
      delay(500);
      setPixelAndShow(0, 0, 0, 0); // Off
      setPowerLatchPin(false);
      return;
    }

    if(transmission_cycle_start_time == -1) {
      Serial.println("Starting transmission cycle...");

      Serial.println("Starting IMU ticker at 100Hz (10ms interval)");
      imuReadTicker.attach_ms(10, triggerIMURead); // 10ms = 100Hz

      transmission_cycle_start_time = TimeSync::getCurrentTimeMillis();
      Serial.print("Transmission cycle start time: ");
      Serial.println(transmission_cycle_start_time);
    }

    if(gps_cycle_start_time == -1 && current_scenario == SCENARIO_1_HIGH_ACCURACY) {
      gps_cycle_start_time = TimeSync::getCurrentTimeMillis();
      Serial.print("GPS cycle start time: ");
      Serial.println(gps_cycle_start_time);
    }

    
    long long current_time = TimeSync::getCurrentTimeMillis();

    if(current_scenario == SCENARIO_1_HIGH_ACCURACY) {
      uint32_t gpsReadCycleTime = NVSConfig::getGPSReadCycleTime();
      unsigned long long gpsReadCycleMs = (unsigned long long)gpsReadCycleTime * 1000ULL;
      
      if((current_time - gps_cycle_start_time) >= gpsReadCycleMs) {
        Serial.println("GPS read cycle time reached - checking GPS status");
        GPSData gpsData = gpsSensor.getGPSData();
        Serial.print("GPS fix: ");
        Serial.print(gpsData.hasValidFix ? "VALID" : "NO FIX");
        Serial.print(", High accuracy: ");
        Serial.println(gpsData.isHighAccuracy ? "YES" : "NO");
        
        if(!gpsData.hasValidFix || !gpsData.isHighAccuracy) {
          Serial.println("GPS accuracy degraded");
          // Force transmission cycle to run immediately in the next block
          Serial.println("Forcing transmission cycle due to degradation");
          transmission_cycle_start_time = 0;           
        } else {
           Serial.println("GPS still high accuracy - resetting GPS cycle");
           gps_cycle_start_time = current_time;
        }
      }
    }

    uint32_t transmissionCycleTime = NVSConfig::getCycleTime();
    unsigned long long transmissionCycleTimeMs = (unsigned long long)transmissionCycleTime * 1000ULL;

    if((current_time - transmission_cycle_start_time) >= transmissionCycleTimeMs) {
      GPSData gpsData;
      if(current_scenario == SCENARIO_1_HIGH_ACCURACY) {
        gpsData = gpsSensor.getGPSData();
        
        if(!gpsData.hasValidFix || !gpsData.isHighAccuracy) {
          Serial.println("GPS accuracy degraded - transitioning to SCENARIO_2_CALCULATED");
          current_scenario = SCENARIO_2_CALCULATED;
          NVSConfig::setScenarioState((uint8_t)SCENARIO_2_CALCULATED);
          gpsSensor.powerOff(); 
          Serial.println("GPS powered off");
        } else {
          gps_cycle_start_time = -1;
        }
      }
      Serial.println("Transmission cycle time reached - preparing data transmission");
      Serial.println("Stopping IMU ticker");
      imuReadTicker.detach();

      Serial.println("Sending transmissions to Model Server and Position Server");
      double lastKnownLat = 0.0;
      double lastKnownLon = 0.0;
      double lastKnownHdop = -1.0;
      NVSConfig::getLastKnownPosition(lastKnownLat, lastKnownLon, lastKnownHdop);
      Serial.print("Position - Lat: ");
      Serial.print(lastKnownLat, 6);
      Serial.print(", Lon: ");
      Serial.print(lastKnownLon, 6);
      Serial.print(", HDOP: ");
      Serial.println(lastKnownHdop, 2);
      
      Serial.println("Sending to Model Server...");
      bool modelSuccess = modelServerTransmissionHandler.sendData(lastKnownLat, lastKnownLon, lastKnownHdop);
      Serial.print("Model Server transmission result: ");
      Serial.println(modelSuccess ? "SUCCESS" : "FAILED");

      if(current_scenario == SCENARIO_1_HIGH_ACCURACY) {
        Serial.println("SCENARIO_1: Saving updated GPS position");
        NVSConfig::saveLastKnownPosition(gpsData.latitude, gpsData.longitude, gpsData.hdop);
      }
      
      if(!modelSuccess) {
        Serial.println("Model Server transmission failed - aborting Position Server transmission");
      } else {
        Serial.print("Sending to Position Server - Scenario: ");
        Serial.println(current_scenario);
        NVSConfig::getLastKnownPosition(lastKnownLat, lastKnownLon, lastKnownHdop);
        bool positionSuccess = positionServerTransmissionHandler.sendData((uint8_t)current_scenario, lastKnownLat, lastKnownLon, lastKnownHdop);
        Serial.print("Position Server transmission result: ");
        Serial.println(positionSuccess ? "SUCCESS" : "FAILED");
      }

      transmission_cycle_start_time = -1;

      if(current_scenario == SCENARIO_2_CALCULATED) {
        transmission_count += 1;
        Serial.print("Transmission count: ");
        Serial.println(transmission_count);
      }
    }

    uint32_t gpsOnAfterTime = NVSConfig::getGPSOnAfter();
    unsigned long long gpsOnAfterTimeMs = (unsigned long long)gpsOnAfterTime * 1000ULL;
    int gpsOnAfterTransmissionCount = gpsOnAfterTimeMs / transmissionCycleTimeMs;

    if(current_scenario == SCENARIO_2_CALCULATED && transmission_count >= gpsOnAfterTransmissionCount) {
      Serial.println("SCENARIO_2: GPS on-after time reached - powering on GPS for search");
      gpsSensor.powerOn();
      gps_search = true;
      gps_search_start_time = TimeSync::getCurrentTimeMillis();
      Serial.print("GPS search started at: ");
      Serial.println(gps_search_start_time);

      Serial.println("Starting IMU ticker at 100Hz (10ms interval)");
      imuReadTicker.attach_ms(10, triggerIMURead); // 10ms = 100Hz

      transmission_cycle_start_time = TimeSync::getCurrentTimeMillis();
      Serial.print("Transmission cycle start time: ");
      Serial.println(transmission_cycle_start_time);

      gps_cycle_start_time = TimeSync::getCurrentTimeMillis();
      Serial.print("GPS cycle start time: ");
      Serial.println(gps_cycle_start_time);

      transmission_count = 0;
    }
  }
}
