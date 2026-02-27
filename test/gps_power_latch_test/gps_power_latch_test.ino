#include "gps_sensor.h"
#include "time_sync.h"
#include <esp_system.h>
#include "driver/gpio.h"

#define POWER_LATCH_PIN 4

GPSSensor gpsSensor;
long long lastGPSToggleTime = -1;
bool gpsIsOn = false;


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


void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("=== GPS Power Latch Test Start ===");

  // Initialize power latch pin
  Serial.println("Initializing power latch pin...");
  setPowerLatchPin(true);
  Serial.println("Power latch activated");

  // Initialize GPS sensor
  Serial.println("Initializing GPS sensor...");
  bool gps_initialized = gpsSensor.begin();
  Serial.print("GPS sensor initialized: ");
  Serial.println(gps_initialized ? "SUCCESS" : "FAILED");

  if (!gps_initialized) {
    Serial.println("ERROR: GPS initialization failed");
    return;
  }

  // Initialize time sync for timestamp tracking
  Serial.println("Initializing time sync...");
  
  lastGPSToggleTime = TimeSync::getCurrentTimeMillis();
  gpsIsOn = true;
  gpsSensor.powerOn();
  Serial.println("GPS powered ON");
  
  Serial.println("=== Setup Complete ===");
}


void loop() {
  long long currentTime = TimeSync::getCurrentTimeMillis();

  // Toggle GPS every 30 seconds
  if ((currentTime - lastGPSToggleTime) >= 15000) {
    lastGPSToggleTime = currentTime;

    if (gpsIsOn) {
      // Turn GPS OFF
      gpsSensor.powerOff();
      gpsIsOn = false;
      Serial.println("GPS powered OFF");
    } else {
      // Turn GPS ON
      gpsSensor.powerOn();
      gpsIsOn = true;
      Serial.println("GPS powered ON");
    }

    Serial.print("GPS Toggle - Current state: ");
    Serial.println(gpsIsOn ? "ON" : "OFF");
  }

  delay(100); // Small delay to prevent blocking
}

