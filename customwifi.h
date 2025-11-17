#ifndef CUSTOMWIFI_H
#define CUSTOMWIFI_H

#include <WiFi.h>
#include <HTTPClient.h>
#include "nvs_config.h"

// Server Configuration
const char* SERVER_URL = "http://echo-http-requests.appspot.com/push/juxtatetsing";
const int REQUEST_TIMEOUT = 5000; // 5 seconds

class CustomWiFi {
private:
  static void (*onTransmissionComplete)(void);
  
public:
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
  
  static void setTransmissionCallback(void (*callback)(void)) {
    onTransmissionComplete = callback;
  }
  
  static bool sendSensorData(const String& jsonData) {
    if (WiFi.status() != WL_CONNECTED) {
      return false;
    }
    
    HTTPClient http;
    http.setTimeout(REQUEST_TIMEOUT);
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");
    
    int httpResponseCode = http.POST(jsonData);
    
    if (httpResponseCode > 0) {
      http.end();
      
      // Trigger LED pulse on successful transmission
      if (httpResponseCode == 200 && onTransmissionComplete != nullptr) {
        onTransmissionComplete();
      }
      
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

// Static member definition
void (*CustomWiFi::onTransmissionComplete)(void) = nullptr;

#endif