// GPS Sensor Module - AT6558

#ifndef GPS_SENSOR_H
#define GPS_SENSOR_H

#include "nvs_config.h"
#include "time_sync.h"

// Scenario definitions
enum GPSScenario {
  SCENARIO_NONE,           // Initial state, no scenario determined yet
  SCENARIO_1_HIGH_ACCURACY, // High accuracy GPS fix found
  SCENARIO_2_CALCULATED,  // Calculated position from last known GPS fix
  SCENARIO_3_NO_FIX,        // No GPS fix found
  SCENARIO_4_GPS_OFF    // GPS is turned off  
};

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
  bool isHighAccuracy = false;
};

class GPSSensor {
private:
  GPSData data;
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
  
  // GPS accuracy detection function - checks if HDOP meets threshold
  bool isHighAccuracy(float hdop) {
    float accuracyThreshold = NVSConfig::getGPSAccuracyThreshold();

    if(hdop <= accuracyThreshold) {
      return true;
    } else {
      return false;
    }
  }

  // Parse GGA sentence: $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
  void parseGGA(String sentence) {
    int commaPos[15];
    int commaCount = 0;
    
    // Find comma positions
    for (int i = 0; i < sentence.length() && commaCount < 15; i++) {
        if (sentence.charAt(i) == ',') {
            commaPos[commaCount++] = i;
        }
    }
    
    if (commaCount < 14) return;
    
    // Extract Fix Quality (indicates if fix is valid)
    String fixQuality = sentence.substring(commaPos[5] + 1, commaPos[6]);
    
    if (fixQuality.toInt() > 0) {
        // Valid Fix
        data.hasValidFix = true;
        data.fixType = 3; // Assume 3D fix
        data.lastFixTimeMillis = TimeSync::getCurrentTimeMillis();

        // Latitude
        String lat = sentence.substring(commaPos[1] + 1, commaPos[2]);
        String latDir = sentence.substring(commaPos[2] + 1, commaPos[3]);
        
        // Longitude
        String lon = sentence.substring(commaPos[3] + 1, commaPos[4]);
        String lonDir = sentence.substring(commaPos[4] + 1, commaPos[5]);
        
        // Convert coordinates
        double latDeg = formatCoordinate(lat);
        double lonDeg = formatCoordinate(lon);
        
        // Apply direction (N/S, E/W)
        if (latDir == "S") latDeg = -latDeg;
        if (lonDir == "W") lonDeg = -lonDeg;
        
        data.latitude = latDeg;
        data.longitude = lonDeg;

        // Satellites
        String satStr = sentence.substring(commaPos[6] + 1, commaPos[7]);
        data.satellites = satStr.toInt();
        
        // HDOP
        String hdopStr = sentence.substring(commaPos[7] + 1, commaPos[8]);
        data.hdop = hdopStr.toFloat();
        
        // Altitude
        String altStr = sentence.substring(commaPos[8] + 1, commaPos[9]);
        data.altitude = altStr.toFloat();

        // Update accuracy flag
        data.isHighAccuracy = isHighAccuracy(data.hdop);

        // GGA doesn't provide Speed/Heading, so default them
        data.speed = 0.0;
        data.heading = 0.0;
        
        // Save complete GPS data to NVS for hot start on next boot
        NVSConfig::saveLastGPSData(data);
    } else {
        // NO FIX
        data.hasValidFix = false;
        data.fixType = 0;
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
    
    // 1. Detect which baud rate the GPS is CURRENTLY using
    Serial.println("\nDetecting GPS baud rate...");
    uint32_t detectedBaud = detectBaudRate();
    
    // Check if GPS device is responding
    if (detectedBaud == 0) {
      Serial.println("ERROR: GPS device not responding at any baud rate!");
      // Fallback: try default 9600 just in case
      detectedBaud = 9600;
    }
    
    // 2. Connect at the detected baud rate to allow sending commands
    Serial1.begin(detectedBaud, SERIAL_8N1, GPS_TX_PIN, GPS_RX_PIN);
    delay(200);
    
    // 3. Configure GPS (Disable RMC, Enable GGA)
    // We send this AT THE CURRENT BAUD RATE so the GPS understands it
    Serial.println("Configuring GPS sentences (GGA Only)...");
    
    // CAS03: 1=Open, 0=Close. Order: GGA,GLL,GSA,GSV,RMC,VTG,ZDA,ANT
    Serial1.println("$PCAS03,1,0,0,0,0,0,0,0*03"); 
    delay(200);
    
    // CAS04: Set System ID (1=GPS Only)
    Serial1.println("$PCAS04,1*18"); 
    delay(200);

    // 4. Ensure we are running at 115200 baud
    if (detectedBaud != 115200) {
      Serial.println("Switching GPS to 115200 baud...");
      Serial1.println("$PCAS01,5*19"); // Set GPS to 115200
      delay(200);
      
      // Re-connect ESP Serial at new baud rate
      Serial1.end();
      delay(100);
      Serial1.begin(115200, SERIAL_8N1, GPS_TX_PIN, GPS_RX_PIN);
      detectedBaud = 115200;
      delay(200);
    }
    
    // 5. Save Configuration
    Serial1.println("$PCAS00*01"); 
    delay(300);
    
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
    data.isHighAccuracy = false;
    
    // Load last known GPS data from NVS purely for hot start command
    GPSData savedData;
    if (NVSConfig::loadLastGPSData(savedData)) {
      Serial.println("Found saved GPS data in NVS, attempting hot start");
      // Send hot start command to GPS module for faster fix
      sendHotStartCommand(savedData.latitude, savedData.longitude, savedData.altitude);
    } else {
      Serial.println("No saved GPS data found");
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
        Serial.print("Received GPS Sentence: ");
        Serial.println(nmeaSentence);
        // Complete sentence received
        if (nmeaSentence.startsWith("$GPGGA") || nmeaSentence.startsWith("$GNGGA")) {
          parseGGA(nmeaSentence);
        }
        nmeaSentence = "";
      } else if (c != '\r') {
        nmeaSentence += c;
      }
    }
  }
  
  GPSData getGPSData() {
    update();
    return data;
  }

};

#endif
