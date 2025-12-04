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
  static const char* DEBUG_MODE_KEY;
  static const char* FIRSTBOOT_KEY;
  static const char* MAX_QUEUE_SIZE_KEY;
  static const char* QUEUE_DATA_KEY;
  static const char* QUEUE_WRITE_PTR_KEY;
  static const char* QUEUE_READ_PTR_KEY;
  static const char* GPS_LAT_KEY;
  static const char* GPS_LON_KEY;
  static const char* GPS_ALT_KEY;
  static const char* GPS_SPEED_KEY;
  static const char* GPS_HEADING_KEY;
  static const char* GPS_SAT_KEY;
  static const char* GPS_HDOP_KEY;
  static const char* GPS_HAS_FIX_KEY;

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

private:
  static String readStringNVS(const char* key) {
    nvs_handle_t nvsHandle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsHandle) != ESP_OK) return "";
    
    size_t len = 0;
    esp_err_t err = nvs_get_str(nvsHandle, key, nullptr, &len);
    if (err != ESP_OK) {
      nvs_close(nvsHandle);
      return "";
    }
    
    char* buffer = (char*)malloc(len);
    if (!buffer) {
      nvs_close(nvsHandle);
      return "";
    }
    
    nvs_get_str(nvsHandle, key, buffer, &len);
    String result(buffer);
    free(buffer);
    nvs_close(nvsHandle);
    return result;
  }

  static bool writeStringNVS(const char* key, const char* value) {
    nvs_handle_t nvsHandle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle) != ESP_OK) return false;
    
    esp_err_t err = nvs_set_str(nvsHandle, key, value);
    if (err == ESP_OK) err = nvs_commit(nvsHandle);
    nvs_close(nvsHandle);
    return (err == ESP_OK);
  }

  static uint32_t readU32NVS(const char* key, uint32_t defaultVal = 0) {
    nvs_handle_t nvsHandle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsHandle) != ESP_OK) return defaultVal;
    
    uint32_t value = defaultVal;
    nvs_get_u32(nvsHandle, key, &value);
    nvs_close(nvsHandle);
    return value;
  }

  static bool writeU32NVS(const char* key, uint32_t value) {
    nvs_handle_t nvsHandle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle) != ESP_OK) return false;
    
    esp_err_t err = nvs_set_u32(nvsHandle, key, value);
    if (err == ESP_OK) err = nvs_commit(nvsHandle);
    nvs_close(nvsHandle);
    return (err == ESP_OK);
  }

  static uint8_t readU8NVS(const char* key, uint8_t defaultVal = 0) {
    nvs_handle_t nvsHandle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsHandle) != ESP_OK) return defaultVal;
    
    uint8_t value = defaultVal;
    nvs_get_u8(nvsHandle, key, &value);
    nvs_close(nvsHandle);
    return value;
  }

  static bool writeU8NVS(const char* key, uint8_t value) {
    nvs_handle_t nvsHandle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle) != ESP_OK) return false;
    
    esp_err_t err = nvs_set_u8(nvsHandle, key, value);
    if (err == ESP_OK) err = nvs_commit(nvsHandle);
    nvs_close(nvsHandle);
    return (err == ESP_OK);
  }

public:
  static String getWiFiSSID() { return readStringNVS(WIFI_SSID_KEY); }
  static String getWiFiPassword() { return readStringNVS(WIFI_PASSWORD_KEY); }
  static bool setWiFiSSID(const char* ssid) { return writeStringNVS(WIFI_SSID_KEY, ssid); }
  static bool setWiFiPassword(const char* password) { return writeStringNVS(WIFI_PASSWORD_KEY, password); }

  static uint8_t getDebugMode() { return readU8NVS(DEBUG_MODE_KEY, 0); } // Default to 0
  static bool setDebugMode(uint8_t debugMode) { return writeU8NVS(DEBUG_MODE_KEY, debugMode); }

  static bool isFirstBoot() {
    return (readU8NVS(FIRSTBOOT_KEY, 1) == 1);
  }

  static bool setFirstBootComplete() {
    return writeU8NVS(FIRSTBOOT_KEY, 0);
  }

  static int getMaxQueueSize() {
    uint32_t val = readU32NVS(MAX_QUEUE_SIZE_KEY, 0);
    return (val == 0) ? -1 : (int)val;
  }

  static bool setMaxQueueSize(int maxSize) {
    return writeU32NVS(MAX_QUEUE_SIZE_KEY, (uint32_t)maxSize);
  }

  static String getQueueData() {
    return readStringNVS(QUEUE_DATA_KEY);
  }

  static bool setQueueData(const char* data) {
    return writeStringNVS(QUEUE_DATA_KEY, data);
  }

  static int getAvailableNVSSpace() {
    const int ESTIMATED_AVAILABLE = 12288;
    nvs_stats_t nvs_stats;
    if (nvs_get_stats("nvs", &nvs_stats) == ESP_OK) {
      int availableBytes = nvs_stats.free_entries * 32;
      if (availableBytes > 0 && availableBytes < ESTIMATED_AVAILABLE) {
        return availableBytes;
      }
    }
    return ESTIMATED_AVAILABLE;
  }

  static uint32_t getQueueWritePtr() { return readU32NVS(QUEUE_WRITE_PTR_KEY, 0); }
  static bool setQueueWritePtr(uint32_t writePtr) { return writeU32NVS(QUEUE_WRITE_PTR_KEY, writePtr); }
  static uint32_t getQueueReadPtr() { return readU32NVS(QUEUE_READ_PTR_KEY, 0); }
  static bool setQueueReadPtr(uint32_t readPtr) { return writeU32NVS(QUEUE_READ_PTR_KEY, readPtr); }

  // GPS Last Known Location Storage (for persistence across deep sleep)
  static bool saveLastKnownGPS(double lat, double lon, double alt, float speed, float heading, int satellites, float hdop, bool hasFix) {
    nvs_handle_t nvsHandle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle) != ESP_OK) return false;
    
    bool success = true;
    // Store doubles as strings to preserve precision
    char latStr[32], lonStr[32], altStr[32];
    snprintf(latStr, sizeof(latStr), "%.7f", lat);
    snprintf(lonStr, sizeof(lonStr), "%.7f", lon);
    snprintf(altStr, sizeof(altStr), "%.2f", alt);
    
    success &= (nvs_set_str(nvsHandle, GPS_LAT_KEY, latStr) == ESP_OK);
    success &= (nvs_set_str(nvsHandle, GPS_LON_KEY, lonStr) == ESP_OK);
    success &= (nvs_set_str(nvsHandle, GPS_ALT_KEY, altStr) == ESP_OK);
    success &= (nvs_set_blob(nvsHandle, GPS_SPEED_KEY, &speed, sizeof(float)) == ESP_OK);
    success &= (nvs_set_blob(nvsHandle, GPS_HEADING_KEY, &heading, sizeof(float)) == ESP_OK);
    success &= (nvs_set_u32(nvsHandle, GPS_SAT_KEY, (uint32_t)satellites) == ESP_OK);
    success &= (nvs_set_blob(nvsHandle, GPS_HDOP_KEY, &hdop, sizeof(float)) == ESP_OK);
    success &= (nvs_set_u8(nvsHandle, GPS_HAS_FIX_KEY, hasFix ? 1 : 0) == ESP_OK);
    
    if (success) {
      nvs_commit(nvsHandle);
    }
    nvs_close(nvsHandle);
    return success;
  }
  
  static bool getLastKnownGPS(double* lat, double* lon, double* alt, float* speed, float* heading, int* satellites, float* hdop, bool* hasFix) {
    nvs_handle_t nvsHandle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsHandle) != ESP_OK) return false;
    
    bool success = true;
    size_t required_size;
    
    // Read latitude
    size_t len = 0;
    if (nvs_get_str(nvsHandle, GPS_LAT_KEY, nullptr, &len) == ESP_OK && len > 0) {
      char* buffer = (char*)malloc(len);
      if (buffer) {
        nvs_get_str(nvsHandle, GPS_LAT_KEY, buffer, &len);
        *lat = String(buffer).toDouble();
        free(buffer);
      } else {
        success = false;
      }
    } else {
      success = false;
    }
    
    // Read longitude
    len = 0;
    if (success && nvs_get_str(nvsHandle, GPS_LON_KEY, nullptr, &len) == ESP_OK && len > 0) {
      char* buffer = (char*)malloc(len);
      if (buffer) {
        nvs_get_str(nvsHandle, GPS_LON_KEY, buffer, &len);
        *lon = String(buffer).toDouble();
        free(buffer);
      } else {
        success = false;
      }
    } else {
      success = false;
    }
    
    // Read altitude
    len = 0;
    if (success && nvs_get_str(nvsHandle, GPS_ALT_KEY, nullptr, &len) == ESP_OK && len > 0) {
      char* buffer = (char*)malloc(len);
      if (buffer) {
        nvs_get_str(nvsHandle, GPS_ALT_KEY, buffer, &len);
        *alt = String(buffer).toDouble();
        free(buffer);
      } else {
        success = false;
      }
    } else {
      success = false;
    }
    
    // Read speed
    required_size = sizeof(float);
    if (success && nvs_get_blob(nvsHandle, GPS_SPEED_KEY, speed, &required_size) != ESP_OK) {
      success = false;
    }
    
    // Read heading
    required_size = sizeof(float);
    if (success && nvs_get_blob(nvsHandle, GPS_HEADING_KEY, heading, &required_size) != ESP_OK) {
      success = false;
    }
    
    // Read satellites
    uint32_t sat = 0;
    if (success && nvs_get_u32(nvsHandle, GPS_SAT_KEY, &sat) == ESP_OK) {
      *satellites = (int)sat;
    } else {
      success = false;
    }
    
    // Read HDOP
    required_size = sizeof(float);
    if (success && nvs_get_blob(nvsHandle, GPS_HDOP_KEY, hdop, &required_size) != ESP_OK) {
      success = false;
    }
    
    // Read hasFix
    uint8_t fix = 0;
    if (success && nvs_get_u8(nvsHandle, GPS_HAS_FIX_KEY, &fix) == ESP_OK) {
      *hasFix = (fix == 1);
    } else {
      success = false;
    }
    
    nvs_close(nvsHandle);
    return success;
  }

};

// Static member definitions
const char* NVSConfig::NVS_NAMESPACE = "wifi_config";
const char* NVSConfig::WIFI_SSID_KEY = "ssid";
const char* NVSConfig::WIFI_PASSWORD_KEY = "password";
const char* NVSConfig::DEBUG_MODE_KEY = "debug_mode";
const char* NVSConfig::FIRSTBOOT_KEY = "firstboot";
const char* NVSConfig::MAX_QUEUE_SIZE_KEY = "max_q_size";
const char* NVSConfig::QUEUE_DATA_KEY = "queue_data";
const char* NVSConfig::QUEUE_WRITE_PTR_KEY = "q_write_ptr";
const char* NVSConfig::QUEUE_READ_PTR_KEY = "q_read_ptr";
const char* NVSConfig::GPS_LAT_KEY = "gps_lat";
const char* NVSConfig::GPS_LON_KEY = "gps_lon";
const char* NVSConfig::GPS_ALT_KEY = "gps_alt";
const char* NVSConfig::GPS_SPEED_KEY = "gps_speed";
const char* NVSConfig::GPS_HEADING_KEY = "gps_heading";
const char* NVSConfig::GPS_SAT_KEY = "gps_sat";
const char* NVSConfig::GPS_HDOP_KEY = "gps_hdop";
const char* NVSConfig::GPS_HAS_FIX_KEY = "gps_has_fix";

#endif
