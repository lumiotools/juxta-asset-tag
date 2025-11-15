#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <WiFi.h>
#include <time.h>

class TimeSync {
public:
  // Synchronize system time with NTP server
  static bool syncTimeNTP() {
    Serial.println("Syncing time with NTP server...");
    
    // Configure time with NTP server
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    
    Serial.print("Waiting for NTP time sync: ");
    time_t now = time(nullptr);
    int attempts = 0;
    
    // Wait for time to be set (max 20 attempts, ~10 seconds)
    while (now < 24 * 3600 && attempts < 20) {
      delay(500);
      Serial.print(".");
      now = time(nullptr);
      attempts++;
    }
    
    Serial.println();
    
    if (now > 24 * 3600) {
      time_t now = time(nullptr);
      Serial.print("Time synced! Current time: ");
      Serial.println(ctime(&now));
      return true;
    } else {
      Serial.println("ERROR: Failed to sync time with NTP server");
      return false;
    }
  }
  
  // Get current time as formatted string
  static String getCurrentTimeString() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char buffer[30];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    return String(buffer);
  }
  
  // Get current Unix timestamp
  static long getCurrentTimestamp() {
    return time(nullptr);
  }
};

#endif
