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
#include <Adafruit_NeoPixel.h>
#include <esp_system.h>
#include "esp_sleep.h"
#include <Ticker.h>

// Device ID and Version Configuration (hardcoded to save memory)
const char* DEVICE_ID = "ASSET_TAG_001";  // Change this for each device
const char* DEVICE_VERSION = "v2.0.0";   // Device firmware/hardware version

const int STATUS_LED_PIN = 1;
const int STATUS_LED_COUNT = 2; // 2 pixels: pixel 0 for device status, pixel 1 for battery status

// Status LED instance
Adafruit_NeoPixel statusLED(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

// Status LED blinking for transmission
Ticker statusLedTicker;
volatile bool statusLedBlinkState = false;
volatile uint8_t blinkR = 0, blinkG = 0, blinkB = 0;
uint8_t restoreR = 0, restoreG = 255, restoreB = 0; // Default to green

// Battery Indicator LED instance (shares statusLED NeoPixel, uses pixel 1)
BatteryIndicatorLED batteryIndicatorLED;

// Transmission handler for failed data retry logic
TransmissionHandler transmissionHandler;

// BLE start time tracking
unsigned long bleStartTime = 0;

// Create sensor instances
IMUSensor imuSensor;
GPSSensor gpsSensor;
SPIFlashHandler spiFlash;

// Sensor status flags
bool imuInitialized = false;
bool gpsInitialized = false;
bool flashInitialized = false;

// Configuration: Reading interval in milliseconds
const unsigned long READING_INTERVAL = 30000; // 30000ms = 30 seconds

// USB detection function for ESP32-S3
// Checks if USB is connected by verifying USB Serial availability
bool isUSBConnected() {
  // On ESP32-S3, USB Serial JTAG controller is active when USB is connected
  // Serial object is available when USB is connected and Serial.begin() has been called
  // This is a reliable method for ESP32-S3 to detect USB connection
  return Serial;  // Returns true if USB Serial is available (USB connected)
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
void startStatusLEDBlink(uint8_t r, uint8_t g, uint8_t b) {
  blinkR = r;
  blinkG = g;
  blinkB = b;
  statusLedBlinkState = false;
  statusLedTicker.attach_ms(20, toggleStatusLED); // 20ms = 50Hz blink
}

// Stop blinking and restore to normal status color
void stopStatusLEDBlink() {
  statusLedTicker.detach();
  setPixelAndShow(0, restoreR, restoreG, restoreB);
}

// Dim status LED to 10% brightness for deep sleep
void dimStatusLEDTo10Percent() {
  statusLED.setBrightness(10); // 10% of original 100
  setPixelAndShow(0, restoreR, restoreG, restoreB);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  statusLED.begin();  // Initialize NeoPixel first
  setPixelAndShow(0, 255, 165, 0); // Orange on pixel 0
  delay(1000);
  
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

  // Initialize transmission handler and data queue (loads from flash, calculates max size on first boot)
  Serial.println("Initializing data queue...");
  if (!transmissionHandler.begin(&spiFlash)) {
    Serial.println("Warning: Data queue initialization failed!");
  }
  
  // Check ESP reset reason
  esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.print("Reset reason: ");
  Serial.println(resetReason);
  
  // Initialize BLE only if reset reason is POWER_ON or reset button press
  if (resetReason == ESP_RST_POWERON || resetReason == ESP_RST_EXT) {
    Serial.println("Starting BLE (POWER_ON or reset button press)");
    BLEConfig::begin();
    BLEConfig::setDeviceId(DEVICE_ID);
    BLEConfig::setDeviceVersion(DEVICE_VERSION); // Set BLE start time when BLE begins
  } else {
    Serial.println("BLE not started (reset reason not POWER_ON or reset button)");
  }
  
  // Initialize Battery Monitor ADC
  BatteryMonitor::initializeADC();
  delay(100);
  
  // Initialize Status LED first (both pixels on same pin)
  statusLED.begin();  // Initialize GPIO first!
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
  // Connect to WiFi
  CustomWiFi::connectWiFi();
  
  // Sync time with NTP server
  if(CustomWiFi::isConnected()) {
    TimeSync::syncTimeNTP();
  } else {
    Serial.println("WiFi not connected, skipping time sync");
  }

  // Update status LED based on sensor initialization
  updateStatusLED();
  if(imuInitialized && gpsInitialized && flashInitialized) {
    bleStartTime = millis();
  } else {
    Serial.println("BLE not started (sensors not initialized)");
  }
  
  delay(100);
}

String createSensorCSV(IMUData imuData, GPSData gpsData) {
  static char csvBuffer[350];
  char timestamp[20];
  
  TimeSync::getCurrentTimeString(timestamp, sizeof(timestamp));
  int batteryLevel = BatteryMonitor::getBatteryPercentage();
  
  // CSV format: device_id,battery_level,timestamp,imu.accelerometer.x,imu.accelerometer.y,imu.accelerometer.z,
  //             imu.gyroscope.x,imu.gyroscope.y,imu.gyroscope.z,imu.temperature,
  //             gps.fix,gps.fixType,gps.satellites,gps.latitude,gps.longitude,gps.altitude,gps.speed,gps.heading,gps.hdop
  
  snprintf(csvBuffer, sizeof(csvBuffer), 
           "%s,%d,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%s,%d,%d,%.7f,%.7f,%.2f,%.2f,%.2f,%.2f",
           DEVICE_ID,
           batteryLevel,
           timestamp,
           imuData.accelerometer.x, imuData.accelerometer.y, imuData.accelerometer.z,
           imuData.gyroscope.x, imuData.gyroscope.y, imuData.gyroscope.z,
           imuData.temperature,
           
           gpsData.hasValidFix ? "true" : "false",
           gpsData.fixType,
           gpsData.satellites,
           gpsData.hasValidFix ? gpsData.latitude : 0.0,
           gpsData.hasValidFix ? gpsData.longitude : 0.0,
           gpsData.hasValidFix ? gpsData.altitude : 0.0,
           gpsData.hasValidFix ? gpsData.speed : 0.0,
           gpsData.hasValidFix ? gpsData.heading : 0.0,
           gpsData.hasValidFix ? gpsData.hdop : 0.0
  );
  
  return String(csvBuffer);
}

bool sendDataWithRetryLogic(String csvData) {
  // Use transmission handler to send via WiFi and/or BLE with queue logic
  return transmissionHandler.handleDataTransmission(csvData);
}

void loop() {
  static unsigned long lastPrintTime = 0;
  static unsigned long lastBatteryUpdate = 0;
  // sensor power management removed; IMU/GPS remain initialized throughout
  static bool firstRun = true;
  unsigned long currentTime = millis();
  
  static bool lastUSBState = isUSBConnected();
  static bool bleJustStopped = false;
  
  if (BLEConfig::isEnabled()) {
    // Update BLE (handles connections and processes WiFi credentials)
    BLEConfig::update();
    // Check if BLE is disconnected and more than 1 minute has passed since start
    if (!BLEConfig::isConnected() && bleStartTime > 0 && (currentTime - bleStartTime >= 60000)) {
      Serial.println("BLE disconnected for more than 1 minute, stopping BLE permanently");
      BLEConfig::stop();
      bleStartTime = 0;
      bleJustStopped = true;
    }
  }
  
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
  
  // Update battery indicator LED every 5 seconds (only if enabled)
  if (currentTime - lastBatteryUpdate >= 5000) {
    if (batteryIndicatorLED.isEnabled()) {
      batteryIndicatorLED.updateBatteryLED();
      
      // Critical battery warning
      if (BatteryMonitor::getBatteryPercentage() < 10) {
        batteryIndicatorLED.doubleBlink();
      }
    }
    lastBatteryUpdate = currentTime;
  }
  
  // Check if interval has passed or first run
  if (firstRun || (currentTime - lastPrintTime >= READING_INTERVAL)) {
    // Sensors remain initialized throughout runtime; no power cycling is performed.
    
    // Update sensors to get fresh data (increased to allow all IMU sensor types to report)
    // Only update if sensors are initialized to avoid crashes
    for (int i = 0; i < 50; i++) {
      if (imuInitialized) {
        imuSensor.update();
      }
      if (gpsInitialized) {
        gpsSensor.update();
      }
      delay(20);
    }
    
    // Get data from sensors
    IMUData imuData = imuSensor.getIMUData();
    GPSData gpsData = gpsSensor.getGPSData();

    // No power-off calls — sensors remain active and will be updated periodically
    
    // Create CSV and send with retry logic (LED blinks automatically during transmission)
    String csvData = createSensorCSV(imuData, gpsData);
    
    if (!BLEConfig::isConnected()) {
      // Reconnect WiFi for retry logic and transmission
      CustomWiFi::connectWiFi();
      delay(1000); // Wait for WiFi to stabilize
    }
    
    bool sendSuccess = sendDataWithRetryLogic(csvData);
    
    // Ensure status LED is restored to green after transmission
    updateStatusLED();

    // After BLE is powered off, check WiFi connection and transmission, then deep sleep
    if (bleJustStopped) {
      bleJustStopped = false;
      
      // Check WiFi connection status
      bool wifiConnected = CustomWiFi::isConnected();
      
      // Force save queue pointers before deep sleep to prevent data loss
      Serial.println("Preparing for deep sleep - saving queue state...");
      transmissionHandler.saveQueueState();
      
      // Disconnect WiFi before deep sleep
      if (wifiConnected) {
        CustomWiFi::disconnectWiFi();
      }
      
      // Dim LEDs to 10% brightness before deep sleep
      Serial.println("Dimming LEDs to 10% for deep sleep...");
      dimStatusLEDTo10Percent(); // Dims entire strip (both pixels share brightness)
      batteryIndicatorLED.updateBatteryLED(); // Update battery pixel color with new brightness
      delay(50);  // Brief delay to ensure LED update completes
      
      // Deep sleep based on WiFi connection and transmission success
      if (wifiConnected && sendSuccess) {
        Serial.println("WiFi connected and send successful - Deep sleeping for 30 seconds");
        esp_sleep_enable_timer_wakeup(30 * 1000000ULL); // 30 seconds in microseconds
      } else {
        Serial.println("WiFi not connected or send failed - Deep sleeping for 1 minute");
        esp_sleep_enable_timer_wakeup(1 * 60 * 1000000ULL); // 1 minute in microseconds
      }
      
      // Small delay to allow serial output to complete
      delay(100);
      
      // Enter deep sleep
      esp_deep_sleep_start();
    }
    
    if (CustomWiFi::isConnected()) {
      // Power off WiFi
      CustomWiFi::disconnectWiFi();
    }
    
    lastPrintTime = currentTime;
    firstRun = false;
  }
  
  delay(100);
}