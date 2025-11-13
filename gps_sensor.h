/*
 * GPS Sensor Module - NEO-M9N
 * Handles all GPS sensor initialization and data reading
 */

#ifndef GPS_SENSOR_H
#define GPS_SENSOR_H

#include <SparkFun_u-blox_GNSS_v3.h>

// GPS pin definitions
#define GPS_TX_PIN 17      // GPS_TX
#define GPS_RX_PIN 18      // GPS_RX
#define GPS_RESET_PIN 15   // GPS_RESET
#define GPS_INT_PIN 16     // GPS_INT

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
  SFE_UBLOX_GNSS gps;
  GPSData data;

public:
  bool begin() {
    // Initialize GPS UART
    Serial2.begin(38400, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    
    // Initialize GPS Reset pin
    pinMode(GPS_RESET_PIN, OUTPUT);
    digitalWrite(GPS_RESET_PIN, HIGH);
    
    delay(100);
    
    // Initialize GPS
    Serial.println("Initializing NEO-M9N GPS...");
    
    if (gps.begin(Serial2) == true) {
      Serial.println("GPS found!");
      gps.setUART1Output(COM_TYPE_UBX); // Set UART output to UBX only
      gps.setI2COutput(COM_TYPE_UBX);
      gps.saveConfigSelective(VAL_CFG_SUBSEC_IOPORT); // Save config
      return true;
    } else {
      Serial.println("GPS not detected! Check wiring.");
      return false;
    }
  }
  
  void update() {
    // Read GPS data
    if (gps.getPVT()) {
      data.latitude = gps.getLatitude() / 10000000.0;
      data.longitude = gps.getLongitude() / 10000000.0;
      data.altitude = gps.getAltitude() / 1000.0;
      data.speed = gps.getGroundSpeed() / 1000.0;
      data.heading = gps.getHeading() / 100000.0;
      data.satellites = gps.getSIV();
      data.fixType = gps.getFixType();
      data.hdop = gps.getHorizontalDOP() / 100.0;
      data.timestamp = millis();
      data.hasValidFix = (data.fixType >= 2);
    }
  }
  
  GPSData getGPSData() {
    return data;
  }
};

#endif
