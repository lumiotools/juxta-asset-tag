#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <string.h>
#include <vector>

// Configuration
static const char STA_SSID[] = "Realme P2 Pro";
static const char STA_PWD[] = "0000000000";
static const char AP_SSID[] = "PMC Test Hub";
static const char AP_PWD[] = "PMC.Hub@Test";
static const char PREFIX[] = "Juxta AT";
static const char REGISTER_URL[] = "https://juxta-pcb-manufacturing.vercel.app/api/register-device";
// ESP32 usually supports ~7 concurrent connections, but we limit to 3 for stability
static const int MAX_CLIENTS = 3; 

// Track connected clients
std::vector<NimBLEClient*> connectedClients;

// Simple MAC queue for registration (keeps code small, avoids heavy work in callback)
static const uint8_t MAC_QUEUE_CAP = 16;
static char macQueue[MAC_QUEUE_CAP][13];
static volatile uint8_t macQHead = 0;
static volatile uint8_t macQTail = 0;
static volatile uint8_t macQCount = 0;

static bool macQueuePush(const char *mac12) {
  bool ok = false;
  noInterrupts();
  if (macQCount < MAC_QUEUE_CAP) {
    strncpy(macQueue[macQTail], mac12, 13);
    macQueue[macQTail][12] = 0;
    macQTail = (uint8_t)((macQTail + 1) % MAC_QUEUE_CAP);
    macQCount++;
    ok = true;
  }
  interrupts();
  return ok;
}

static bool macQueuePop(char outMac12[13]) {
  bool ok = false;
  noInterrupts();
  if (macQCount > 0) {
    strncpy(outMac12, macQueue[macQHead], 13);
    outMac12[12] = 0;
    macQHead = (uint8_t)((macQHead + 1) % MAC_QUEUE_CAP);
    macQCount--;
    ok = true;
  }
  interrupts();
  return ok;
}

// Helper to extract ID from name "Juxta AT1234..."
String getIDFromName(String name) {
  if (!name.startsWith(PREFIX)) return "";
  String id = name.substring(strlen(PREFIX));
  id.trim();
  id.replace(":", "");
  id.replace("-", ""); 
  return id;
}

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
  
  // 2. Setup WiFi (AP + Station)
  WiFi.mode(WIFI_AP_STA);
  
  // Register event handler for AP joins
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
      char macStr[13];
      sprintf(macStr, "%02X%02X%02X%02X%02X%02X", 
        info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
        info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
        info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5]);

      Serial.printf("AP: Device joined! MAC: %s\n", macStr);

      if (!macQueuePush(macStr)) {
        Serial.println("Register MAC queue full, dropping");
      }
    }
  });

  WiFi.begin(STA_SSID, STA_PWD);
  WiFi.softAP(AP_SSID, AP_PWD);

  Serial.printf("AP Started: %s (IP: %s)\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  Serial.printf("Connecting to %s...", STA_SSID);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.printf("\nWiFi Connected! IP: %s\n", WiFi.localIP().toString().c_str());
}

void loop() {
  // Handle device registration queue
  static char retryMac[13] = {0};
  static bool retryPending = false;
  static unsigned long lastAttemptMs = 0;
  static const unsigned long RETRY_DELAY_MS = 1500;

  if (!retryPending) {
    retryPending = macQueuePop(retryMac);
  }

  if (retryPending && (millis() - lastAttemptMs) >= RETRY_DELAY_MS) {
    lastAttemptMs = millis();
    bool ok = false;

    if (WiFi.status() == WL_CONNECTED) {
      WiFiClientSecure client;
      client.setInsecure();

      HTTPClient http;
      http.setTimeout(6000);
      http.begin(client, REGISTER_URL);
      http.addHeader("Content-Type", "text/plain");

      String payload = "AT" + String(retryMac);
      int code = http.POST(payload);
      Serial.printf("Registration (%s) response: %d\n", retryMac, code);
      http.end();
      ok = (code >= 200 && code < 300);
    } else {
      Serial.println("Registration deferred: No WiFi connection");
    }

    if (ok) {
      retryPending = false;
    } else {
      // append back to queue; if queue is full, keep retryPending and try again later
      if (macQueuePush(retryMac)) {
        retryPending = false;
      }
    }
  }

  // Only scan if we have slots available
  if (connectedClients.size() < MAX_CLIENTS) {
    Serial.printf("Scanning... (Connected: %d/%d)\n", connectedClients.size(), MAX_CLIENTS);
    
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    // start(duration, is_continue) -> duration in seconds. Returns bool in this version.
    pScan->start(0); 

    unsigned long scanStart = millis();
    while ((millis() - scanStart) < 5000) {
      delay(50);
    }

    NimBLEScanResults results = pScan->getResults();

    for (int i = 0; i < results.getCount(); i++) {
      const NimBLEAdvertisedDevice* d = results.getDevice(i);
      String name = d->getName().c_str();
      String id = getIDFromName(name);

      if (id.length() > 0) {
        // Found a Juxta device
        if (!isAlreadyConnected(d->getAddress())) {
          Serial.printf("Found new target: %s (ID: %s)\n", name.c_str(), id.c_str());
          
          NimBLEClient* pClient = NimBLEDevice::createClient();
          if (pClient->connect(d->getAddress())) {
            Serial.printf("Connected to %s\n", id.c_str());
            connectedClients.push_back(pClient);
            
            if (connectedClients.size() >= MAX_CLIENTS) break; // Stop connecting if full
          } else {
            Serial.println("Connection failed.");
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
