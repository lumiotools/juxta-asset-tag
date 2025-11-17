#ifndef CUSTOMWIFI_H
#define CUSTOMWIFI_H

#include <WiFi.h>
#include <HTTPClient.h>
#include "nvs_config.h"
#include <Ticker.h>

// Server Configuration
const char* SERVER_URL = "http://echo-http-requests.appspot.com/push/juxtatetsing";
const int REQUEST_TIMEOUT = 5000; // 5 seconds

class CustomWiFi {
private:
  static int txLedPin;
  static Ticker* ledTicker;
  static volatile bool ledState;
  
  static void toggleLED() {
    if (txLedPin >= 0) {
      ledState = !ledState;
      digitalWrite(txLedPin, ledState);
    }
  }
  
public:
  static void setTxLEDPin(int pin) {
    txLedPin = pin;
    if (pin >= 0) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
      ledTicker = new Ticker();
    }
  }
  
  static bool connectWiFi() {
    // Read WiFi credentials from NVS
    String ssid = NVSConfig::getWiFiSSID();
    String password = NVSConfig::getWiFiPassword();
    
    if (ssid.length() == 0 || password.length() == 0) {
      return false;
    }
    
    WiFi.begin(ssid.c_str(), password.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
    }
    
    return (WiFi.status() == WL_CONNECTED);
  }
  
  static bool sendSensorData(const String& jsonData) {
    if (WiFi.status() != WL_CONNECTED) {
      return false;
    }
    
    // Start LED blinking (100ms period = 5Hz)
    if (txLedPin >= 0 && ledTicker != nullptr) {
      ledState = false;
      ledTicker->attach_ms(20, toggleLED);
    }
    
    HTTPClient http;
    http.setTimeout(REQUEST_TIMEOUT);
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");
    
    // POST is blocking - LED blinks via Ticker interrupt during transmission
    int httpResponseCode = http.POST(jsonData);
    // delay(2000); //pura 2s chalu bujban chalu rakhne
    // Stop LED blinking
    if (txLedPin >= 0 && ledTicker != nullptr) {
      ledTicker->detach();
      digitalWrite(txLedPin, LOW);
    }
    
    if (httpResponseCode > 0) {
      http.end();
      return (httpResponseCode == 200);
    } else {
      http.end();
      return false;
    }
  }
  
  static void disconnectWiFi() {
    WiFi.disconnect(true); // true = turn off WiFi radio
  }
};

// Static member definitions
int CustomWiFi::txLedPin = -1;
Ticker* CustomWiFi::ledTicker = nullptr;
volatile bool CustomWiFi::ledState = false;

#endif