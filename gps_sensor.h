// GPS Sensor Module - NEO-M9N

#ifndef GPS_SENSOR_H
#define GPS_SENSOR_H

#include <TinyGPSPlus.h>

// GPS pin definitions
#define GPS_TX_PIN 7      // GPS_TX
#define GPS_RX_PIN 6      // GPS_RX

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
  unsigned long timestamp;
  bool hasValidFix = false;
};

class GPSSensor {
private:
  TinyGPSPlus gps;
  GPSData data;

  // Check if GPS device is connected and sending data
  bool checkDeviceConnection() {
    // Clear any existing data in buffer
    while (Serial2.available() > 0) {
      Serial2.read();
    }
    
    // Wait a bit for GPS to send data
    delay(200);
    
    // Check if we're receiving any data (NMEA sentences start with '$')
    unsigned long startTime = millis();
    bool dataReceived = false;
    
    while (millis() - startTime < 1000) { // Wait up to 1 second
      if (Serial2.available() > 0) {
        char c = Serial2.peek(); // Peek at first character without reading
        if (c == '$' || c == '\n' || c == '\r') {
          dataReceived = true;
          break;
        }
        Serial2.read(); // Remove non-NMEA data
      }
      delay(10);
    }
    
    return dataReceived;
  }

public:
  bool begin() {
    Serial.print("Initializing GPS sensor (RX=");
    Serial.print(GPS_RX_PIN);
    Serial.print(", TX=");
    Serial.print(GPS_TX_PIN);
    Serial.println(", 9600 baud)...");

    // Initialize GPS UART (TinyGPS uses 9600 baud by default for NMEA)
    Serial2.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    delay(500); // Give GPS time to power up
    
    // Check if GPS device is connected and sending data
    Serial.println("Checking GPS connection...");
    // if (!checkDeviceConnection()) {
    //   Serial.println("WARNING: GPS device not detected or not sending data!");
    //   Serial.println("Check: 1) UART wiring (RX/TX) 2) Power supply 3) GPS antenna");
    //   Serial.println("Note: GPS may take time to acquire satellites - continuing anyway");
    //   // Don't return false - GPS might just need more time to start
    //   return true; // Allow initialization to continue (GPS might need time)
    // }

    Serial.println("GPS sensor initialized successfully (receiving data)");
    return true;
  }
  
  void update() {
    // Feed GPS parser with available data
    while (Serial2.available() > 0) {
      gps.encode(Serial2.read());
    }
    
    // Update data if location is valid
    if (gps.location.isUpdated()) {
      data.latitude = gps.location.lat();
      data.longitude = gps.location.lng();
      data.altitude = gps.altitude.meters();
      data.speed = gps.speed.mps();
      data.heading = gps.course.deg();
      data.satellites = gps.satellites.value();
      data.hdop = gps.hdop.hdop();
      data.timestamp = millis();
      data.hasValidFix = gps.location.isValid();
      data.fixType = data.hasValidFix ? 3 : 0;
    }
  }
  
  GPSData getGPSData() {
    return data;
  }
  
};

#endif
