// GPS Scenario Handler - Manages 4 GPS scenarios based on process flow v2.4

#ifndef GPS_SCENARIO_HANDLER_H
#define GPS_SCENARIO_HANDLER_H

#include "gps_sensor.h"
#include "nvs_config.h"
#include "time_sync.h"

// GPS Scenario States
enum GPSScenario {
  SCENARIO_NONE,           // Initial state, no scenario determined yet
  SCENARIO_1_HIGH_ACCURACY, // High accuracy GPS fix found
  SCENARIO_2_LOW_ACCURACY,  // Low accuracy GPS fix found
  SCENARIO_3_NO_FIX,        // No GPS fix found
  SCENARIO_4_UI_POSITION    // Initial position provided from UI
};

class GPSScenarioHandler {
private:
  GPSSensor* gps;
  GPSScenario currentScenario;
  unsigned long long gpsFixStartTime;  // Time when GPS fix acquisition started
  unsigned long long gpsFixEndTime;  // Time when GPS was turned off (for continuous cycle in Scenario 2)
  unsigned long long lastTransmissionTime;  // Time of last transmission cycle
  double referenceLatitude;   // Reference position (stored in NVS)
  double referenceLongitude;   // Reference position (stored in NVS)
  bool referencePositionSet;   // Whether reference position has been set
  
  // Constants
  static const unsigned long long GPS_FIX_ACQUISITION_TIMEOUT = 120000;  // 2 minutes in milliseconds
  static const unsigned long long DEEP_SLEEP_DURATION = 30000;  // 30 seconds in milliseconds
  static const unsigned long long GPS_FIX_ATTEMPT_DURATION = 60000;  // 1 minute in milliseconds
  
public:
  GPSScenarioHandler(GPSSensor* gpsSensor) {
    gps = gpsSensor;
    currentScenario = SCENARIO_NONE;
    gpsFixStartTime = 0;
    gpsFixEndTime = 0;
    lastTransmissionTime = 0;
    referenceLatitude = 0.0;
    referenceLongitude = 0.0;
    referencePositionSet = false;
  }
  
  // Initialize scenario handler - loads scenario state from NVS (persisted through deep sleep)
  void begin() {
    // Load scenario state from NVS (persisted through deep sleep, not reset on power_on)
    uint8_t savedScenario = NVSConfig::getScenarioState();
    if (savedScenario <= SCENARIO_4_UI_POSITION) {
      currentScenario = (GPSScenario)savedScenario;
    } else {
      currentScenario = SCENARIO_NONE;
    }
    
    // Load reference position from NVS
    loadReferencePosition();
    
    Serial.print("GPS Scenario Handler initialized - Current scenario: ");
    Serial.println(currentScenario);
  }
  
  // Start GPS fix acquisition period (2 minutes)
  void startGPSFixAcquisition() {
    gpsFixStartTime = TimeSync::getCurrentTimeMillis();
    currentScenario = SCENARIO_NONE;
    Serial.println("Starting GPS fix acquisition period (2 minutes)...");
  }
  
  // Check if 2-minute GPS fix acquisition period has elapsed
  bool isGPSFixAcquisitionComplete() {
    if (gpsFixStartTime == 0) return false;
    unsigned long long elapsed = TimeSync::getCurrentTimeMillis() - gpsFixStartTime;
    return (elapsed >= GPS_FIX_ACQUISITION_TIMEOUT);
  }
  
  // Determine scenario based on GPS fix status (called after 2-minute period or at transmission time)
  GPSScenario determineScenario() {
    if (gps == nullptr) return SCENARIO_3_NO_FIX;
    
    GPSData gpsData = gps->getGPSData();
    
    // Check if position was set from UI (Scenario 4 takes precedence)
    // This check happens at transmission time, but GPS should already be off if position was received
    if (NVSConfig::hasInitialPosition()) {
      double uiLat, uiLon;
      if (NVSConfig::getInitialPosition(uiLat, uiLon)) {
        currentScenario = SCENARIO_4_UI_POSITION;
        setReferencePosition(uiLat, uiLon);
        // Ensure GPS is off when UI position is used
        GPSSensor::powerOff();
        return currentScenario;
      }
    }
    
    // Check GPS fix status
    if (gpsData.hasValidFix) {
      // Check accuracy (only at transmission cycle time)
      if (gps->isHighAccuracy()) {
        currentScenario = SCENARIO_1_HIGH_ACCURACY;
        // Store high accuracy GPS in reference position (internal flash/NVS)
        setReferencePosition(gpsData.latitude, gpsData.longitude);
      } else {
        currentScenario = SCENARIO_2_LOW_ACCURACY;
        // Store low accuracy GPS in reference position (internal flash/NVS)
        setReferencePosition(gpsData.latitude, gpsData.longitude);
      }
    } else {
      currentScenario = SCENARIO_3_NO_FIX;
    }
    
    // Save scenario state to NVS (persisted through deep sleep)
    NVSConfig::setScenarioState((uint8_t)currentScenario);
    
    return currentScenario;
  }
  
  // Get current scenario
  GPSScenario getCurrentScenario() {
    return currentScenario;
  }
  
  // Set reference position (stored in NVS/internal flash)
  void setReferencePosition(double lat, double lon) {
    // Only log if position has actually changed (avoid repeated logs)
    bool positionChanged = (fabs(referenceLatitude - lat) > 0.0000001) || 
                          (fabs(referenceLongitude - lon) > 0.0000001) ||
                          !referencePositionSet;
    
    referenceLatitude = lat;
    referenceLongitude = lon;
    referencePositionSet = true;
    
    // Save to NVS (internal flash)
    NVSConfig::saveLastKnownPosition(lat, lon);
    
    // Only print log message if position actually changed
    if (positionChanged) {
      Serial.print("Reference position set: (");
      Serial.print(lat, 7);
      Serial.print(", ");
      Serial.print(lon, 7);
      Serial.println(")");
    }
  }
  
  // Load reference position from NVS
  void loadReferencePosition() {
    if (NVSConfig::getLastKnownPosition(referenceLatitude, referenceLongitude)) {
      referencePositionSet = true;
      Serial.print("Reference position loaded from NVS: (");
      Serial.print(referenceLatitude, 7);
      Serial.print(", ");
      Serial.print(referenceLongitude, 7);
      Serial.println(")");
    } else {
      referencePositionSet = false;
    }
  }
  
  // Get reference position
  void getReferencePosition(double& lat, double& lon) {
    lat = referenceLatitude;
    lon = referenceLongitude;
  }
  
  // Check if reference position is set
  bool hasReferencePosition() {
    return referencePositionSet;
  }
  
  // Update position after transmission (Scenario 1: from GPS, Scenario 2: from delta)
  void updatePositionAfterTransmission(double lat, double lon) {
    setReferencePosition(lat, lon);
    lastTransmissionTime = TimeSync::getCurrentTimeMillis();
    // Note: GPS fix attempt cycle is now continuous and independent of transmission time
  }
  
  // Handle Scenario 3: Deep sleep and retry
  void handleScenario3() {
    Serial.println("Scenario 3: No fix found - entering deep sleep for 30 seconds...");
    // Deep sleep will be handled in main loop
  }
  
  // Record when GPS was turned off (called after 2-minute acquisition or after fix attempt)
  void recordGPSOffTime() {
    gpsFixEndTime = TimeSync::getCurrentTimeMillis();
    gpsFixStartTime = 0;  // Clear start time since GPS is now off
    Serial.print("GPS off time recorded: ");
    Serial.println(gpsFixEndTime);
  }
  
  // Handle GPS fix attempt after transmission (Scenario 2)
  void startGPSFixAttempt() {
    Serial.println("Starting GPS fix attempt (1 minute)...");
    GPSSensor::powerOn();
    gpsFixStartTime = TimeSync::getCurrentTimeMillis();
  }
  
  // Check if GPS fix attempt duration has elapsed
  bool isGPSFixAttemptComplete() {
    if (gpsFixStartTime == 0) return false;
    unsigned long long elapsed = TimeSync::getCurrentTimeMillis() - gpsFixStartTime;
    return (elapsed >= GPS_FIX_ATTEMPT_DURATION);
  }
  
  // End GPS fix attempt (turn off GPS)
  void endGPSFixAttempt() {
    GPSSensor::powerOff();
    recordGPSOffTime();  // Record when GPS was turned off
    Serial.println("GPS fix attempt ended - GPS powered off");
    // Note: Next GPS fix attempt will be scheduled automatically by checkAndStartGPSFixAttempt()
    // based on gpsFixEndTime + "GPS on after" delay
  }
  
  // Check if it's time to start next GPS fix attempt (called from main loop)
  // Returns true if GPS was started, false otherwise
  bool checkAndStartGPSFixAttempt() {
    if (currentScenario != SCENARIO_2_LOW_ACCURACY) {
      return false;  // Only for Scenario 2
    }
    
    // If GPS is already on, don't schedule
    if (gpsFixStartTime != 0) {
      return false;  // GPS already on
    }
    
    // If GPS was never turned off, this shouldn't happen in Scenario 2
    // (GPS should have been turned off after 2-minute period or after fix attempt)
    if (gpsFixEndTime == 0) {
      // This is a safety check - if somehow we're in Scenario 2 and GPS was never turned off,
      // turn it off now and start the cycle
      Serial.println("WARNING: Scenario 2 but GPS off time not recorded - turning off GPS now");
      recordGPSOffTime();
      return false;  // Will start on next check after delay
    }
    
    // Check if "GPS on after" delay has elapsed since GPS was turned off
    unsigned long long currentTime = TimeSync::getCurrentTimeMillis();
    uint32_t gpsOnAfterSeconds = NVSConfig::getGPSOnAfter();
    unsigned long long gpsOnAfterDelayMs = (unsigned long long)gpsOnAfterSeconds * 1000ULL;
    unsigned long long nextGPSOnTime = gpsFixEndTime + gpsOnAfterDelayMs;
    
    if (currentTime >= nextGPSOnTime) {
      startGPSFixAttempt();
      return true;
    }
    
    return false;  // Not time yet
  }
  
  // Set current scenario directly (for BLE callback)
  void setCurrentScenario(GPSScenario scenario) {
    currentScenario = scenario;
    NVSConfig::setScenarioState((uint8_t)scenario);
    Serial.print("Scenario set to: ");
    Serial.println(scenario);
  }
  
  // Check if GPS fix is lost during Scenario 1
  bool checkGPSFixLost() {
    if (currentScenario != SCENARIO_1_HIGH_ACCURACY) return false;
    if (gps == nullptr) return true;
    
    GPSData gpsData = gps->getGPSData();
    if (!gpsData.hasValidFix || !gps->isHighAccuracy()) {
      // Switch to Scenario 2 with last stored position
      currentScenario = SCENARIO_2_LOW_ACCURACY;
      NVSConfig::setScenarioState((uint8_t)currentScenario);
      Serial.println("GPS fix lost - switching to Scenario 2");
      return true;
    }
    return false;
  }
  
  // Reset scenario state on power_on (not deep sleep)
  void resetOnPowerOn() {
    currentScenario = SCENARIO_NONE;
    gpsFixStartTime = 0;
    gpsFixEndTime = 0;
    NVSConfig::setScenarioState((uint8_t)SCENARIO_NONE);
    Serial.println("Scenario state reset on power_on");
  }
};

#endif

