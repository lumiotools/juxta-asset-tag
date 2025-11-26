// GPS Sensor Module - NEO-M9N

#ifndef GPS_SENSOR_H
#define GPS_SENSOR_H

#include <TinyGPSPlus.h>

// GPS pin definitions
#define GPS_TX_PIN 17      // GPS_TX
#define GPS_RX_PIN 18      // GPS_RX

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

public:
  bool begin() {
    // Initialize GPS UART (TinyGPS uses 9600 baud by default for NMEA)
    Serial2.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    
    delay(500);
    
    return true; // TinyGPS doesn't need explicit initialization
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
