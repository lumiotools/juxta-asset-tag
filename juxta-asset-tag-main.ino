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

bool is_first_cycle = false;
bool power_button_pressed = false;
int prev_button_state = -1;
long long prev_button_click_time = -1;

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

  if(is_first_cycle) {
    CustomWiFi::connectWiFi();
    attemptTimeSyncIfNeeded();
    CustomWiFi::disconnectWiFi();
    Serial.println("WiFi disconnected after time sync");
  }

  BLEConfig::setDeviceId(DEVICE_ID);
  BLEConfig::setDeviceVersion(DEVICE_VERSION);
  long long bleStartTime = TimeSync::getCurrentTimeMillis();
  BLEConfig::setBLEStartTime(bleStartTime); // Set BLE start time for countdown timer
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
  }

}
