#ifndef CUSTOMWIFI_H
#define CUSTOMWIFI_H

#include <WiFi.h>
#include <HTTPClient.h>

// WiFi Configuration
const char* WIFI_SSID = "your_ssid";
const char* WIFI_PASSWORD = "your_password";

// Server Configuration
const char* SERVER_URL = "http://your-server.com/api/sensor-data";
const int REQUEST_TIMEOUT = 5000; // 5 seconds

class CustomWiFi {
public:
  static bool connectWiFi() {
    Serial.println("Connecting to WiFi: " + String(WIFI_SSID));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected!");
      Serial.println("IP address: " + WiFi.localIP().toString());
      return true;
    } else {
      Serial.println("\nFailed to connect to WiFi");
      return false;
    }
  }
  
  static bool sendSensorData(const String& jsonData) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi not connected");
      return false;
    }
    
    HTTPClient http;
    http.setTimeout(REQUEST_TIMEOUT);
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");
    
    int httpResponseCode = http.POST(jsonData);
    
    if (httpResponseCode > 0) {
      Serial.println("POST Response Code: " + String(httpResponseCode));
      String response = http.getString();
      Serial.println("Response: " + response);
      http.end();
      return (httpResponseCode == 200);
    } else {
      Serial.println("Error on sending POST: " + String(httpResponseCode));
      http.end();
      return false;
    }
  }
  
  static void disconnectWiFi() {
    WiFi.disconnect(true); // true = turn off WiFi radio
    Serial.println("WiFi disconnected and powered off");
  }
};

#endif