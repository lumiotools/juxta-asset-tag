#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <WiFi.h>
#include <time.h>

class TimeSync {
public:
  // Check if system time has been synced (time > Jan 1, 1970)
  static bool isTimeSynced() {
    time_t now = time(nullptr);
    return (now > 24 * 3600); // More than 24 hours after epoch (Jan 1, 1970)
  }
  
  // Synchronize system time with NTP server (simplified)
  static bool syncTimeNTP() {
    configTime(0, 0, "pool.ntp.org");
    time_t now = time(nullptr);
    int attempts = 0;
    while (now < 24 * 3600 && attempts < 15) {
      delay(500);
      now = time(nullptr);
      attempts++;
    }
    return (now > 24 * 3600);
  }
  
  // Get current time as formatted string (yyyy:mm:dd hh:mm:ss)
  static void getCurrentTimeString(char* buffer, size_t bufSize) {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    snprintf(buffer, bufSize, "%04d:%02d:%02d %02d:%02d:%02d",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  }
};

#endif
