#include "device_id.h"
#include "nvs_config.h"
#include "spi_flash_handler.h"
#include "imu_sensor.h"
#include "gps_sensor.h"
#include "ble_config.h"
#include "customwifi.h"
#include "battery_indicator_led.h"
#include "time_sync.h"
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

const int STATUS_LED_PIN = 22;
const int STATUS_LED_COUNT = 2; 

Adafruit_NeoPixel statusLED(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

enum GPSScenario {
  SCENARIO_NONE,           // Initial state, no scenario determined yet
  SCENARIO_1_HIGH_ACCURACY, // High accuracy GPS fix found
  SCENARIO_2_LOW_ACCURACY,  // Low accuracy GPS fix found
  SCENARIO_3_NO_FIX,        // No GPS fix found
  SCENARIO_4_UI_POSITION    // Initial position provided from UI
};

BatteryIndicatorLED batteryIndicatorLED;
IMUSensor imuSensor;
GPSSensor gpsSensor;
SPIFlashHandler spiFlash;
UnifiedCSVStorage unifiedCSVStorage;

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
long long ble_off_after_time = -1;

bool gps_search = true;
long long gps_search_start_time = -1;

long long transmission_cycle_start_time = -1;
long long gps_cycle_start_time = -1;

Ticker imuReadTicker;
bool should_read_imu = false;

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
    setPowerLatchPin(true);
    is_first_cycle = false;
    gps_search = true;
    Serial.println("Set to normal cycle mode, GPS search enabled");

  } else if (wakeReason == ESP_SLEEP_WAKEUP_EXT0 || wakeReason == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.println("EXTERNAL (EXT0/EXT1)");
    setPowerLatchPin(true);
    Serial.println("External wake detected");

  } else {
    Serial.println("BUTTON or FIRST BOOT");
    pinMode(BUTTON_PIN, INPUT_PULLDOWN);
    pinMode(POWER_LATCH_PIN, OUTPUT);
    power_button_pressed = true;
    Serial.println("Waiting 5 seconds for button release...");
    delay(5000);
    Serial.println("Button pressed - after delay of 5 seconds");
    setPowerLatchPin(true);
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

  if(!flash_initialized || !csv_storage_initialized || !imu_initialized || !gps_initialized) {
    Serial.println("ERROR: Critical initialization failed - aborting setup");
    return;
  }
  Serial.println("All sensors initialized successfully");

  uint8_t savedScenario = NVSConfig::getScenarioState();
  Serial.print("Restored scenario from NVS: ");
  Serial.println(savedScenario);
  if (savedScenario <= SCENARIO_4_UI_POSITION) {
    current_scenario = (GPSScenario)savedScenario;
    Serial.print("Current scenario set to: ");
    Serial.println(current_scenario);
  } else {
    current_scenario = SCENARIO_NONE;
    Serial.println("Invalid saved scenario - reset to SCENARIO_NONE");
  }

  if(is_first_cycle) {
    Serial.println("First cycle: Connecting WiFi for time sync...");
    CustomWiFi::connectWiFi();
    attemptTimeSyncIfNeeded();
    CustomWiFi::disconnectWiFi();
    Serial.println("WiFi disconnected after time sync");
  } else {
    Serial.println("Normal cycle: Skipping WiFi time sync");
  }

  Serial.println("Initializing BLE...");
  BLEConfig::setDeviceId(DEVICE_ID);
  BLEConfig::setDeviceVersion(DEVICE_VERSION);
  ble_start_time = TimeSync::getCurrentTimeMillis();
  Serial.print("BLE start time: ");
  Serial.println(ble_start_time);
  BLEConfig::setBLEStartTime(ble_start_time); // Set BLE start time for countdown timer
  BLEConfig::begin();
  Serial.println("BLE initialized");
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
      if(prev_button_state == HIGH && (current_button_click_time - prev_button_click_time) > 5000) {
        Serial.println("Long press detected (5s) - powering off device...");
        setPowerLatchPin(false);
      } else if(prev_button_state == LOW && (current_button_click_time - prev_button_click_time) < 800) {
        Serial.println("Double press detected - restarting ESP...");
        delay(100); // Brief delay before restart
        ESP.restart();
      } else {
        if(prev_button_state == LOW) {
          Serial.println("Button press detected");
        }
        prev_button_click_time = current_button_click_time;
      }
    } else {
      prev_button_state = LOW;
    }
  }

  if(prev_button_state == HIGH) {
    return;
  }

  batteryIndicatorLED.update();

  if(!flash_initialized || !csv_storage_initialized || !imu_initialized || !gps_initialized) {
    Serial.println("ERROR: Sensors not initialized - showing error LED");
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
  }

  long long current_time = TimeSync::getCurrentTimeMillis();

  if(is_first_cycle) {
    long long ble_elapsed = current_time - ble_start_time;
    Serial.print("First cycle - BLE elapsed time: ");
    Serial.print(ble_elapsed);
    Serial.print("ms, BLE off after: ");
    Serial.print(ble_off_after_time);
    Serial.println("ms");
    
    if((current_time - ble_start_time) > ble_off_after_time) {
      Serial.println("BLE timeout reached - ending first cycle");
      is_first_cycle = false;
      if(!NVSConfig::getGPSActive()) {
        Serial.println("GPS is not active");
        if(NVSConfig::hasInitialPosition()) {
          Serial.println("Initial position found - setting SCENARIO_4_UI_POSITION");
          current_scenario = SCENARIO_4_UI_POSITION;
          NVSConfig::setScenarioState((uint8_t)SCENARIO_4_UI_POSITION);
          gpsSensor.powerOff();
          Serial.println("GPS powered off (SCENARIO_4)");

          //TODO: Send transmission to pmc server
        } else {
          Serial.println("No initial position available");
        }
      } else {
        Serial.println("GPS is active - starting GPS search");
        gps_search = true;
        gps_search_start_time = TimeSync::getCurrentTimeMillis();
        Serial.print("GPS search start time: ");
        Serial.println(gps_search_start_time);
      }
    }
  } else if(gps_search && NVSConfig::getGPSActive()) {
    Serial.println("GPS search active - checking GPS status...");
    GPSData gpsData = gpsSensor.getGPSData();
    long long search_elapsed = current_time - gps_search_start_time;
    Serial.print("GPS search elapsed time: ");
    Serial.print(search_elapsed);
    Serial.println("ms");
    Serial.print("GPS fix status: ");
    Serial.print(gpsData.hasValidFix ? "VALID" : "NO FIX");
    Serial.print(", High accuracy: ");
    Serial.println(gpsSensor.isHighAccuracy() ? "YES" : "NO");

    if(gpsData.hasValidFix && gpsSensor.isHighAccuracy()) {
      Serial.println("SCENARIO_1_HIGH_ACCURACY detected!");
      Serial.print("GPS Position - Lat: ");
      Serial.print(gpsData.latitude, 6);
      Serial.print(", Lon: ");
      Serial.println(gpsData.longitude, 6);
      current_scenario = SCENARIO_1_HIGH_ACCURACY;
      NVSConfig::setScenarioState((uint8_t)SCENARIO_1_HIGH_ACCURACY);
      NVSConfig::saveLastKnownPosition(gpsData.latitude, gpsData.longitude);
      gps_search = false;
      Serial.println("GPS search completed - SCENARIO_1 active");
    } else if((current_time - gps_search_start_time) > (1000 * 60 * 2)) {
      Serial.println("GPS search timeout (2 minutes) reached");
      if(gpsData.hasValidFix && !gpsSensor.isHighAccuracy()) {
        Serial.println("SCENARIO_2_LOW_ACCURACY detected!");
        Serial.print("GPS Position - Lat: ");
        Serial.print(gpsData.latitude, 6);
        Serial.print(", Lon: ");
        Serial.println(gpsData.longitude, 6);
        current_scenario = SCENARIO_2_LOW_ACCURACY;
        NVSConfig::setScenarioState((uint8_t)SCENARIO_2_LOW_ACCURACY);
        NVSConfig::saveLastKnownPosition(gpsData.latitude, gpsData.longitude);
        gpsSensor.powerOff();
        Serial.println("GPS powered off (SCENARIO_2)");
      } else if(!gpsData.hasValidFix) {
        Serial.println("SCENARIO_3_NO_FIX detected - no GPS fix found");
        current_scenario = SCENARIO_3_NO_FIX;
        NVSConfig::setScenarioState((uint8_t)SCENARIO_3_NO_FIX);
      }
      gps_search = false;
      Serial.println("GPS search ended");
    }

    if(gps_search == false) {
      Serial.println("GPS search completed - preparing transmission");
      //TODO: Send transmission to pmc server
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
      esp_sleep_enable_timer_wakeup(1000 * 1000 * 30); // 30 seconds in microseconds
      esp_deep_sleep_start();
      return; // Will not reach here
    }

    if(transmission_cycle_start_time == -1) {
      Serial.println("Starting transmission cycle...");
      double lat = 0.0;
      double lon = 0.0;
      if(!NVSConfig::getLastKnownPosition(lat, lon)) {
        Serial.println("No last known position - getting from GPS");
        GPSData gpsData = gpsSensor.getGPSData();
        NVSConfig::saveLastKnownPosition(gpsData.latitude, gpsData.longitude);
        Serial.print("Saved position - Lat: ");
        Serial.print(gpsData.latitude, 6);
        Serial.print(", Lon: ");
        Serial.println(gpsData.longitude, 6);
      } else {
        Serial.print("Using last known position - Lat: ");
        Serial.print(lat, 6);
        Serial.print(", Lon: ");
        Serial.println(lon, 6);
      }

      Serial.println("Starting IMU ticker at 100Hz (10ms interval)");
      imuReadTicker.attach_ms(10, triggerIMURead); // 10ms = 100Hz

      transmission_cycle_start_time = TimeSync::getCurrentTimeMillis();
      Serial.print("Transmission cycle start time: ");
      Serial.println(transmission_cycle_start_time);
    }

    if(gps_cycle_start_time == -1) {
      gps_cycle_start_time = TimeSync::getCurrentTimeMillis();
      Serial.print("GPS cycle start time: ");
      Serial.println(gps_cycle_start_time);
    }

    
    long long current_time = TimeSync::getCurrentTimeMillis();

    if(current_scenario == SCENARIO_1_HIGH_ACCURACY) {
      uint32_t gpsReadCycleTime = NVSConfig::getGPSReadCycleTime();
      unsigned long long gpsReadCycleMs = (unsigned long long)gpsReadCycleTime * 1000ULL;
      long long gps_elapsed = current_time - gps_cycle_start_time;
      Serial.print("SCENARIO_1: GPS cycle elapsed: ");
      Serial.print(gps_elapsed);
      Serial.print("ms / ");
      Serial.print(gpsReadCycleMs);
      Serial.println("ms");
      
      if((current_time - gps_cycle_start_time) >= gpsReadCycleMs) {
        Serial.println("GPS read cycle time reached - checking GPS status");
        GPSData gpsData = gpsSensor.getGPSData();
        Serial.print("GPS fix: ");
        Serial.print(gpsData.hasValidFix ? "VALID" : "NO FIX");
        Serial.print(", High accuracy: ");
        Serial.println(gpsSensor.isHighAccuracy() ? "YES" : "NO");
        
        if(!gpsData.hasValidFix || !gpsSensor.isHighAccuracy()) {
          Serial.println("GPS accuracy degraded - transitioning to SCENARIO_2_LOW_ACCURACY");
          Serial.print("Last position - Lat: ");
          Serial.print(gpsData.latitude, 6);
          Serial.print(", Lon: ");
          Serial.println(gpsData.longitude, 6);
          current_scenario = SCENARIO_2_LOW_ACCURACY;
          NVSConfig::setScenarioState((uint8_t)SCENARIO_2_LOW_ACCURACY);
          gpsSensor.powerOff();
          Serial.println("GPS powered off");
          // TODO: Data Transmission to Model Server & PMC Server
          Serial.println("Stopping IMU ticker");
          imuReadTicker.detach();
          NVSConfig::saveLastKnownPosition(gpsData.latitude, gpsData.longitude);
          transmission_cycle_start_time = -1;
          gps_cycle_start_time = -1;
          Serial.println("Transmission and GPS cycles reset");
          return;
        }
        Serial.println("GPS still high accuracy - resetting GPS cycle");
        gps_cycle_start_time = -1;
      }
    }

    uint32_t transmissionCycleTime = NVSConfig::getCycleTime();
    unsigned long long transmissionCycleTimeMs = (unsigned long long)transmissionCycleTime * 1000ULL;
    long long transmission_elapsed = current_time - transmission_cycle_start_time;
    Serial.print("Transmission cycle elapsed: ");
    Serial.print(transmission_elapsed);
    Serial.print("ms / ");
    Serial.print(transmissionCycleTimeMs);
    Serial.println("ms");

    if((current_time - transmission_cycle_start_time) >= transmissionCycleTimeMs) {
      Serial.println("Transmission cycle time reached - preparing data transmission");
      // TODO: Data Transmission to Model Server & PMC Server
      Serial.println("Stopping IMU ticker");
      imuReadTicker.detach();
      transmission_cycle_start_time = -1;
      Serial.println("Transmission cycle reset");
    }

    uint32_t gpsOnAfterTime = NVSConfig::getGPSOnAfter();
    unsigned long long gpsOnAfterTimeMs = (unsigned long long)gpsOnAfterTime * 1000ULL;
    
    if(current_scenario == SCENARIO_2_LOW_ACCURACY) {
      long long gps_wait_elapsed = current_time - gps_cycle_start_time;
      Serial.print("SCENARIO_2: GPS wait elapsed: ");
      Serial.print(gps_wait_elapsed);
      Serial.print("ms / ");
      Serial.print(gpsOnAfterTimeMs);
      Serial.println("ms");
    }

    if(current_scenario == SCENARIO_2_LOW_ACCURACY && (current_time - gps_cycle_start_time) >= gpsOnAfterTimeMs) {
      Serial.println("SCENARIO_2: GPS on-after time reached - powering on GPS for search");
      gpsSensor.powerOn();
      gps_search = true;
      gps_search_start_time = TimeSync::getCurrentTimeMillis();
      Serial.print("GPS search started at: ");
      Serial.println(gps_search_start_time);
      gps_cycle_start_time = -1;
      return;
    }
  }
}
