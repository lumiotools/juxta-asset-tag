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
  static const char* FIRSTBOOT_KEY;
  static const char* MAX_QUEUE_SIZE_KEY;
  static const char* QUEUE_DATA_KEY;

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

  // Check if first boot
  static bool isFirstBoot() {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsHandle);
    
    if (err != ESP_OK) {
      return true; // Assume first boot if NVS can't be opened
    }
    
    uint8_t firstBoot = 1;
    err = nvs_get_u8(nvsHandle, FIRSTBOOT_KEY, &firstBoot);
    nvs_close(nvsHandle);
    
    return (err == ESP_ERR_NVS_NOT_FOUND || firstBoot == 1);
  }

  // Set first boot flag to false
  static bool setFirstBootComplete() {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
    
    if (err != ESP_OK) {
      return false;
    }
    
    err = nvs_set_u8(nvsHandle, FIRSTBOOT_KEY, 0);
    if (err != ESP_OK) {
      nvs_close(nvsHandle);
      return false;
    }
    
    err = nvs_commit(nvsHandle);
    nvs_close(nvsHandle);
    
    return (err == ESP_OK);
  }

  // Get max queue size from NVS
  static int getMaxQueueSize() {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsHandle);
    
    if (err != ESP_OK) {
      return -1;
    }
    
    uint32_t maxSize = 0;
    err = nvs_get_u32(nvsHandle, MAX_QUEUE_SIZE_KEY, &maxSize);
    nvs_close(nvsHandle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      return -1; // Not set yet
    }
    
    return (int)maxSize;
  }

  // Set max queue size in NVS
  static bool setMaxQueueSize(int maxSize) {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
    
    if (err != ESP_OK) {
      return false;
    }
    
    err = nvs_set_u32(nvsHandle, MAX_QUEUE_SIZE_KEY, (uint32_t)maxSize);
    if (err != ESP_OK) {
      nvs_close(nvsHandle);
      return false;
    }
    
    err = nvs_commit(nvsHandle);
    nvs_close(nvsHandle);
    
    return (err == ESP_OK);
  }

  // Get queue data from NVS
  static String getQueueData() {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsHandle);
    
    if (err != ESP_OK) {
      return "";
    }
    
    size_t dataLen = 0;
    err = nvs_get_str(nvsHandle, QUEUE_DATA_KEY, nullptr, &dataLen);
    
    if (err == ESP_ERR_NVS_NOT_FOUND || err != ESP_OK) {
      nvs_close(nvsHandle);
      return "";
    }
    
    char* data = (char*)malloc(dataLen);
    if (data == nullptr) {
      nvs_close(nvsHandle);
      return "";
    }
    
    nvs_get_str(nvsHandle, QUEUE_DATA_KEY, data, &dataLen);
    String result = String(data);
    free(data);
    nvs_close(nvsHandle);
    
    return result;
  }

  // Set queue data in NVS
  static bool setQueueData(const char* data) {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
    
    if (err != ESP_OK) {
      return false;
    }
    
    err = nvs_set_str(nvsHandle, QUEUE_DATA_KEY, data);
    if (err != ESP_OK) {
      nvs_close(nvsHandle);
      return false;
    }
    
    err = nvs_commit(nvsHandle);
    nvs_close(nvsHandle);
    
    return (err == ESP_OK);
  }

  // Calculate available NVS space and estimate max queue size
  // Returns available space in bytes
  // Uses a conservative estimate based on typical ESP32-S3 NVS partition
  static int getAvailableNVSSpace() {
    // ESP32-S3 typically has 24KB NVS partition by default
    // We'll use a conservative estimate of 12KB available for our namespace
    // This accounts for system usage, other namespaces, and overhead
    // The actual available space may be more, but we're being conservative
    const int ESTIMATED_AVAILABLE = 12288; // 12KB (conservative estimate)
    
    // Try to get actual stats if possible (using default "nvs" partition)
    nvs_stats_t nvs_stats;
    esp_err_t err = nvs_get_stats("nvs", &nvs_stats);
    
    if (err == ESP_OK) {
      // Calculate available space (free entries * entry size)
      // Each entry is typically 32 bytes, but we'll be conservative
      // Available space = free_entries * 32 bytes (approximate)
      int availableBytes = nvs_stats.free_entries * 32;
      
      // Use the smaller of estimated or calculated to be safe
      // But ensure at least 4KB is available
      if (availableBytes > 0 && availableBytes < ESTIMATED_AVAILABLE) {
        return availableBytes;
      }
    }
    
    // Fallback to conservative estimate
    return ESTIMATED_AVAILABLE;
  }

};

// Static member definitions
const char* NVSConfig::NVS_NAMESPACE = "wifi_config";
const char* NVSConfig::WIFI_SSID_KEY = "ssid";
const char* NVSConfig::WIFI_PASSWORD_KEY = "password";
const char* NVSConfig::FIRSTBOOT_KEY = "firstboot";
const char* NVSConfig::MAX_QUEUE_SIZE_KEY = "max_q_size";
const char* NVSConfig::QUEUE_DATA_KEY = "queue_data";

#endif
