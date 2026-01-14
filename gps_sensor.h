// GPS Sensor Module - AT6558

#ifndef GPS_SENSOR_H
#define GPS_SENSOR_H

#include "nvs_config.h"
#include "time_sync.h"

// GPS pin definitions
#define GPS_TX_PIN 17     // GPS_TX connects to ESP32 RX (RXD0)
#define GPS_RX_PIN 16     // GPS_RX connects to ESP32 TX (TXD0)
// GPS_POWER_PIN - Define pin number here for GPS power control
#define GPS_POWER_PIN 19

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
  unsigned long long lastFixTimeMillis = 0;  // Timestamp (ms since epoch or millis) when last valid fix was recorded
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
  
  // Detect which baud rate is working (9600 or 115200)
  // Returns the baud rate that receives valid NMEA data, or 0 if neither works
  uint32_t detectBaudRate() {
    uint32_t baudRates[] = {4800,9600,19200,38400,57600,115200};
    const int detectionTimeout = 1000; // 1 second to detect data
    const int minValidChars = 10; // Minimum characters to consider valid data
    
    bool foundValidData = false;

    for (int i = 0; i < 6; i++) {
      uint32_t testBaud = baudRates[i];
      Serial.print("Testing baud rate: ");
      Serial.println(testBaud);
      
      // End current serial connection
      Serial1.end();
      delay(100);
      
      // Start at test baud rate
      Serial1.begin(testBaud, SERIAL_8N1, GPS_TX_PIN, GPS_RX_PIN);
      delay(200); // Give serial time to initialize
      
      // Clear any existing data
      while (Serial1.available() > 0) {
        Serial1.read();
      }
      
      // Try to receive data for detection timeout
      unsigned long long startTime = TimeSync::getCurrentTimeMillis();
      String testSentence = "";
      
      while (TimeSync::getCurrentTimeMillis() - startTime < detectionTimeout) {
        if (Serial1.available() > 0) {
          char c = Serial1.read();
          
          // Check for NMEA sentence start
          if (c == '$') {
            testSentence = "$";
            foundValidData = true;
          } else if (foundValidData && c != '\r' && c != '\n') {
            testSentence += c;
            
            // If we have enough characters and it looks like NMEA, this baud rate works
            if (testSentence.length() >= minValidChars) {
              // Check if it's a valid NMEA sentence (contains common NMEA identifiers)
              if (testSentence.indexOf("GPRMC") >= 0 || 
                  testSentence.indexOf("GNRMC") >= 0 ||
                  testSentence.indexOf("GPGGA") >= 0 ||
                  testSentence.indexOf("GNGGA") >= 0) {
                Serial.print("Valid NMEA data detected at ");
                Serial.print(testBaud);
                Serial.println(" baud");
                foundValidData = true;
                return testBaud;
              }
            }
          } else if (c == '\n') {
            // End of sentence, reset
            testSentence = "";
            foundValidData = false;
          }
        }
        delay(10);
      }
      
      Serial.print("No valid data at ");
      Serial.print(testBaud);
      Serial.println(" baud");
    }
    
    Serial.println("Warning: Could not detect valid baud rate, defaulting to 115200");
    foundValidData = false;
    return 0; // Default to 0 if detection fails
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
      data.lastFixTimeMillis = TimeSync::getCurrentTimeMillis();  // Record timestamp when fix was received
      
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
      
      // Save complete GPS data to NVS for hot start on next boot
      NVSConfig::saveLastGPSData(data);
      
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
        data.lastFixTimeMillis = lastKnownData.lastFixTimeMillis;  // Preserve timestamp of last valid fix
      }
    }
  }

public:
  // GPS power control functions
  static void powerOn() {
    // GPS_POWER_PIN must be defined with a pin number (e.g., #define GPS_POWER_PIN D3)
    // If defined as empty or 0, power control is disabled
    // Note: LOW = GPS ON, HIGH = GPS OFF (inverted logic)
    if (GPS_POWER_PIN > 0) {
      pinMode(GPS_POWER_PIN, OUTPUT);
      digitalWrite(GPS_POWER_PIN, LOW);
      delay(500); // Give GPS time to power up
      Serial.print("GPS power turned ON (pin ");
      Serial.print(GPS_POWER_PIN);
      Serial.println(" set to LOW)");

      if(!NVSConfig::getGPSActive()) {
        NVSConfig::setGPSActive(1);
      }
    } else {
      Serial.println("GPS_POWER_PIN not configured (0 or not defined) - GPS power control disabled");
    }
    
  }
  
  static void powerOff() {
    if (GPS_POWER_PIN > 0) {
      pinMode(GPS_POWER_PIN, OUTPUT);
      digitalWrite(GPS_POWER_PIN, HIGH);
      Serial.print("GPS power turned OFF (pin ");
      Serial.print(GPS_POWER_PIN);
      Serial.println(" set to HIGH)");

      if(NVSConfig::getGPSActive()) {
        NVSConfig::setGPSActive(0);
      }
    } else {
      Serial.println("GPS_POWER_PIN not configured (0 or not defined) - GPS power control disabled");
    }
  }
  
  bool begin() {
    Serial.println("=== AT6558 GPS Configuration ===");
    Serial.print("GPS TX (to ESP RX): ");
    Serial.println(GPS_TX_PIN);
    Serial.print("GPS RX (to ESP TX): ");
    Serial.println(GPS_RX_PIN);
    #if GPS_POWER_PIN > 0
      Serial.print("GPS Power Pin: ");
      Serial.println(GPS_POWER_PIN);
    #else
      Serial.println("GPS Power Pin: Not configured");
    #endif
    
    // Power on GPS first
    powerOn();
    
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
    // delay(100);
    // Serial1.println("$PCAS04,2*3F\r\n"); // low power mode
    delay(100);
    Serial1.println("$PCAS00*01"); // Save config
    delay(300);
    
    // Detect which baud rate is working (9600 or 115200)
    Serial.println("\nDetecting GPS baud rate...");
    uint32_t detectedBaud = detectBaudRate();
    
    // Check if GPS device is responding
    if (detectedBaud == 0) {
      Serial.println("ERROR: GPS device not responding at any baud rate!");
      Serial.println("GPS initialization failed - device may not be working properly");
      // powerOff(); // Turn off GPS to save power
      configured = false;
      return false;
    }
    
    // Switch to detected baud rate
    Serial1.end();
    delay(100);
    Serial1.begin(detectedBaud, SERIAL_8N1, GPS_TX_PIN, GPS_RX_PIN);
    delay(200);
    
    Serial.print("Using baud rate: ");
    Serial.println(detectedBaud);
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
    data.lastFixTimeMillis = 0;
    
    // Load last known GPS data from NVS and send hot start command
    if (NVSConfig::loadLastGPSData(lastKnownData)) {
      Serial.println("Found last known GPS data in NVS");
      // Send hot start command to GPS module for faster fix (only needs position)
      sendHotStartCommand(lastKnownData.latitude, lastKnownData.longitude, lastKnownData.altitude);
    } else {
      Serial.println("No saved GPS data found");
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
      data.lastFixTimeMillis = lastKnownData.lastFixTimeMillis;  // Preserve timestamp of last valid fix
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
  
  // GPS accuracy detection function (placeholder - returns manual true/false)
  // TODO: Implement actual accuracy threshold checking based on HDOP
  bool isHighAccuracy() {
    // Empty function - manual true/false return for now
    // Will be implemented with HDOP threshold checking later
    return true; // Placeholder: return true for high accuracy
  }

};

#endif
