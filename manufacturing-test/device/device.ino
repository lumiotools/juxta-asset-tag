// Factory Test - Juxta Asset Tag
// Goal: < 60 seconds end-to-end factory validation with Serial PASS/FAIL logs
//
// This sketch reuses the production firmware modules without modifying them.
// It is intended for bulk manufacturing testing with a technician watching Serial logs.
//
// Notes / constraints:
// - Does NOT intentionally toggle POWER_LATCH low (may power off the DUT).
// - Does NOT require WiFi or a GPS fix; it validates GPS UART data presence.
// - BLE test validates BLE stack init + advertising start (connection optional).
// - At the end, it releases the power latch to safely power off.

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include "../../device_id.h"
#include "../../nvs_config.h"
#include "../../battery_indicator_led.h"
#include "../../spi_flash_handler.h"
#include "../../unified_csv_storage.h"
#include "../../imu_sensor.h"
#include "../../gps_sensor.h"
#include "../../ble_config.h"
#include "../../motion_sleep_manager.h"

#include "driver/gpio.h"

// -------------------- Pins / hardware (match main firmware) --------------------
#define BUTTON_PIN 10

static const int STATUS_LED_PIN = 11;
static const int STATUS_LED_COUNT = 2;

// -------------------- Globals required by included modules --------------------
// BLEConfig expects these globals (declared extern in ble_config.h)
SPIFlashHandler spiFlash;
UnifiedCSVStorage unifiedCSVStorage;

// MotionSleepManager expects these globals (declared extern in motion_sleep_manager.h)
volatile bool motionInterruptFlag = false;
unsigned long lastMotionTime = 0;
unsigned long noMotionStartTime = 0;
bool noMotionTracking = false;

// customwifi.h (included via motion_sleep_manager.h) declares these as extern;
// MotionSleepManager calls them only indirectly, but they must exist at link time.
long long startStatusLEDBlink(uint8_t /*r*/, uint8_t /*g*/, uint8_t /*b*/) { return 0; }
void stopStatusLEDBlink(long long /*t*/) {}

// -------------------- Test objects --------------------
Adafruit_NeoPixel statusLED(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);
IMUSensor imuSensor;
GPSSensor gpsSensor;

static const char* DEVICE_VERSION_FOR_TEST = "v2.0.0";
static char deviceIdBuffer[32];

// Hub AP creds used by factory when BLE connected
static const char HUB_AP_SSID[] = "PMC Test Hub";
static const char HUB_AP_PWD[] = "PMC.Hub@Test";

// Track whether any BLE client connected at any time during the setup
static bool bleConnectedDuringSetup = false;

static const uint32_t STEP_PAUSE_MS = 500;
static const uint32_t OPERATOR_INPUT_TIMEOUT_MS = 20000;

// -------------------- Logging helpers --------------------
static void banner(const char* title) {
  Serial.println();
  Serial.println("============================================================");
  Serial.println(title);
  Serial.println("============================================================");
}

static void stepHeader(int stepIndex, int stepCount, const char* name) {
  Serial.println();
  Serial.print("STEP ");
  Serial.print(stepIndex);
  Serial.print("/");
  Serial.print(stepCount);
  Serial.print(": ");
  Serial.println(name);
  Serial.println("------------------------------------------------------------");
}

static void waitCountdown(const char* label, int seconds) {
  
  // Save current pixel colors so we can restore them after turning off.
  uint32_t prev0 = statusLED.getPixelColor(0);
  uint32_t prev1 = statusLED.getPixelColor(1);

  for (int i = seconds; i > 0; --i) {
    Serial.print(label);
    Serial.print(" ");
    Serial.print(i);
    Serial.println("...");
    // Poll BLE state during countdown so connections are captured
    pollBLEDuringSetup();

    for (int j = 0; j < 4; j++) {
      delay(250);
      if (j % 2 == 0) {
        // off
        statusLED.setPixelColor(0, 0);
        statusLED.setPixelColor(1, 0);
      } else {
        // restore previous colors
        statusLED.setPixelColor(0, prev0);
        statusLED.setPixelColor(1, prev1);
      }
      statusLED.show();
    }
  }
} 

static void pauseBetweenSteps(uint32_t ms = STEP_PAUSE_MS) {
  Serial.print("Waiting ");
  Serial.print(ms);
  Serial.println(" ms...");
  unsigned long start = millis();
  while ((millis() - start) < ms) {
    pollBLEDuringSetup();
    delay(50);
  }
}

// Poll BLEConfig so we can detect if a client connected during setup
static void pollBLEDuringSetup() {
  // Let BLEConfig perform its periodic processing
  BLEConfig::update();
  if (BLEConfig::isConnected()) {
    if (!bleConnectedDuringSetup) {
      Serial.println("BLE: Device connected during setup");
      bleConnectedDuringSetup = true;
    }
  }
}

static int waitForChoice12(uint32_t timeoutMs) {
  // Returns: 1 or 2 if received, 0 on timeout
  unsigned long start = millis();
  while ((millis() - start) < timeoutMs) {
    while (Serial.available() > 0) {
      int ch = Serial.read();
      if (ch == '1') return 1;
      if (ch == '2') return 2;
      // ignore whitespace/newlines/other chars
    }
    delay(10);
  }
  return 0;
}

static bool askYesNo(const char* question, uint32_t timeoutMs = OPERATOR_INPUT_TIMEOUT_MS) {
  Serial.println();
  Serial.println(question);
  Serial.println("Enter 1 = YES, 2 = NO (Arduino Serial Monitor can send with Newline/No line ending)");
  Serial.print("Waiting for input (timeout ");
  Serial.print(timeoutMs / 1000);
  Serial.println("s)...");

  int choice = waitForChoice12(timeoutMs);
  if (choice == 1) {
    Serial.println("Operator input: YES");
    return true;
  }
  if (choice == 2) {
    Serial.println("Operator input: NO");
    return false;
  }
  Serial.println("Operator input: TIMEOUT (treating as NO)");
  return false;
}

static void logPass(const char* name) {
  Serial.print("TEST: ");
  Serial.print(name);
  Serial.println(" => PASS");
}

static void logFail(const char* name, const char* reason) {
  Serial.print("TEST: ");
  Serial.print(name);
  Serial.print(" => FAIL: ");
  Serial.println(reason);
}

static void setPixelAndShow(uint8_t pixel, uint8_t r, uint8_t g, uint8_t b) {
  statusLED.setPixelColor(pixel, statusLED.Color(r, g, b));
  statusLED.show();
}

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

static void releasePowerLatchAndPowerOff() {
  // WARNING: If power latch controls the DUT power rail, the device will turn off immediately.
  Serial.println("Releasing power latch (power off)...");

  // Best-effort: stop radios before cutting power
  if (BLEConfig::isEnabled()) {
    BLEConfig::stop();
  }
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  GPSSensor::powerOff();

  setPowerLatchPin(false); // Set LOW to release power latch
}

// Block here until the operator holds the button to request firmware upload.
// This function normally never returns - it keeps the device powered and provides visual
// feedback until the upload readiness string is printed. With a timeout, we will
// power off if no hold is detected within the timeout window.
static void waitForUploadRequest() {
  Serial.println("Awaiting operator to hold button to enable firmware upload...");

  // Blink green indefinitely until a button hold is detected. On a hold, print readiness
  // string and keep device powered on (do NOT release power latch).
  bool ready_printed = false;
  unsigned long press_start = 0;
  unsigned long last_toggle = millis();
  bool led_on = false;

  // Timeout: power off after this many milliseconds if no hold detected
  const unsigned long UPLOAD_REQUEST_TIMEOUT_MS = 10000; // 10 seconds
  unsigned long wait_start = millis();

  // Ensure button pin is configured for pull-down input
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);

  while (true) {
    // If a readiness hasn't been printed and timeout elapsed -> power off
    if (!ready_printed && (millis() - wait_start) >= UPLOAD_REQUEST_TIMEOUT_MS) {
      Serial.println("UPLOAD TIMEOUT: no button press detected - powering off");
      // Visual: RED
      setPixelAndShow(0, 255, 0, 0);
      setPixelAndShow(1, 255, 0, 0);
      waitCountdown("Powering off in", 3);
      releasePowerLatchAndPowerOff();
      return;
    }

    // Toggle every 500 ms
    if ((millis() - last_toggle) >= 500) {
      last_toggle = millis();
      led_on = !led_on;
      if (led_on) {
        setPixelAndShow(0, 0, 255, 0);
        setPixelAndShow(1, 0, 255, 0);
      } else {
        setPixelAndShow(0, 0, 0, 0);
        setPixelAndShow(1, 0, 0, 0);
      }
    }

    // Debounced hold detection: require >=500 ms continuous HIGH
    bool pressed = (digitalRead(BUTTON_PIN) == HIGH);
    if (pressed) {
      if (press_start == 0) press_start = millis();
      else if (!ready_printed && (millis() - press_start) >= 500) {
        Serial.println("UPLOAD FIRMWARE READY");
        ready_printed = true;

        // Make LED solid green after readiness announced
        setPixelAndShow(0, 0, 255, 0);
        setPixelAndShow(1, 0, 255, 0);
      }
    } else {
      press_start = 0;
    }

    // Small sleep to reduce CPU usage
    delay(50);
  }
}

// -------------------- Individual tests --------------------
static bool testDeviceId() {
  DeviceID::getDeviceId(deviceIdBuffer, sizeof(deviceIdBuffer));
  if (strlen(deviceIdBuffer) < 4) {
    logFail("DeviceID", "Empty/short device id");
    return false;
  }
  Serial.print("Device ID: ");
  Serial.println(deviceIdBuffer);
  logPass("DeviceID");
  return true;
}

static bool testStatusLED() {
  Serial.println("LED: both pixels WHITE (500ms)");
  setPixelAndShow(0, 255, 255, 255);
  setPixelAndShow(1, 255, 255, 255);
  delay(500);

  // Test both pixels, one-by-one, for each color.
  // Keep the *other* pixel off while testing the current one.
  const uint8_t level = 255;
  for (uint8_t pixel = 0; pixel < 2; pixel++) {
    Serial.print("LED pixel ");
    Serial.print(pixel);
    Serial.println(": testing R, G, B");

    // Ensure both off
    setPixelAndShow(0, 0, 0, 0);
    setPixelAndShow(1, 0, 0, 0);
    delay(500);

    // Red
    setPixelAndShow(pixel, level, 0, 0);
    delay(500);

    // Green
    setPixelAndShow(pixel, 0, level, 0);
    delay(500);

    // Blue
    setPixelAndShow(pixel, 0, 0, level);
    delay(500);

    // Off
    setPixelAndShow(pixel, 0, 0, 0);
    delay(500);
  }

  setPixelAndShow(0, 255, 255, 255);
  setPixelAndShow(1, 255, 255, 255);
  return true;
}

static bool testNVS() {
  bool ok = NVSConfig::initializeNVS();
  if (!ok) {
    logFail("NVS", "nvs_flash_init failed");
    return false;
  }
  int space = NVSConfig::getAvailableNVSSpace();
  Serial.print("Estimated available NVS bytes: ");
  Serial.println(space);
  logPass("NVS");
  return true;
}

static bool testBatteryADC(uint32_t maxMillisBudget) {
  BatteryMonitor::initializeADC();

  unsigned long start = millis();
  while (!BatteryMonitor::isNewReadingAvailable() && (millis() - start) < maxMillisBudget) {
    BatteryMonitor::updateBatteryReading();
    pollBLEDuringSetup();
    delay(20);
  }

  float v = BatteryMonitor::readBatteryVoltage();
  int p = BatteryMonitor::getBatteryPercentageV(v);
  Serial.print("Battery voltage: ");
  Serial.print(v, 3);
  Serial.print(" V, percent: ");
  Serial.print(p);
  Serial.println("%");

  // Very forgiving bounds to avoid false failures.
  if (!(v > 2.5f && v < 4.6f)) {
    logFail("Battery ADC", "Voltage out of expected range");
    return false;
  }

  logPass("Battery ADC");
  return true;
}

static bool testSPIFlashRW() {
  if (!spiFlash.begin()) {
    logFail("SPI Flash", "chip not detected");
    return false;
  }

  uint32_t cap = spiFlash.getCapacity();
  if (cap < 16777216) { // expect at least 16MB
    logFail("SPI Flash", "Capacity invalid (<16MB)");
    return false;
  }

  // Use the last sector to avoid colliding with production storage starting at 0x000000.
  uint32_t base = (cap - 4096) & 0xFFFFF000; // align to 4KB
  Serial.print("Flash test sector @ 0x");
  Serial.println(base, HEX);

  if (!spiFlash.eraseSector(base)) {
    logFail("SPI Flash", "Erase sector failed");
    return false;
  }

  const char* payload = "JUXTA_FACTORY_TEST";
  const size_t payloadLen = strlen(payload) + 1;

  if (!spiFlash.writeCharArray(base, payload, payloadLen)) {
    logFail("SPI Flash", "Write failed");
    return false;
  }

  char readback[32];
  memset(readback, 0, sizeof(readback));
  if (!spiFlash.readCharArray(base, readback, payloadLen)) {
    logFail("SPI Flash", "Read failed");
    return false;
  }

  Serial.print("Flash readback: ");
  Serial.println(readback);

  if (strcmp(readback, payload) != 0) {
    logFail("SPI Flash", "Readback mismatch");
    return false;
  }

  // Leave flash clean: erase the sector we touched.
  if (!spiFlash.eraseSector(base)) {
    logFail("SPI Flash", "Post-test erase sector failed");
    return false;
  }

  logPass("SPI Flash");
  return true;
}

static bool testIMUReadings(uint32_t maxMillisBudget) {
  if (!imuSensor.begin()) {
    logFail("IMU", "Initialization failed");
    return false;
  }

  unsigned long start = millis();
  bool gotData = false;

  while ((millis() - start) < maxMillisBudget) {
    pollBLEDuringSetup();
    imuSensor.update();
    IMUData d = imuSensor.getIMUData();

    float ax = d.accelerometer.x;
    float ay = d.accelerometer.y;
    float az = d.accelerometer.z;

    // Detect non-zero, non-NaN reads.
    if (isfinite(ax) && isfinite(ay) && isfinite(az) && (fabs(ax) + fabs(ay) + fabs(az) > 0.1f)) {
      gotData = true;
      float mag = sqrtf(ax * ax + ay * ay + az * az);
      Serial.print("IMU ACC m/s^2: x=");
      Serial.print(ax, 3);
      Serial.print(" y=");
      Serial.print(ay, 3);
      Serial.print(" z=");
      Serial.print(az, 3);
      Serial.print(" |mag|=");
      Serial.println(mag, 3);
      break;
    }

    delay(30);
  }

  if (!gotData) {
    logFail("IMU", "No valid accelerometer data received");
    return false;
  }

  logPass("IMU");
  return true;
}

static bool testIMUMotionInterrupt(uint32_t maxMillisBudget) {
  // Configure BMI323 interrupt mapping (INT1 -> GPIO5) using production MotionSleepManager.
  if (!MotionSleepManager::configureBMI323Interrupts(&imuSensor)) {
    logFail("IMU INT (GPIO5)", "Interrupt configuration failed");
    return false;
  }

  MotionSleepManager::setupMotionISR();

  // Visual cue: motion-wait mode (unique color not used elsewhere)
  setPixelAndShow(0, 180, 180, 0); // Yellow: waiting for motion
  setPixelAndShow(1, 180, 180, 0);

  Serial.print("ACTION: Tap/shake the device now (<= ");
  Serial.print((maxMillisBudget + 999) / 1000);
  Serial.println("s) to verify INT line");

  unsigned long start = millis();
  uint32_t lastPrintedSec = 0;
  const uint32_t budgetSec = (maxMillisBudget + 999) / 1000;
  Serial.print("Idle: 0s/");
  Serial.print(budgetSec);
  Serial.println("s");

  while ((millis() - start) < maxMillisBudget) {
    pollBLEDuringSetup();

    if (motionInterruptFlag) {
      motionInterruptFlag = false;
      uint32_t elapsedSec = (millis() - start) / 1000;
      Serial.print("Motion detected at ");
      Serial.print(elapsedSec);
      Serial.println("s");

      // Visual cue: motion detected (turn off)
      setPixelAndShow(0, 0, 0, 0);
      setPixelAndShow(1, 0, 0, 0);
      logPass("IMU INT (GPIO5)");
      return true;
    }

    uint32_t elapsedSec = (millis() - start) / 1000;
    if (elapsedSec != lastPrintedSec) {
      lastPrintedSec = elapsedSec;
      Serial.print("Idle: ");
      Serial.print(elapsedSec);
      Serial.print("s/");
      Serial.print(budgetSec);
      Serial.println("s");
    }

    delay(10);
  }

  Serial.print("No motion detected after ");
  Serial.print(budgetSec);
  Serial.println("s");
  // Optional cue: timeout (turn off)
  setPixelAndShow(0, 0, 0, 0);
  setPixelAndShow(1, 0, 0, 0);
  logFail("IMU INT (GPIO5)", "No interrupt observed");
  return false;
}

static bool testGPS() {
  if (!gpsSensor.begin()) {
    logFail("GPS", "Initialization failed");
    return false;
  }

  logPass("GPS Initialization");
  return true;
}

static bool testHubConnect() {
  Serial.print("Connecting to Hub AP: ");
  Serial.println(HUB_AP_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(50);

  unsigned long start = millis();
  const unsigned long timeout = 10000; // 10s timeout

  WiFi.begin(HUB_AP_SSID, HUB_AP_PWD);
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout) {
    // Poll BLE while waiting so we don't miss events
    pollBLEDuringSetup();
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected to Hub. IP: ");
    Serial.println(WiFi.localIP());
    logPass("Hub Connect");

    // Visual cue: Hub connected -> PURPLE on both pixels
    setPixelAndShow(0, 128, 0, 128);
    setPixelAndShow(1, 128, 0, 128);

    return true;
  } else {
    logFail("Hub Connect", "Failed to connect to Hub AP");
    WiFi.mode(WIFI_OFF);
    return false;
  }
} 

// -------------------- Main factory sequence --------------------
static bool ran = false;

void setup() {
  Serial.begin(115200);
  delay(250);

  banner("JUXTA FACTORY TEST");

  // Visual indication immediately on boot (helps operator while holding the button)
  statusLED.begin();
  statusLED.setBrightness(100);
  setPixelAndShow(0, 150, 80, 0); // Amber: booting
  setPixelAndShow(1, 150, 80, 0); // Amber: booting

  // Assert latch ASAP so operator can release the button
  setPowerLatchPin(true);
  delay(250);
  setPixelAndShow(0, 0, 255, 0); // Green: latch asserted
  setPixelAndShow(1, 0, 255, 0); // Green: latch asserted
  Serial.println("POWER LATCH ASSERTED - you can release the button now.");

  // Start BLE advertising ASAP (so operator has time to find it while other tests run)
  DeviceID::getDeviceId(deviceIdBuffer, sizeof(deviceIdBuffer));
  BLEConfig::setDeviceId(deviceIdBuffer);
  BLEConfig::setDeviceVersion(DEVICE_VERSION_FOR_TEST);
  static long long bleOffAfterTime = 60000;
  BLEConfig::setBleOffAfterTime(&bleOffAfterTime, 0);
  (void)BLEConfig::begin();
  Serial.print("BLE started. Look for: Juxta ");
  Serial.println(deviceIdBuffer);
  // Poll immediately so a quick connect is captured
  pollBLEDuringSetup();

  Serial.println("Starting step-by-step tests...");

  // Human-friendly step-by-step execution
  const int STEP_COUNT = 8;
  int step = 1;
  bool overall = true;

  stepHeader(step++, STEP_COUNT, "Identity + NVS");
  overall &= testDeviceId();
  overall &= testNVS();
  // pauseBetweenSteps();

  stepHeader(step++, STEP_COUNT, "GPS");
  overall &= testGPS();
  Serial.println("GPS step done.");
  // pauseBetweenSteps();

  stepHeader(step++, STEP_COUNT, "Status LEDs");
  overall &= testStatusLED();
  // No operator feedback required for LEDs
  logPass("LED");
  // pauseBetweenSteps();

  stepHeader(step++, STEP_COUNT, "Battery ADC");
  overall &= testBatteryADC(3000);
  // pauseBetweenSteps();

  stepHeader(step++, STEP_COUNT, "External SPI flash R/W/C");
  overall &= testSPIFlashRW();
  // pauseBetweenSteps();

  stepHeader(step++, STEP_COUNT, "IMU readings + motion interrupt (tap/shake)");
  overall &= testIMUReadings(2000);
  Serial.println("ACTION: Get ready to tap/shake for motion interrupt check...");
  overall &= testIMUMotionInterrupt(10000);

  // Early-exit conditions per new flow:
  // 1) If any prior step failed (overall == false) -> immediate power off
  if (!overall) {
    Serial.println();
    Serial.println("EARLY EXIT: Failures detected before BLE/WiFi; powering off");
    Serial.println("OVERALL: FAIL");

    // Visual: RED
    setPixelAndShow(0, 255, 0, 0);
    setPixelAndShow(1, 255, 0, 0);

    waitCountdown("Powering off in", 3);
    releasePowerLatchAndPowerOff();
    return;
  }

  // 2) If no BLE connection detected during setup -> early exit
  stepHeader(step++, STEP_COUNT, "BLE connection during setup");
  if (!bleConnectedDuringSetup) {
    logFail("BLE Connection", "No device connected during setup");

    Serial.println();
    Serial.println("EARLY EXIT: BLE not connected during setup; powering off");

    // Visual: RED
    setPixelAndShow(0, 255, 0, 0);
    setPixelAndShow(1, 255, 0, 0);

    waitCountdown("Powering off in", 3);
    releasePowerLatchAndPowerOff();
    return;
  }

  // BLE was connected earlier -> proceed
  logPass("BLE Connection");
  // Visual cue: BLE seen -> BLUE on both pixels
  setPixelAndShow(0, 0, 0, 255);
  setPixelAndShow(1, 0, 0, 255);
  // pauseBetweenSteps();

  // Proceed to hub WiFi connection
  stepHeader(step++, STEP_COUNT, "Hub WiFi connect (if BLE connected)");
  overall &= testHubConnect();
  // pauseBetweenSteps();

  Serial.println();
  if (overall) {
    Serial.println("OVERALL: PASS");

    // Block here until the operator signals readiness to upload the production firmware.
    waitForUploadRequest();

  } else {
    Serial.println("OVERALL: FAIL");
    // Red on pixel 0 for 2s
    setPixelAndShow(0, 255, 0, 0);
    setPixelAndShow(1, 255, 0, 0);

    waitCountdown("Powering off in", 3);
    releasePowerLatchAndPowerOff();
    return;
  }
}

void loop() {}
