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
  
  // Convert decimal degrees to DDMM.MMMM format for NMEA
  String formatCoordinateToNMEA(double coord, bool isLatitude) {
    char dir = isLatitude ? (coord >= 0 ? 'N' : 'S') : (coord >= 0 ? 'E' : 'W');
    coord = fabs(coord);
    
    int degrees = (int)coord;
    double minutes = (coord - degrees) * 60.0;
    
    char buffer[20];
    if (isLatitude) {
      snprintf(buffer, sizeof(buffer), "%02d%07.4f", degrees, minutes);
    } else {
      snprintf(buffer, sizeof(buffer), "%03d%07.4f", degrees, minutes);
    }
    
    return String(buffer) + "," + String(dir);
  }
  
  // Calculate NMEA checksum
  uint8_t calculateNMEAChecksum(const char* sentence) {
    uint8_t checksum = 0;
    // Start after $, end before *
    for (int i = 1; sentence[i] != '\0' && sentence[i] != '*'; i++) {
      checksum ^= sentence[i];
    }
    return checksum;
  }
  
  // Send hot start command to GPS with last known position
  void sendHotStartCommand(double latitude, double longitude, double altitude) {
    // AT6558 position initialization command
    // Format: $PCAS10,lat,lon,alt*checksum
    // Coordinates in decimal degrees
    char cmd[100];
    snprintf(cmd, sizeof(cmd), "$PCAS10,%.7f,%.7f,%.2f", latitude, longitude, altitude);
    
    // Calculate checksum
    uint8_t checksum = calculateNMEAChecksum(cmd);
    char checksumStr[3];
    snprintf(checksumStr, sizeof(checksumStr), "%02X", checksum);
    
    // Send command
    Serial1.print(cmd);
    Serial1.print("*");
    Serial1.println(checksumStr);
    
    Serial.print("GPS Hot Start: Sending position (");
    Serial.print(latitude, 7);
    Serial.print(", ");
    Serial.print(longitude, 7);
    Serial.print(", ");
    Serial.print(altitude, 2);
    Serial.println(")");
    
    delay(100); // Give GPS time to process
  }
  
  // Convert DDMM.MMMM format to decimal degrees
  double formatCoordinate(String coord) {
    if (coord.length() < 4) return 0.0;
    
    int dotPos = coord.indexOf('.');
    if (dotPos < 2) return 0.0;
    
    String degrees = coord.substring(0, dotPos - 2);
    String minutes = coord.substring(dotPos - 2);
    
    return degrees.toFloat() + minutes.toFloat() / 60.0;
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
      
      // Save to NVS for hot start on next boot
      NVSConfig::saveLastGPSLocation(data.latitude, data.longitude, data.altitude);
      
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
    Serial1.println("$PCAS04,1*18"); // GPS only
    delay(100);
    Serial1.println("$PCAS01,5*19"); // 115200 baud
    delay(100);
    Serial1.println("$PCAS00*01"); // Save config
    delay(300);
    
    // Switch to 115200 baud
    Serial1.end();
    Serial1.begin(115200, SERIAL_8N1, GPS_TX_PIN, GPS_RX_PIN);
    
    Serial.println("Configuration complete!");
    
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
    
    // Load last known location from NVS and send hot start command
    double savedLat = 0.0, savedLon = 0.0, savedAlt = 0.0;
    if (NVSConfig::loadLastGPSLocation(savedLat, savedLon, savedAlt)) {
      Serial.println("Found last known GPS location in NVS");
      // Update lastKnownData with saved values
      lastKnownData.hasValidFix = true;
      lastKnownData.latitude = savedLat;
      lastKnownData.longitude = savedLon;
      lastKnownData.altitude = savedAlt;
      
      // Send hot start command to GPS module for faster fix
      sendHotStartCommand(savedLat, savedLon, savedAlt);
    } else {
      Serial.println("No saved GPS location found");
      lastKnownData = data;
    }
    
    Serial.println("Waiting for GPS fix...\n");
    configured = true;
    
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
  
  // Public method to send hot start command if last known location exists
  void sendHotStartIfAvailable() {
    // Only send hot start if we have last known location and don't currently have a fix
    if (lastKnownData.hasValidFix && !data.hasValidFix) {
      sendHotStartCommand(lastKnownData.latitude, lastKnownData.longitude, lastKnownData.altitude);
    }
  }
  
};

#endif
