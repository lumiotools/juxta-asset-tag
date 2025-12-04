// GPS Sensor Module - AT6558

#ifndef GPS_SENSOR_H
#define GPS_SENSOR_H

#include "time_sync.h"
#include "nvs_config.h"

// GPS pin definitions
#define GPS_TX_PIN D7      // GPS_TX connects to ESP32 RX
#define GPS_RX_PIN D6      // GPS_RX connects to ESP32 TX

// Structure to hold GPS data
struct GPSData {
  double latitude;
  double longitude;
  double altitude;
  float speed;
  float heading;
  int satellites;
  int fixType;  // 0=no fix, 2=2D, 3=3D
  float hdop;
  char timestamp[20];  // Formatted time string (yyyy:mm:dd hh:mm:ss)
  bool hasValidFix = false;
};

class GPSSensor {
private:
  GPSData data;
  GPSData lastKnownData;  // Store last known values when fix is lost
  String nmeaSentence = "";
  bool configured = false;
  
  // Convert DDMM.MMMM format to decimal degrees
  double formatCoordinate(String coord) {
    if (coord.length() < 4) return 0.0;
    
    int dotPos = coord.indexOf('.');
    if (dotPos < 2) return 0.0;
    
    String degrees = coord.substring(0, dotPos - 2);
    String minutes = coord.substring(dotPos - 2);
    
    return degrees.toFloat() + minutes.toFloat() / 60.0;
  }
  
  // Convert decimal degrees to DDMM.MMMM format (NMEA format)
  String formatCoordinateToNMEA(double coord, bool isLatitude) {
    char dir = (coord >= 0) ? (isLatitude ? 'N' : 'E') : (isLatitude ? 'S' : 'W');
    coord = abs(coord);
    
    int degrees = (int)coord;
    double minutes = (coord - degrees) * 60.0;
    
    char buffer[16];
    if (isLatitude) {
      snprintf(buffer, sizeof(buffer), "%02d%07.4f", degrees, minutes);
    } else {
      snprintf(buffer, sizeof(buffer), "%03d%07.4f", degrees, minutes);
    }
    
    return String(buffer);
  }
  
  // Calculate NMEA checksum (XOR of all characters between $ and *)
  uint8_t calculateNMEAChecksum(String sentence) {
    uint8_t checksum = 0;
    int start = sentence.indexOf('$');
    int end = sentence.indexOf('*');
    
    if (start < 0 || end < 0) return 0;
    
    for (int i = start + 1; i < end; i++) {
      checksum ^= sentence.charAt(i);
    }
    
    return checksum;
  }
  
  // Parse RMC sentence: $GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
  void parseRMC(String sentence) {
    int commaPos[12];
    int commaCount = 0;
    
    // Find comma positions
    for (int i = 0; i < sentence.length() && commaCount < 12; i++) {
      if (sentence.charAt(i) == ',') {
        commaPos[commaCount++] = i;
      }
    }
    
    if (commaCount < 9) return;
    
    // Extract status (A = valid, V = invalid)
    String status = sentence.substring(commaPos[1] + 1, commaPos[2]);
    
    if (status == "A") {
      // GPS HAS FIX
      data.hasValidFix = true;
      data.fixType = 3;  // Assume 3D fix when status is A
      
      // Extract position data
      String lat = sentence.substring(commaPos[2] + 1, commaPos[3]);
      String latDir = sentence.substring(commaPos[3] + 1, commaPos[4]);
      String lon = sentence.substring(commaPos[4] + 1, commaPos[5]);
      String lonDir = sentence.substring(commaPos[5] + 1, commaPos[6]);
      String speedStr = sentence.substring(commaPos[6] + 1, commaPos[7]);
      String courseStr = sentence.substring(commaPos[7] + 1, commaPos[8]);
      
      // Convert coordinates
      double latDeg = formatCoordinate(lat);
      double lonDeg = formatCoordinate(lon);
      
      // Apply direction (N/S, E/W)
      if (latDir == "S") latDeg = -latDeg;
      if (lonDir == "W") lonDeg = -lonDeg;
      
      data.latitude = latDeg;
      data.longitude = lonDeg;
      
      // Speed in knots to m/s
      data.speed = speedStr.toFloat() * 0.514444;  // knots to m/s
      
      // Course/heading
      data.heading = courseStr.toFloat();
      
      // Update timestamp
      TimeSync::getCurrentTimeString(data.timestamp, sizeof(data.timestamp));
      
      // RMC doesn't provide altitude, satellites, or HDOP
      // Keep last known values if available, otherwise keep defaults
      if (lastKnownData.hasValidFix) {
        data.altitude = lastKnownData.altitude;
        data.satellites = lastKnownData.satellites;
        data.hdop = lastKnownData.hdop;
      } else {
        // Defaults for first fix
        data.altitude = 0.0;
        data.satellites = 0;
        data.hdop = 0.0;
      }
      
      // Store as last known values
      lastKnownData = data;
      
    } else {
      // NO FIX - use last known values
      data.hasValidFix = false;
      data.fixType = 0;
      
      // Restore last known values if available
      if (lastKnownData.hasValidFix) {
        data.latitude = lastKnownData.latitude;
        data.longitude = lastKnownData.longitude;
        data.altitude = lastKnownData.altitude;
        data.speed = lastKnownData.speed;
        data.heading = lastKnownData.heading;
        data.satellites = lastKnownData.satellites;
        data.hdop = lastKnownData.hdop;
        // Keep current timestamp to indicate this is stale data
        TimeSync::getCurrentTimeString(data.timestamp, sizeof(data.timestamp));
      }
    }
  }

public:
  bool begin() {
    Serial.println("=== AT6558 GPS Configuration ===");
    Serial.print("GPS TX (to ESP RX): ");
    Serial.println(GPS_TX_PIN);
    Serial.print("GPS RX (to ESP TX): ");
    Serial.println(GPS_RX_PIN);
    
    // Initialize Serial1 at 9600 baud for configuration
    Serial1.begin(9600, SERIAL_8N1, GPS_TX_PIN, GPS_RX_PIN);
    delay(500); // Give GPS time to power up
    
    // Configure GPS for fastest operation
    Serial.println("\nConfiguring GPS...");
    Serial1.println("$PCAS03,0,0,0,0,1,0,0,0*03"); // RMC only
    delay(100);
    // Switch to GPS + GLONASS (Recommended for USA)
    Serial1.println("$PCAS04,5*1C");
    delay(100);
    Serial1.println("$PCAS01,5*19"); // 115200 baud
    delay(100);
    Serial1.println("$PCAS00*01"); // Save config
    delay(300);
    
    // Switch to 115200 baud
    Serial1.end();
    Serial1.begin(115200, SERIAL_8N1, GPS_TX_PIN, GPS_RX_PIN);
    
    Serial.println("Configuration complete!");
    Serial.println("Waiting for GPS fix...\n");
    configured = true;
    
    // Initialize data structures
    data.hasValidFix = false;
    data.fixType = 0;
    data.latitude = 0.0;
    data.longitude = 0.0;
    data.altitude = 0.0;
    data.speed = 0.0;
    data.heading = 0.0;
    data.satellites = 0;
    data.hdop = 0.0;
    TimeSync::getCurrentTimeString(data.timestamp, sizeof(data.timestamp));
    
    // Try to restore last known location from NVS (persistent across deep sleep)
    if (restoreLastKnownLocation()) {
      Serial.println("Last known GPS location restored from NVS");
    } else {
      // No saved location, initialize to defaults
      lastKnownData = data;
    }
    
    return true;
  }
  
  void update() {
    // Read NMEA sentences from GPS
    while (Serial1.available() > 0) {
      char c = Serial1.read();
      
      if (c == '\n') {
        // Complete sentence received
        if (nmeaSentence.startsWith("$GPRMC") || nmeaSentence.startsWith("$GNRMC")) {
          parseRMC(nmeaSentence);
        }
        nmeaSentence = "";
      } else if (c != '\r') {
        nmeaSentence += c;
      }
    }
    
    // If no data available and we don't have a fix, use last known values
    if (!Serial1.available() && !data.hasValidFix && lastKnownData.hasValidFix) {
      data.latitude = lastKnownData.latitude;
      data.longitude = lastKnownData.longitude;
      data.altitude = lastKnownData.altitude;
      data.speed = lastKnownData.speed;
      data.heading = lastKnownData.heading;
      data.satellites = lastKnownData.satellites;
      data.hdop = lastKnownData.hdop;
      TimeSync::getCurrentTimeString(data.timestamp, sizeof(data.timestamp));
      // hasValidFix remains false to indicate this is stale data
    }
  }
  
  GPSData getGPSData() {
    return data;
  }
  
  // Save location to GPS module flash memory using PCAS command
  // This pushes the last known location to the GPS module's internal flash
  bool saveLocationToGPSModule() {
    if (!lastKnownData.hasValidFix || !configured) {
      return false;
    }
    
    // Convert coordinates to NMEA format (DDMM.MMMM)
    String latNMEA = formatCoordinateToNMEA(lastKnownData.latitude, true);
    String lonNMEA = formatCoordinateToNMEA(lastKnownData.longitude, false);
    
    // Determine direction
    char latDir = (lastKnownData.latitude >= 0) ? 'N' : 'S';
    char lonDir = (lastKnownData.longitude >= 0) ? 'E' : 'W';
    
    // AT6558 PCAS command to set initial position for faster acquisition
    // Format: $PCAS11,lat,lat_dir,lon,lon_dir*checksum
    // This helps with faster Time To First Fix (TTFF) and stores position in module flash
    String command = "$PCAS11,";
    command += latNMEA;
    command += ",";
    command += latDir;
    command += ",";
    command += lonNMEA;
    command += ",";
    command += lonDir;
    
    // Calculate and append checksum
    uint8_t checksum = calculateNMEAChecksum(command);
    char checksumStr[3];
    snprintf(checksumStr, sizeof(checksumStr), "%02X", checksum);
    command += "*";
    command += checksumStr;
    
    // Send command to GPS module
    Serial1.println(command);
    delay(100);
    
    // Save configuration to flash
    Serial1.println("$PCAS00*01"); // Save config to flash
    delay(200);
    
    Serial.print("Location saved to GPS module flash: ");
    Serial.print(lastKnownData.latitude, 7);
    Serial.print(", ");
    Serial.println(lastKnownData.longitude, 7);
    
    return true;
  }
  
  // Save last known location to NVS (call before deep sleep)
  bool saveLastKnownLocation() {
    if (lastKnownData.hasValidFix) {
      return NVSConfig::saveLastKnownGPS(
        lastKnownData.latitude,
        lastKnownData.longitude,
        lastKnownData.altitude,
        lastKnownData.speed,
        lastKnownData.heading,
        lastKnownData.satellites,
        lastKnownData.hdop,
        lastKnownData.hasValidFix
      );
    }
    return false; // No valid location to save
  }
  
  // Restore last known location from NVS (call after wake from deep sleep)
  bool restoreLastKnownLocation() {
    double lat, lon, alt;
    float speed, heading, hdop;
    int satellites;
    bool hasFix;
    
    if (NVSConfig::getLastKnownGPS(&lat, &lon, &alt, &speed, &heading, &satellites, &hdop, &hasFix)) {
      lastKnownData.latitude = lat;
      lastKnownData.longitude = lon;
      lastKnownData.altitude = alt;
      lastKnownData.speed = speed;
      lastKnownData.heading = heading;
      lastKnownData.satellites = satellites;
      lastKnownData.hdop = hdop;
      lastKnownData.hasValidFix = hasFix;
      TimeSync::getCurrentTimeString(lastKnownData.timestamp, sizeof(lastKnownData.timestamp));
      
      // Also update current data if we don't have a fix yet
      if (!data.hasValidFix) {
        data.latitude = lat;
        data.longitude = lon;
        data.altitude = alt;
        data.speed = speed;
        data.heading = heading;
        data.satellites = satellites;
        data.hdop = hdop;
        data.hasValidFix = false; // Mark as stale
        TimeSync::getCurrentTimeString(data.timestamp, sizeof(data.timestamp));
      }
      
      return true;
    }
    return false; // No saved location found
  }
  
};

#endif
