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
  static const char* GPS_ACTIVE_KEY;
  static const char* FIRSTBOOT_KEY;
  static const char* MAX_QUEUE_SIZE_KEY;
  static const char* QUEUE_DATA_KEY;
  static const char* QUEUE_WRITE_PTR_KEY;
  static const char* QUEUE_READ_PTR_KEY;
  static const char* GPS_LAST_LAT_KEY;
  static const char* GPS_LAST_LON_KEY;
  static const char* GPS_LAST_ALT_KEY;
  static const char* GPS_LAST_FIX_KEY;
  static const char* GPS_LAST_SPEED_KEY;
  static const char* GPS_LAST_HEADING_KEY;
  static const char* GPS_LAST_SATELLITES_KEY;
  static const char* GPS_LAST_FIXTYPE_KEY;
  static const char* GPS_LAST_HDOP_KEY;
  static const char* GPS_LAST_FIX_TIME_KEY;
  static const char* CYCLE_TIME_KEY;
  static const char* IMU_WRITE_PTR_KEY;
  static const char* IMU_READ_PTR_KEY;
  static const char* IMU_TOTAL_READINGS_KEY;
  static const char* CSV_WRITE_PTR_KEY;
  static const char* CSV_READ_PTR_KEY;
  static const char* GPS_ACCURACY_THRESHOLD_KEY;
  static const char* GPS_READ_CYCLE_TIME_KEY;
  static const char* GPS_ON_AFTER_KEY;
  static const char* INITIAL_POSITION_LAT_KEY;
  static const char* INITIAL_POSITION_LON_KEY;
  static const char* SCENARIO_STATE_KEY;
  static const char* LAST_KNOWN_POSITION_LAT_KEY;
  static const char* LAST_KNOWN_POSITION_LON_KEY;

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

  static uint64_t readU64NVS(const char* key, uint64_t defaultVal = 0) {
    nvs_handle_t nvsHandle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsHandle) != ESP_OK) return defaultVal;
    
    uint64_t value = defaultVal;
    nvs_get_u64(nvsHandle, key, &value);
    nvs_close(nvsHandle);
    return value;
  }

  static bool writeU64NVS(const char* key, uint64_t value) {
    nvs_handle_t nvsHandle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle) != ESP_OK) return false;
    
    esp_err_t err = nvs_set_u64(nvsHandle, key, value);
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

  static uint8_t getGPSActive() { return readU8NVS(GPS_ACTIVE_KEY, 0); } // Default to 0 (GPS OFF)
  static bool setGPSActive(uint8_t gpsActive) { return writeU8NVS(GPS_ACTIVE_KEY, gpsActive); }

  static uint32_t getCycleTime() { return readU32NVS(CYCLE_TIME_KEY, 900); } // Default to 900 seconds (15 minutes)
  static bool setCycleTime(uint32_t cycleTimeSeconds) { return writeU32NVS(CYCLE_TIME_KEY, cycleTimeSeconds); }

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
  
  // IMU Flash Storage pointers
  static uint32_t getIMUWritePtr() { return readU32NVS(IMU_WRITE_PTR_KEY, 0x100000); } // Default to IMU start
  static bool setIMUWritePtr(uint32_t writePtr) { return writeU32NVS(IMU_WRITE_PTR_KEY, writePtr); }
  static uint32_t getIMUReadPtr() { return readU32NVS(IMU_READ_PTR_KEY, 0x100000); } // Default to IMU start
  static bool setIMUReadPtr(uint32_t readPtr) { return writeU32NVS(IMU_READ_PTR_KEY, readPtr); }
  static uint32_t getIMUTotalReadings() { return readU32NVS(IMU_TOTAL_READINGS_KEY, 0); }
  static bool setIMUTotalReadings(uint32_t count) { return writeU32NVS(IMU_TOTAL_READINGS_KEY, count); }
  
  // Unified CSV Storage pointers (uses entire external flash)
  static uint32_t getCSVWritePtr() { return readU32NVS(CSV_WRITE_PTR_KEY, 0); } // Default to start
  static bool setCSVWritePtr(uint32_t writePtr) { return writeU32NVS(CSV_WRITE_PTR_KEY, writePtr); }
  static uint32_t getCSVReadPtr() { return readU32NVS(CSV_READ_PTR_KEY, 0); } // Default to start
  static bool setCSVReadPtr(uint32_t readPtr) { return writeU32NVS(CSV_READ_PTR_KEY, readPtr); }

  // GPS last known location storage (using strings to handle negative coordinates)
  // Legacy function - kept for backward compatibility
  static bool saveLastGPSLocation(double latitude, double longitude, double altitude) {
    bool success = true;
    success &= writeU32NVS(GPS_LAST_FIX_KEY, 1); // Mark as having valid fix
    
    // Store coordinates as strings to preserve precision and handle negative values
    char latStr[20], lonStr[20], altStr[20];
    snprintf(latStr, sizeof(latStr), "%.7f", latitude);
    snprintf(lonStr, sizeof(lonStr), "%.7f", longitude);
    snprintf(altStr, sizeof(altStr), "%.2f", altitude);
    
    success &= writeStringNVS(GPS_LAST_LAT_KEY, latStr);
    success &= writeStringNVS(GPS_LAST_LON_KEY, lonStr);
    success &= writeStringNVS(GPS_LAST_ALT_KEY, altStr);
    return success;
  }

  static bool loadLastGPSLocation(double& latitude, double& longitude, double& altitude) {
    uint32_t hasFix = readU32NVS(GPS_LAST_FIX_KEY, 0);
    if (hasFix == 0) return false; // No saved location
    
    String latStr = readStringNVS(GPS_LAST_LAT_KEY);
    String lonStr = readStringNVS(GPS_LAST_LON_KEY);
    String altStr = readStringNVS(GPS_LAST_ALT_KEY);
    
    if (latStr.length() == 0 || lonStr.length() == 0 || altStr.length() == 0) {
      return false; // Invalid data
    }
    
    latitude = latStr.toFloat();
    longitude = lonStr.toFloat();
    altitude = altStr.toFloat();
    return true;
  }

  // Save complete GPS data structure
  // Note: GPSData struct must be defined when this template is instantiated
  template<typename GPSDataType>
  static bool saveLastGPSData(const GPSDataType& gpsData) {
    if (!gpsData.hasValidFix) return false;
    
    bool success = true;
    success &= writeU32NVS(GPS_LAST_FIX_KEY, 1); // Mark as having valid fix
    
    // Store coordinates as strings to preserve precision and handle negative values
    char latStr[20], lonStr[20], altStr[20], speedStr[20], headingStr[20], hdopStr[20];
    snprintf(latStr, sizeof(latStr), "%.7f", gpsData.latitude);
    snprintf(lonStr, sizeof(lonStr), "%.7f", gpsData.longitude);
    snprintf(altStr, sizeof(altStr), "%.2f", gpsData.altitude);
    snprintf(speedStr, sizeof(speedStr), "%.3f", gpsData.speed);
    snprintf(headingStr, sizeof(headingStr), "%.2f", gpsData.heading);
    snprintf(hdopStr, sizeof(hdopStr), "%.2f", gpsData.hdop);
    
    success &= writeStringNVS(GPS_LAST_LAT_KEY, latStr);
    success &= writeStringNVS(GPS_LAST_LON_KEY, lonStr);
    success &= writeStringNVS(GPS_LAST_ALT_KEY, altStr);
    success &= writeStringNVS(GPS_LAST_SPEED_KEY, speedStr);
    success &= writeStringNVS(GPS_LAST_HEADING_KEY, headingStr);
    success &= writeU32NVS(GPS_LAST_SATELLITES_KEY, (uint32_t)gpsData.satellites);
    success &= writeU8NVS(GPS_LAST_FIXTYPE_KEY, (uint8_t)gpsData.fixType);
    success &= writeStringNVS(GPS_LAST_HDOP_KEY, hdopStr);
    // Store lastFixTimeMillis (64-bit timestamp in milliseconds)
    success &= writeU64NVS(GPS_LAST_FIX_TIME_KEY, (uint64_t)gpsData.lastFixTimeMillis);
    
    return success;
  }

  // Load complete GPS data structure
  template<typename GPSDataType>
  static bool loadLastGPSData(GPSDataType& gpsData) {
    uint32_t hasFix = readU32NVS(GPS_LAST_FIX_KEY, 0);
    if (hasFix == 0) return false; // No saved location
    
    String latStr = readStringNVS(GPS_LAST_LAT_KEY);
    String lonStr = readStringNVS(GPS_LAST_LON_KEY);
    String altStr = readStringNVS(GPS_LAST_ALT_KEY);
    
    if (latStr.length() == 0 || lonStr.length() == 0 || altStr.length() == 0) {
      return false; // Invalid data
    }
    
    gpsData.hasValidFix = true;
    gpsData.latitude = latStr.toFloat();
    gpsData.longitude = lonStr.toFloat();
    gpsData.altitude = altStr.toFloat();
    
    // Load additional fields if available (for backward compatibility, use defaults if not present)
    String speedStr = readStringNVS(GPS_LAST_SPEED_KEY);
    String headingStr = readStringNVS(GPS_LAST_HEADING_KEY);
    String hdopStr = readStringNVS(GPS_LAST_HDOP_KEY);
    
    gpsData.speed = (speedStr.length() > 0) ? speedStr.toFloat() : 0.0;
    gpsData.heading = (headingStr.length() > 0) ? headingStr.toFloat() : 0.0;
    gpsData.satellites = (int)readU32NVS(GPS_LAST_SATELLITES_KEY, 0);
    gpsData.fixType = (int)readU8NVS(GPS_LAST_FIXTYPE_KEY, 0);
    gpsData.hdop = (hdopStr.length() > 0) ? hdopStr.toFloat() : 0.0;
    gpsData.lastFixTimeMillis = readU64NVS(GPS_LAST_FIX_TIME_KEY, 0);
    
    return true;
  }

  static bool hasLastGPSLocation() {
    return (readU32NVS(GPS_LAST_FIX_KEY, 0) == 1);
  }
  
  // GPS Accuracy Threshold (declaration only, no default value)
  static float getGPSAccuracyThreshold();
  static bool setGPSAccuracyThreshold(float threshold);
  
  // GPS Read Cycle Time (declaration only, no default value)
  static uint32_t getGPSReadCycleTime();
  static bool setGPSReadCycleTime(uint32_t cycleTimeSeconds);
  
  // GPS On After (delay in seconds before turning GPS on again in Scenario 2)
  static uint32_t getGPSOnAfter();
  static bool setGPSOnAfter(uint32_t seconds);
  
  // Initial Position from UI (Scenario 4)
  static bool setInitialPosition(double latitude, double longitude);
  static bool getInitialPosition(double& latitude, double& longitude);
  static bool hasInitialPosition();
  static bool clearInitialPosition();
  
  // Scenario State (persisted through deep sleep, reset on power_on)
  static uint8_t getScenarioState();
  static bool setScenarioState(uint8_t scenario);
  
  // Last Known Position (used for lat/long prefix in model server transmission)
  static bool saveLastKnownPosition(double latitude, double longitude);
  static bool getLastKnownPosition(double& latitude, double& longitude);

};

// Static member definitions
const char* NVSConfig::NVS_NAMESPACE = "wifi_config";
const char* NVSConfig::WIFI_SSID_KEY = "ssid";
const char* NVSConfig::WIFI_PASSWORD_KEY = "password";
const char* NVSConfig::GPS_ACTIVE_KEY = "gps_active";
const char* NVSConfig::FIRSTBOOT_KEY = "firstboot";
const char* NVSConfig::MAX_QUEUE_SIZE_KEY = "max_q_size";
const char* NVSConfig::QUEUE_DATA_KEY = "queue_data";
const char* NVSConfig::QUEUE_WRITE_PTR_KEY = "q_write_ptr";
const char* NVSConfig::QUEUE_READ_PTR_KEY = "q_read_ptr";
const char* NVSConfig::GPS_LAST_LAT_KEY = "gps_last_lat";
const char* NVSConfig::GPS_LAST_LON_KEY = "gps_last_lon";
const char* NVSConfig::GPS_LAST_ALT_KEY = "gps_last_alt";
const char* NVSConfig::GPS_LAST_FIX_KEY = "gps_last_fix";
const char* NVSConfig::GPS_LAST_SPEED_KEY = "gps_last_speed";
const char* NVSConfig::GPS_LAST_HEADING_KEY = "gps_last_heading";
const char* NVSConfig::GPS_LAST_SATELLITES_KEY = "gps_last_sat";
const char* NVSConfig::GPS_LAST_FIXTYPE_KEY = "gps_last_fixtype";
const char* NVSConfig::GPS_LAST_HDOP_KEY = "gps_last_hdop";
const char* NVSConfig::GPS_LAST_FIX_TIME_KEY = "gps_last_fix_time";
const char* NVSConfig::CYCLE_TIME_KEY = "cycle_time";
const char* NVSConfig::IMU_WRITE_PTR_KEY = "imu_write_ptr";
const char* NVSConfig::IMU_READ_PTR_KEY = "imu_read_ptr";
const char* NVSConfig::IMU_TOTAL_READINGS_KEY = "imu_total_readings";
const char* NVSConfig::CSV_WRITE_PTR_KEY = "csv_write_ptr";
const char* NVSConfig::CSV_READ_PTR_KEY = "csv_read_ptr";
const char* NVSConfig::GPS_ACCURACY_THRESHOLD_KEY = "gps_acc_thresh";
const char* NVSConfig::GPS_READ_CYCLE_TIME_KEY = "gps_read_cycle";
const char* NVSConfig::GPS_ON_AFTER_KEY = "gps_on_after";
const char* NVSConfig::INITIAL_POSITION_LAT_KEY = "init_pos_lat";
const char* NVSConfig::INITIAL_POSITION_LON_KEY = "init_pos_lon";
const char* NVSConfig::SCENARIO_STATE_KEY = "scenario_state";
const char* NVSConfig::LAST_KNOWN_POSITION_LAT_KEY = "last_known_lat";
const char* NVSConfig::LAST_KNOWN_POSITION_LON_KEY = "last_known_lon";

// GPS Accuracy Threshold implementation
float NVSConfig::getGPSAccuracyThreshold() {
  // Read as string to preserve float precision
  String thresholdStr = readStringNVS(GPS_ACCURACY_THRESHOLD_KEY);
  if (thresholdStr.length() == 0) {
    return 0.0; // No value set
  }
  return thresholdStr.toFloat();
}

bool NVSConfig::setGPSAccuracyThreshold(float threshold) {
  char thresholdStr[20];
  snprintf(thresholdStr, sizeof(thresholdStr), "%.2f", threshold);
  return writeStringNVS(GPS_ACCURACY_THRESHOLD_KEY, thresholdStr);
}

// GPS Read Cycle Time implementation
uint32_t NVSConfig::getGPSReadCycleTime() {
  return readU32NVS(GPS_READ_CYCLE_TIME_KEY, 0);
}

bool NVSConfig::setGPSReadCycleTime(uint32_t cycleTimeSeconds) {
  return writeU32NVS(GPS_READ_CYCLE_TIME_KEY, cycleTimeSeconds);
}

// GPS On After implementation
uint32_t NVSConfig::getGPSOnAfter() {
  return readU32NVS(GPS_ON_AFTER_KEY, 60); // Default: 60 seconds
}

bool NVSConfig::setGPSOnAfter(uint32_t seconds) {
  return writeU32NVS(GPS_ON_AFTER_KEY, seconds);
}

// Initial Position implementation
bool NVSConfig::setInitialPosition(double latitude, double longitude) {
  char latStr[20], lonStr[20];
  snprintf(latStr, sizeof(latStr), "%.7f", latitude);
  snprintf(lonStr, sizeof(lonStr), "%.7f", longitude);
  bool success = writeStringNVS(INITIAL_POSITION_LAT_KEY, latStr);
  success &= writeStringNVS(INITIAL_POSITION_LON_KEY, lonStr);
  return success;
}

bool NVSConfig::getInitialPosition(double& latitude, double& longitude) {
  String latStr = readStringNVS(INITIAL_POSITION_LAT_KEY);
  String lonStr = readStringNVS(INITIAL_POSITION_LON_KEY);
  
  if (latStr.length() == 0 || lonStr.length() == 0) {
    return false;
  }
  
  latitude = latStr.toFloat();
  longitude = lonStr.toFloat();
  return true;
}

bool NVSConfig::hasInitialPosition() {
  String latStr = readStringNVS(INITIAL_POSITION_LAT_KEY);
  String lonStr = readStringNVS(INITIAL_POSITION_LON_KEY);
  return (latStr.length() > 0 && lonStr.length() > 0);
}

bool NVSConfig::clearInitialPosition() {
  bool success = writeStringNVS(INITIAL_POSITION_LAT_KEY, "");
  success &= writeStringNVS(INITIAL_POSITION_LON_KEY, "");
  return success;
}

// Scenario State implementation
uint8_t NVSConfig::getScenarioState() {
  return readU8NVS(SCENARIO_STATE_KEY, 0);
}

bool NVSConfig::setScenarioState(uint8_t scenario) {
  return writeU8NVS(SCENARIO_STATE_KEY, scenario);
}

// Last Known Position implementation (for lat/long prefix)
bool NVSConfig::saveLastKnownPosition(double latitude, double longitude) {
  char latStr[20], lonStr[20];
  snprintf(latStr, sizeof(latStr), "%.7f", latitude);
  snprintf(lonStr, sizeof(lonStr), "%.7f", longitude);
  bool success = writeStringNVS(LAST_KNOWN_POSITION_LAT_KEY, latStr);
  success &= writeStringNVS(LAST_KNOWN_POSITION_LON_KEY, lonStr);
  return success;
}

bool NVSConfig::getLastKnownPosition(double& latitude, double& longitude) {
  String latStr = readStringNVS(LAST_KNOWN_POSITION_LAT_KEY);
  String lonStr = readStringNVS(LAST_KNOWN_POSITION_LON_KEY);
  
  if (latStr.length() == 0 || lonStr.length() == 0) {
    return false;
  }
  
  latitude = latStr.toFloat();
  longitude = lonStr.toFloat();
  return true;
}

#endif
