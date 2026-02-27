#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <WiFi.h>
#include <time.h>
#include <sys/time.h>

// Include esp_sntp.h if available (ESP-IDF v4.1+, Arduino ESP32 v2.0+)
#if __has_include(<esp_sntp.h>)
  #include <esp_sntp.h>
  #define HAS_ESP_SNTP 1
#else
  #define HAS_ESP_SNTP 0
#endif

class TimeSync {
private:
  static bool _timeSyncReceived;
  static unsigned long long _syncTimeMillis;  // Unix timestamp in ms when sync happened
  static unsigned long _syncMillis;            // millis() value when sync happened
  static bool _hasSyncOffset;                  // Whether we have a valid sync offset
  
  // Callback function (gets called when time adjusts via NTP)
  static void onTimeSyncCallback(struct timeval *t) {
    Serial.println("NTP time sync notification received");
    _timeSyncReceived = true;
  }

public:
  // Initialize NTP sync (call once after WiFi connects)
  static void begin() {
    // Set notification callback
    sntp_set_time_sync_notification_cb(onTimeSyncCallback);
    
    #if HAS_ESP_SNTP
    // Enable DHCP NTP server mode (optional)
    esp_sntp_servermode_dhcp(1);
    #endif
    
    // Configure NTP with UTC (gmtOffset=0, daylightOffset=0)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("NTP sync initiated");
  }
  
  // Synchronize system time with NTP server
  static bool syncTimeNTP() {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi not connected, cannot sync time");
      return false;
    }

    _timeSyncReceived = false;
    begin();
    
    // Wait for time synchronization (with timeout)
    time_t now = time(nullptr);
    int attempts = 0;
    while (now < 24 * 3600 && attempts < 15) {
      delay(500);
      now = time(nullptr);
      attempts++;
    }
    
    bool success = (now > 24 * 3600);
    if (success) {
      // Store sync time offset for accurate timestamps even after WiFi disconnect
      struct timeval tv;
      if (gettimeofday(&tv, nullptr) == 0) {
        _syncTimeMillis = (unsigned long long)tv.tv_sec * 1000ULL + (unsigned long long)tv.tv_usec / 1000ULL;
        _syncMillis = millis();
        _hasSyncOffset = true;
        Serial.print("NTP sync successful - UTC time: ");
        printLocalTime();
        Serial.print("Sync offset stored: Unix=");
        Serial.print(_syncTimeMillis);
        Serial.print("ms, millis()=");
        Serial.println(_syncMillis);
      } else {
        // Fallback if gettimeofday fails
        _syncTimeMillis = (unsigned long long)now * 1000ULL;
        _syncMillis = millis();
        _hasSyncOffset = true;
        Serial.print("NTP sync successful (fallback) - UTC time: ");
        printLocalTime();
      }
    } else {
      Serial.println("NTP sync failed - timeout");
    }
    return success;
  }
  
  // Print UTC time in human-readable format
  static void printLocalTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      Serial.println("No time available");
      return;
    }
    Serial.println(&timeinfo, "%Y-%m-%d %H:%M:%S UTC");
  }
  
  // Get current UTC time as formatted string (yyyy:mm:dd hh:mm:ss)
  static void getCurrentTimeString(char* buffer, size_t bufSize) {
    time_t now = time(nullptr);
    struct tm* timeinfo = gmtime(&now);  // Use gmtime for UTC
    snprintf(buffer, bufSize, "%04d:%02d:%02d %02d:%02d:%02d",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  }
  
  // Check if time has been synchronized
  static bool isTimeSynced() {
    time_t now = time(nullptr);
    return (now > 24 * 3600);
  }
  
  // Get Unix timestamp
  static time_t getUnixTime() {
    return time(nullptr);
  }
  
  // Get Unix timestamp in milliseconds (synced time)
  // Returns milliseconds since epoch if time is synced, otherwise returns 0
  // Uses gettimeofday() for accurate millisecond precision
  static unsigned long long getUnixTimeMillis() {
    if (!isTimeSynced()) {
      return 0; // Return 0 if time not synced
    }
    struct timeval tv;
    if (gettimeofday(&tv, nullptr) == 0) {
      // Convert seconds and microseconds to milliseconds
      return (unsigned long long)tv.tv_sec * 1000ULL + (unsigned long long)tv.tv_usec / 1000ULL;
    }
    // Fallback to seconds * 1000 if gettimeofday fails
    time_t now = time(nullptr);
    return (unsigned long long)now * 1000ULL;
  }
  
  // Get current timestamp in milliseconds (prefers synced NTP time, falls back to millis)
  // Uses stored sync offset to maintain accurate timestamps even after WiFi disconnect
  static unsigned long long getCurrentTimeMillis() {
    // First, try to use system time if synced
    if (isTimeSynced()) {
      unsigned long long unixTime = getUnixTimeMillis();
      if (unixTime > 0) {
        // Update sync offset if system time is available
        _syncTimeMillis = unixTime;
        _syncMillis = millis();
        _hasSyncOffset = true;
        return unixTime;
      }
    }
    
    // If we have a stored sync offset, use it with millis() for accurate timestamps
    // This ensures timestamps remain correct even after WiFi disconnects
    if (_hasSyncOffset) {
      unsigned long currentMillis = millis();
      // Handle millis() overflow (happens every ~49 days)
      unsigned long elapsed = (currentMillis >= _syncMillis) 
                            ? (currentMillis - _syncMillis)
                            : (ULONG_MAX - _syncMillis + currentMillis + 1);
      return _syncTimeMillis + (unsigned long long)elapsed;
    }
    
    // Last resort: fallback to millis() if no sync has ever happened
    return (unsigned long long)millis();
  }
};

// Static member initialization
bool TimeSync::_timeSyncReceived = false;
unsigned long long TimeSync::_syncTimeMillis = 0;
unsigned long TimeSync::_syncMillis = 0;
bool TimeSync::_hasSyncOffset = false;

#endif
