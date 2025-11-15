#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include <nvs_flash.h>
#include <nvs.h>
#include <Arduino.h>

class NVSConfig {
private:
  static const char* NVS_NAMESPACE;
  static const char* WIFI_SSID_KEY;
  static const char* WIFI_PASSWORD_KEY;

public:
  // Initialize NVS flash memory
  static bool initializeNVS() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      // NVS partition was truncated and needs to be erased
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
    }
    
    if (err == ESP_OK) {
      Serial.println("NVS initialized successfully");
      return true;
    } else {
      Serial.println("ERROR: Failed to initialize NVS");
      return false;
    }
  }

  // Read WiFi SSID from NVS
  static String getWiFiSSID() {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsHandle);
    
    if (err != ESP_OK) {
      Serial.println("ERROR: Failed to open NVS namespace for reading");
      return "";
    }
    
    size_t ssidLen = 0;
    err = nvs_get_str(nvsHandle, WIFI_SSID_KEY, nullptr, &ssidLen);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      Serial.println("WARNING: WiFi SSID not found in NVS");
      nvs_close(nvsHandle);
      return "";
    } else if (err != ESP_OK) {
      Serial.println("ERROR: Failed to read WiFi SSID from NVS");
      nvs_close(nvsHandle);
      return "";
    }
    
    char ssid[ssidLen];
    nvs_get_str(nvsHandle, WIFI_SSID_KEY, ssid, &ssidLen);
    nvs_close(nvsHandle);
    
    Serial.println("WiFi SSID read from NVS: " + String(ssid));
    return String(ssid);
  }

  // Read WiFi Password from NVS
  static String getWiFiPassword() {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsHandle);
    
    if (err != ESP_OK) {
      Serial.println("ERROR: Failed to open NVS namespace for reading");
      return "";
    }
    
    size_t passwordLen = 0;
    err = nvs_get_str(nvsHandle, WIFI_PASSWORD_KEY, nullptr, &passwordLen);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      Serial.println("WARNING: WiFi Password not found in NVS");
      nvs_close(nvsHandle);
      return "";
    } else if (err != ESP_OK) {
      Serial.println("ERROR: Failed to read WiFi Password from NVS");
      nvs_close(nvsHandle);
      return "";
    }
    
    char password[passwordLen];
    nvs_get_str(nvsHandle, WIFI_PASSWORD_KEY, password, &passwordLen);
    nvs_close(nvsHandle);
    
    Serial.println("WiFi Password read from NVS (length: " + String(passwordLen) + ")");
    return String(password);
  }

  // Write WiFi SSID to NVS
  static bool setWiFiSSID(const char* ssid) {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
    
    if (err != ESP_OK) {
      Serial.println("ERROR: Failed to open NVS namespace for writing");
      return false;
    }
    
    err = nvs_set_str(nvsHandle, WIFI_SSID_KEY, ssid);
    if (err != ESP_OK) {
      Serial.println("ERROR: Failed to write WiFi SSID to NVS");
      nvs_close(nvsHandle);
      return false;
    }
    
    err = nvs_commit(nvsHandle);
    nvs_close(nvsHandle);
    
    if (err == ESP_OK) {
      Serial.println("WiFi SSID written to NVS: " + String(ssid));
      return true;
    } else {
      Serial.println("ERROR: Failed to commit WiFi SSID to NVS");
      return false;
    }
  }

  // Write WiFi Password to NVS
  static bool setWiFiPassword(const char* password) {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
    
    if (err != ESP_OK) {
      Serial.println("ERROR: Failed to open NVS namespace for writing");
      return false;
    }
    
    err = nvs_set_str(nvsHandle, WIFI_PASSWORD_KEY, password);
    if (err != ESP_OK) {
      Serial.println("ERROR: Failed to write WiFi Password to NVS");
      nvs_close(nvsHandle);
      return false;
    }
    
    err = nvs_commit(nvsHandle);
    nvs_close(nvsHandle);
    
    if (err == ESP_OK) {
      Serial.println("WiFi Password written to NVS");
      return true;
    } else {
      Serial.println("ERROR: Failed to commit WiFi Password to NVS");
      return false;
    }
  }

  // Clear WiFi credentials from NVS
  static bool clearWiFiCredentials() {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
    
    if (err != ESP_OK) {
      Serial.println("ERROR: Failed to open NVS namespace");
      return false;
    }
    
    nvs_erase_key(nvsHandle, WIFI_SSID_KEY);
    nvs_erase_key(nvsHandle, WIFI_PASSWORD_KEY);
    err = nvs_commit(nvsHandle);
    nvs_close(nvsHandle);
    
    if (err == ESP_OK) {
      Serial.println("WiFi credentials cleared from NVS");
      return true;
    } else {
      Serial.println("ERROR: Failed to clear WiFi credentials");
      return false;
    }
  }
};

// Static member definitions
const char* NVSConfig::NVS_NAMESPACE = "wifi_config";
const char* NVSConfig::WIFI_SSID_KEY = "ssid";
const char* NVSConfig::WIFI_PASSWORD_KEY = "password";

#endif
