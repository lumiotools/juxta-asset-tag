#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <string.h>
#include <vector>

// Configuration
static const char AP_SSID[] = "PMC Test Hub";
static const char AP_PWD[] = "PMC.Hub@Test";
static const char PREFIX[] = "Juxta AT";
// ESP32 usually supports ~7 concurrent connections, but we limit to 4 for stability
static const int MAX_CLIENTS = 4; 

// Track connected clients
std::vector<NimBLEClient*> connectedClients;

// Check if we are already connected to this address
bool isAlreadyConnected(NimBLEAddress addr) {
  for (auto* c : connectedClients) {
    if (c->getPeerAddress().equals(addr)) return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  Serial.println("BLE Hub Starting in Simplified Mode...");

  // 1. Setup BLE
  NimBLEDevice::init("JuxtaHub");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  
  // 2. Setup WiFi (host-only AP)
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PWD);
  Serial.printf("AP Started: %s (IP: %s)\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void loop() {
  // Only scan if we have slots available
  if (connectedClients.size() < MAX_CLIENTS) {
    Serial.printf("Scanning... (Connected: %d/%d)\n", connectedClients.size(), MAX_CLIENTS);
    
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    // start(duration, is_continue) -> duration in seconds. Returns bool in this version.
    pScan->start(0); 

    unsigned long scanStart = millis();
    while ((millis() - scanStart) < 2500) {
      delay(50);
    }

    NimBLEScanResults results = pScan->getResults();

    for (int i = 0; i < results.getCount(); i++) {
      const NimBLEAdvertisedDevice* d = results.getDevice(i);
      String name = d->getName().c_str();

      // If advertised name starts with the PREFIX, attempt connection if not already connected
      if (name.startsWith(PREFIX)) {
        if (!isAlreadyConnected(d->getAddress())) {
          NimBLEClient* pClient = NimBLEDevice::createClient();
          if (pClient->connect(d->getAddress())) {
            connectedClients.push_back(pClient);
            if (connectedClients.size() >= MAX_CLIENTS) break; // Stop connecting if full
          } else {
            NimBLEDevice::deleteClient(pClient);
          }
        }
      }
    }
    pScan->clearResults(); // Important to free memory
  } else {
    // Full, just wait a bit and check for disconnects
    delay(1000);
  }

  // 4. Cleanup disconnected clients
  for (auto it = connectedClients.begin(); it != connectedClients.end(); ) {
    if (!(*it)->isConnected()) {
      Serial.println("Client disconnected. freeing resource.");
      NimBLEDevice::deleteClient(*it);
      it = connectedClients.erase(it);
    } else {
      ++it;
    }
  }

  delay(100);
}
