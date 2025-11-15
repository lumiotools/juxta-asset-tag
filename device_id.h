#ifndef DEVICE_ID_H
#define DEVICE_ID_H

#include <WiFi.h>

class DeviceID {
public:
  // Get MAC address as string (format: AA:BB:CC:DD:EE:FF)
  static String getMACAddress() {
    byte mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
  }
  
  // Get MAC address as string without colons (format: AABBCCDDEEFF)
  static String getMACAddressNoDashes() {
    byte mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    char macStr[13];
    snprintf(macStr, sizeof(macStr), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
  }
};

#endif
