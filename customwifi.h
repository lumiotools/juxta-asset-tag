#ifndef CUSTOMWIFI_H
#define CUSTOMWIFI_H

#include <WiFi.h>
#include <HTTPClient.h>
#include "nvs_config.h"

// Forward declarations for status LED control
extern long long startStatusLEDBlink(uint8_t r, uint8_t g, uint8_t b);
extern void stopStatusLEDBlink(long long t);

// Server Configuration
// Note: http.begin() requires full URL with protocol (http:// or https://)
const char* SERVER_URL = "http://192.168.0.2:3000"; // Backend server with DB (PMC)
const char* MODEL_SERVER_URL = "http://192.168.0.2:3000/api/model"; // Model server (Juxta) - to be configured
const int REQUEST_TIMEOUT = 60000; // 60 seconds (for large batches and slow servers)

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
    
    WiFi.setAutoReconnect(true); // Enable auto-reconnect for stability
    WiFi.persistent(false); // Don't save WiFi config to flash (reduces wear)
    WiFi.begin(ssid.c_str(), password.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
    }
    
    return (WiFi.status() == WL_CONNECTED);
  }
  
  static bool sendSensorData(const String& csvData) {
    Serial.print("CustomWiFi::sendSensorData: Entry - WiFi.status()=");
    Serial.print(WiFi.status());
    Serial.print(", WiFi.isConnected()=");
    Serial.print(WiFi.isConnected());
    Serial.print(", SSID=");
    Serial.println(WiFi.SSID());
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("CustomWiFi::sendSensorData: ERROR - WiFi.status() != WL_CONNECTED");
      return false;
    }
    
    if (!WiFi.isConnected()) {
      Serial.println("CustomWiFi::sendSensorData: ERROR - WiFi.isConnected() returned false");
      return false;
    }
    
    Serial.print("CustomWiFi::sendSensorData: Sending ");
    Serial.print(csvData.length());
    Serial.print(" bytes to ");
    Serial.println(SERVER_URL);
    
    // Start white LED blinking for WiFi transmission
    // White = (255, 255, 255)
    long long startTime = startStatusLEDBlink(255, 255, 255);
    
    HTTPClient http;
    http.setTimeout(REQUEST_TIMEOUT);
    
    Serial.println("CustomWiFi::sendSensorData: Initializing HTTP client...");
    Serial.print("CustomWiFi::sendSensorData: URL: ");
    Serial.println(SERVER_URL);
    
    // Try with full URL first
    bool httpBegin = http.begin(SERVER_URL);
    
    if (!httpBegin) {
      Serial.println("CustomWiFi::sendSensorData: ERROR - http.begin() failed");
      Serial.print("CustomWiFi::sendSensorData: WiFi.status()=");
      Serial.print(WiFi.status());
      Serial.print(", IP address: ");
      Serial.println(WiFi.localIP());
      
      // Try alternative: begin with WiFiClient
      WiFiClient client;
      bool httpBegin2 = http.begin(client, SERVER_URL);
      if (httpBegin2) {
        Serial.println("CustomWiFi::sendSensorData: http.begin() succeeded with WiFiClient");
        httpBegin = true;
      } else {
        Serial.println("CustomWiFi::sendSensorData: ERROR - http.begin() with WiFiClient also failed");
        stopStatusLEDBlink(startTime);
        return false;
      }
    } else {
      Serial.println("CustomWiFi::sendSensorData: http.begin() succeeded");
    }
    
    http.addHeader("Content-Type", "text/csv");
    http.addHeader("Content-Length", String(csvData.length()));
    http.addHeader("Connection", "close"); // Force connection close to prevent reuse issues
    
    Serial.println("CustomWiFi::sendSensorData: Sending POST request...");
    // POST is blocking - LED blinks via Ticker interrupt during transmission
    int httpResponseCode = http.POST(csvData);
    
    Serial.print("CustomWiFi::sendSensorData: HTTP response code: ");
    Serial.println(httpResponseCode);
    
    // Stop LED blinking and restore to green
    stopStatusLEDBlink(startTime);
    
    http.end();
    
    if (httpResponseCode > 0) {
      bool success = (httpResponseCode == 200);
      if (success) {
        Serial.println("CustomWiFi::sendSensorData: SUCCESS - HTTP 200");
      } else {
        Serial.print("CustomWiFi::sendSensorData: HTTP error - code ");
        Serial.println(httpResponseCode);
      }
      return success;
    } else {
      Serial.print("CustomWiFi::sendSensorData: ERROR - HTTP request failed (code=");
      Serial.print(httpResponseCode);
      Serial.println(")");
      return false;
    }
  }
  
  static bool isConnected() {
    // Use both checks for reliability
    bool connected = (WiFi.status() == WL_CONNECTED) && WiFi.isConnected();
    if (!connected) {
      // Debug output
      Serial.print("WiFi status check: status=");
      Serial.print(WiFi.status());
      Serial.print(", isConnected()=");
      Serial.print(WiFi.isConnected());
      Serial.print(", SSID=");
      Serial.println(WiFi.SSID());
    }
    return connected;
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
  
  // Send IMU data to model server with lat/long prefix and receive delta position
  // Format: (lat, long), imuObj1, imuObj2, ...
  // Returns: true if successful, deltaLat and deltaLon are updated with response
  static bool sendToModelServer(const String& imuDataWithPrefix, double& deltaLat, double& deltaLon) {
    if (MODEL_SERVER_URL[0] == '\0') {
      Serial.println("CustomWiFi::sendToModelServer: ERROR - Model server URL not configured");
      return false;
    }
    
    if (WiFi.status() != WL_CONNECTED || !WiFi.isConnected()) {
      Serial.println("CustomWiFi::sendToModelServer: ERROR - WiFi not connected");
      return false;
    }
    
    Serial.print("CustomWiFi::sendToModelServer: Sending ");
    Serial.print(imuDataWithPrefix.length());
    Serial.print(" bytes to model server: ");
    Serial.println(MODEL_SERVER_URL);
    
    // Start white LED blinking for WiFi transmission
    long long startTime = startStatusLEDBlink(255, 255, 255);
    
    HTTPClient http;
    http.setTimeout(REQUEST_TIMEOUT);
    
    bool httpBegin = http.begin(MODEL_SERVER_URL);
    if (!httpBegin) {
      WiFiClient client;
      httpBegin = http.begin(client, MODEL_SERVER_URL);
    }
    
    if (!httpBegin) {
      Serial.println("CustomWiFi::sendToModelServer: ERROR - http.begin() failed");
      stopStatusLEDBlink(startTime);
      return false;
    }
    
    http.addHeader("Content-Type", "text/csv");
    http.addHeader("Content-Length", String(imuDataWithPrefix.length()));
    http.addHeader("Connection", "close");
    
    int httpResponseCode = http.POST(imuDataWithPrefix);
    
    Serial.print("CustomWiFi::sendToModelServer: HTTP response code: ");
    Serial.println(httpResponseCode);
    
    bool success = false;
    deltaLat = 0.0;
    deltaLon = 0.0;
    
    if (httpResponseCode == 200) {
      String response = http.getString();
      Serial.print("CustomWiFi::sendToModelServer: Response: ");
      Serial.println(response);
      
      // Parse delta position from response
      // Expected format: "delta_lat,delta_lon" or JSON format
      // For now, assume CSV format: "delta_lat,delta_lon"
      int commaPos = response.indexOf(',');
      if (commaPos > 0) {
        deltaLat = response.substring(0, commaPos).toFloat();
        deltaLon = response.substring(commaPos + 1).toFloat();
        success = true;
        Serial.print("CustomWiFi::sendToModelServer: Delta position received: (");
        Serial.print(deltaLat, 7);
        Serial.print(", ");
        Serial.print(deltaLon, 7);
        Serial.println(")");
      } else {
        Serial.println("CustomWiFi::sendToModelServer: WARNING - Could not parse delta position from response");
      }
    } else {
      Serial.print("CustomWiFi::sendToModelServer: ERROR - HTTP error code ");
      Serial.println(httpResponseCode);
    }
    
    stopStatusLEDBlink(startTime);
    http.end();
    
    return success;
  }
};

#endif