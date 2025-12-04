#ifndef CUSTOMWIFI_H
#define CUSTOMWIFI_H

#include <WiFi.h>
#include <HTTPClient.h>
#include "nvs_config.h"

// Forward declarations for status LED control
extern void startStatusLEDBlink(uint8_t r, uint8_t g, uint8_t b);
extern void stopStatusLEDBlink();

// Server Configuration
const char* SERVER_URL = "http://echo-http-requests.appspot.com/push/juxtatetsing";
const int REQUEST_TIMEOUT = 5000; // 5 seconds

class CustomWiFi {
private:
  
public:
  
  static bool connectWiFi() {
    // Read WiFi credentials from NVS
    String ssid = NVSConfig::getWiFiSSID();
    String password = NVSConfig::getWiFiPassword();

    Serial.println("Connecting to WiFi SSID: " + ssid);
    Serial.println("Using password: " + password);
    
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
  
  static bool sendSensorData(const String& csvData) {
    if (WiFi.status() != WL_CONNECTED) {
      return false;
    }
    
    // Start orange LED blinking for WiFi transmission
    // Orange = (255, 165, 0)
    startStatusLEDBlink(255, 165, 0);
    
    HTTPClient http;
    http.setTimeout(REQUEST_TIMEOUT);
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "text/csv");
    
    // POST is blocking - LED blinks via Ticker interrupt during transmission
    int httpResponseCode = http.POST(csvData);
    
    // Stop LED blinking and restore to green
    stopStatusLEDBlink();
    
    if (httpResponseCode > 0) {
      http.end();
      return (httpResponseCode == 200);
    } else {
      http.end();
      return false;
    }
  }
  
  static bool isConnected() {
    return WiFi.isConnected();
  }
  
  // Get WiFi RSSI signal strength (returns dBm, or -100 if not connected)
  static int getRSSI() {
    if (WiFi.status() != WL_CONNECTED) {
      return -100; // Return invalid RSSI if not connected
    }
    return WiFi.RSSI();
  }
  
  static void disconnectWiFi() {
    WiFi.disconnect(true); // true = turn off WiFi radio
  }
};

#endif