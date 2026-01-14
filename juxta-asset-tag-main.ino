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
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // Initialize Device ID
  initializeDeviceId();
  Serial.print("Device ID: ");
  Serial.println(DEVICE_ID);

  statusLED.begin();

  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();

  if(wakeReason == ESP_SLEEP_WAKEUP_TIMER) {
    setPowerLatchPin(true);
    is_first_cycle = false;
    gps_search = true;

  } else if (wakeReason == ESP_SLEEP_WAKEUP_EXT0 || wakeReason == ESP_SLEEP_WAKEUP_EXT1) {
    setPowerLatchPin(true);

  } else {
    pinMode(BUTTON_PIN, INPUT_PULLDOWN);
    pinMode(POWER_LATCH_PIN, OUTPUT);
    power_button_pressed = true;
    delay(5000);
    Serial.println("Button pressed - after delay of 5 seconds");
    setPowerLatchPin(true);
    is_first_cycle = true;
  }

  batteryIndicatorLED.begin(&statusLED, 1); // Pass NeoPixel pointer and pixel index 1
  batteryIndicatorLED.updateBatteryLED(); // Initialize state
  delay(100);

  NVSConfig::initializeNVS();

  Serial.println("Initializing SPI Flash...");
  flash_initialized = spiFlash.begin();

  Serial.println("Initializing Unified CSV Storage...");
  csv_storage_initialized = unifiedCSVStorage.begin(&spiFlash);

  Serial.println("Initializing IMU sensor...");
  imu_initialized = imuSensor.begin();

  Serial.println("Initializing GPS sensor...");
  gps_initialized = gpsSensor.begin();

  if(!flash_initialized || !csv_storage_initialized || !imu_initialized || !gps_initialized) {
    return;
  }

  uint8_t savedScenario = NVSConfig::getScenarioState();
  if (savedScenario <= SCENARIO_4_UI_POSITION) {
    current_scenario = (GPSScenario)savedScenario;
  } else {
    current_scenario = SCENARIO_NONE;
  }

  if(is_first_cycle) {
    CustomWiFi::connectWiFi();
    attemptTimeSyncIfNeeded();
    CustomWiFi::disconnectWiFi();
    Serial.println("WiFi disconnected after time sync");
  }

  BLEConfig::setDeviceId(DEVICE_ID);
  BLEConfig::setDeviceVersion(DEVICE_VERSION);
  ble_start_time = TimeSync::getCurrentTimeMillis();
  BLEConfig::setBLEStartTime(ble_start_time); // Set BLE start time for countdown timer
  BLEConfig::begin();
}

void loop() {

  int current_button_state = digitalRead(BUTTON_PIN);
  if(power_button_pressed) {
    if(current_button_state == LOW) {
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
  }

  long long current_time = TimeSync::getCurrentTimeMillis();

  if(is_first_cycle) {
    if((current_time - ble_start_time) > ble_off_after_time) {
      // Turn OFF BLE

      is_first_cycle = false;
      if(!NVSConfig::getGPSActive()) {
        if(NVSConfig::hasInitialPosition()) {
          current_scenario = SCENARIO_4_UI_POSITION;
          NVSConfig::setScenarioState((uint8_t)SCENARIO_4_UI_POSITION);
          gpsSensor.powerOff();

          //TODO: Send transmission to pmc server
        }
      } else {
        gps_search = true;
        gps_search_start_time = TimeSync::getCurrentTimeMillis();
      }
    }
  } else if(gps_search && NVSConfig::getGPSActive()) {
    GPSData gpsData = gpsSensor.getGPSData();

    if(gpsData.hasValidFix && gpsSensor.isHighAccuracy()) {
      current_scenario = SCENARIO_1_HIGH_ACCURACY;
      NVSConfig::setScenarioState((uint8_t)SCENARIO_1_HIGH_ACCURACY);
      NVSConfig::saveLastKnownPosition(gpsData.latitude, gpsData.longitude);
      gps_search = false;
    } else if((current_time - gps_search_start_time) > (1000 * 60 * 2)) {
      if(gpsData.hasValidFix && !gpsSensor.isHighAccuracy()) {
        current_scenario = SCENARIO_2_LOW_ACCURACY;
        NVSConfig::setScenarioState((uint8_t)SCENARIO_2_LOW_ACCURACY);
        NVSConfig::saveLastKnownPosition(gpsData.latitude, gpsData.longitude);
        gpsSensor.powerOff();
      } else if(!gpsData.hasValidFix) {
        current_scenario = SCENARIO_3_NO_FIX;
        NVSConfig::setScenarioState((uint8_t)SCENARIO_3_NO_FIX);
      }
      gps_search = false;
    }

    if(gps_search == false) {

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
      double lat = 0.0;
      double lon = 0.0;
      if(!NVSConfig::getLastKnownPosition(lat, lon)) {
        GPSData gpsData = gpsSensor.getGPSData();
        NVSConfig::saveLastKnownPosition(gpsData.latitude, gpsData.longitude);
      }

      // TODO: Start IMU Ticker
      imuReadTicker.attach_ms(10, triggerIMURead); // 10ms = 100Hz

      transmission_cycle_start_time = TimeSync::getCurrentTimeMillis();
    }

    if(gps_cycle_start_time == -1) {
      gps_cycle_start_time = TimeSync::getCurrentTimeMillis();
    }

    
    long long current_time = TimeSync::getCurrentTimeMillis();

    if(current_scenario == SCENARIO_1_HIGH_ACCURACY) {
      uint32_t gpsReadCycleTime = NVSConfig::getGPSReadCycleTime();
      unsigned long long gpsReadCycleMs = (unsigned long long)gpsReadCycleTime * 1000ULL;
      if((current_time - gps_cycle_start_time) >= gpsReadCycleMs) {
        GPSData gpsData = gpsSensor.getGPSData();
        if(!gpsData.hasValidFix || !gpsSensor.isHighAccuracy()) {
          current_scenario = SCENARIO_2_LOW_ACCURACY;
          NVSConfig::setScenarioState((uint8_t)SCENARIO_2_LOW_ACCURACY);
          gpsSensor.powerOff();
          // TODO: Data Transmission to Model Server & PMC Server
          imuReadTicker.detach();
          NVSConfig::saveLastKnownPosition(gpsData.latitude, gpsData.longitude);
          transmission_cycle_start_time = -1;
          gps_cycle_start_time = -1;
          return;
        }
        gps_cycle_start_time = -1;
      }
    }

    uint32_t transmissionCycleTime = NVSConfig::getCycleTime();
    unsigned long long transmissionCycleTimeMs = (unsigned long long)transmissionCycleTime * 1000ULL;

    if((current_time - transmission_cycle_start_time) >= transmissionCycleTimeMs) {
      // TODO: Data Transmission to Model Server & PMC Server
      imuReadTicker.detach();
      transmission_cycle_start_time = -1;
    }

    uint32_t gpsOnAfterTime = NVSConfig::getGPSOnAfter();
    unsigned long long gpsOnAfterTimeMs = (unsigned long long)gpsOnAfterTime * 1000ULL;

    if(current_scenario == SCENARIO_2_LOW_ACCURACY && (current_time - gps_cycle_start_time) >= gpsOnAfterTimeMs) {
      gpsSensor.powerOn();
      gps_search = true;
      gps_cycle_start_time = -1;
      return;
    }
  }
}
