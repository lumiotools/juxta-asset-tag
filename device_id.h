#ifndef DEVICE_ID_H
#define DEVICE_ID_H

#include <WiFi.h>
#include "esp_mac.h"

class DeviceID {
public:
  // Get MAC address as string (format: AA:BB:CC:DD:EE:FF)
  static void getMACAddress(char* buffer, size_t bufSize) {
    byte mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buffer, bufSize, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
  
  // Get device ID from MAC address (format: ASSET_TAG_AABBCCDDEEFF)
  static void getDeviceId(char* buffer, size_t bufSize) {
    char macBuffer[18];  // MAC address with colons: "AA:BB:CC:DD:EE:FF"
    getMACAddress(macBuffer, sizeof(macBuffer));
    
    // Remove colons from MAC address for cleaner device ID
    char macNoColons[13];  // MAC without colons: "AABBCCDDEEFF"
    int j = 0;
    for (int i = 0; i < 17 && macBuffer[i] != '\0'; i++) {
      if (macBuffer[i] != ':') {
        macNoColons[j++] = macBuffer[i];
      }
    }
    macNoColons[j] = '\0';
    
    // Format: ASSET_TAG_{MAC}
    snprintf(buffer, bufSize, "ASSET_TAG_%s", macNoColons);
  }
};

#endif
