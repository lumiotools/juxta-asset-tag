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
    
    return (err == ESP_OK);
  }

  // Read WiFi SSID from NVS
  static String getWiFiSSID() {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsHandle);
    
    if (err != ESP_OK) {
      return "";
    }
    
    size_t ssidLen = 0;
    err = nvs_get_str(nvsHandle, WIFI_SSID_KEY, nullptr, &ssidLen);
    
    if (err == ESP_ERR_NVS_NOT_FOUND || err != ESP_OK) {
      nvs_close(nvsHandle);
      return "";
    }
    
    char ssid[ssidLen];
    nvs_get_str(nvsHandle, WIFI_SSID_KEY, ssid, &ssidLen);
    nvs_close(nvsHandle);
    
    return String(ssid);
  }

  // Read WiFi Password from NVS
  static String getWiFiPassword() {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsHandle);
    
    if (err != ESP_OK) {
      return "";
    }
    
    size_t passwordLen = 0;
    err = nvs_get_str(nvsHandle, WIFI_PASSWORD_KEY, nullptr, &passwordLen);
    
    if (err == ESP_ERR_NVS_NOT_FOUND || err != ESP_OK) {
      nvs_close(nvsHandle);
      return "";
    }
    
    char password[passwordLen];
    nvs_get_str(nvsHandle, WIFI_PASSWORD_KEY, password, &passwordLen);
    nvs_close(nvsHandle);
    
    return String(password);
  }

  // Write WiFi SSID to NVS
  static bool setWiFiSSID(const char* ssid) {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
    
    if (err != ESP_OK) {
      return false;
    }
    
    err = nvs_set_str(nvsHandle, WIFI_SSID_KEY, ssid);
    if (err != ESP_OK) {
      nvs_close(nvsHandle);
      return false;
    }
    
    err = nvs_commit(nvsHandle);
    nvs_close(nvsHandle);
    
    return (err == ESP_OK);
  }

  // Write WiFi Password to NVS
  static bool setWiFiPassword(const char* password) {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
    
    if (err != ESP_OK) {
      return false;
    }
    
    err = nvs_set_str(nvsHandle, WIFI_PASSWORD_KEY, password);
    if (err != ESP_OK) {
      nvs_close(nvsHandle);
      return false;
    }
    
    err = nvs_commit(nvsHandle);
    nvs_close(nvsHandle);
    
    return (err == ESP_OK);
  }

};

// Static member definitions
const char* NVSConfig::NVS_NAMESPACE = "wifi_config";
const char* NVSConfig::WIFI_SSID_KEY = "ssid";
const char* NVSConfig::WIFI_PASSWORD_KEY = "password";

#endif
