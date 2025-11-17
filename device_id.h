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
};

#endif
