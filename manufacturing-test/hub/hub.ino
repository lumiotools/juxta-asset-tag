#include <Arduino.h>
#include <NimBLEDevice.h>
#include <FastLED.h>
#include <Ticker.h>
#include <string.h>
#include <vector>

#define POWER_LATCH_PIN 4

// Configuration
static const char PREFIX[] = "Juxta AT";
static const int MAX_CLIENTS = 5; 

// LED Configuration (match device.ino)
static const int STATUS_LED_PIN = 11;
static const int STATUS_LED_COUNT = 2;
CRGB statusLED[STATUS_LED_COUNT];

// Ticker for LED blinks
Ticker blinkTicker;
volatile int blinksRemaining = 0;

void setPowerLatchPin(bool high) {
  if (high) {
    // Set HIGH: Configure as OUTPUT with pull-up
    pinMode(POWER_LATCH_PIN, OUTPUT);
    gpio_set_pull_mode(GPIO_NUM_4, GPIO_PULLUP_ONLY);
    digitalWrite(POWER_LATCH_PIN, HIGH);
    Serial.println("Power latch pin (IO4) set HIGH with pull-up");
  } else {
    // Set LOW: Configure as OUTPUT with pull-down
    pinMode(POWER_LATCH_PIN, OUTPUT);
    gpio_set_pull_mode(GPIO_NUM_4, GPIO_PULLDOWN_ONLY);
    digitalWrite(POWER_LATCH_PIN, LOW);
    Serial.println("Power latch pin (IO4) set LOW with pull-down");
  }
}

// LED Blink Callback
void handleBlink() {
  if (blinksRemaining > 0) {
    blinksRemaining--;
    statusLED[1] = (blinksRemaining % 2 != 0) ? CRGB::Blue : CRGB::Black;
    FastLED.show();
    
    if (blinksRemaining == 0) {
      blinkTicker.detach();
    }
  }
}

// Function to trigger/reset blinks
void triggerConnectionBlink() {
  blinksRemaining = 6; // 3 blinks (on-off * 3)
  blinkTicker.detach(); // Reset if already running
  blinkTicker.attach_ms(300, handleBlink);
}

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
  setPowerLatchPin(true);

  // Initialize LEDs
  FastLED.addLeds<WS2812, STATUS_LED_PIN, GRB>(statusLED, STATUS_LED_COUNT);
  
  // Turn on LED 1 as green immediately
  statusLED[0] = CRGB::Green;
  statusLED[1] = CRGB::Black;
  FastLED.show();

  // 1. Setup BLE
  NimBLEDevice::init("JuxtaHub");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
}

void loop() {
  // Only scan if we have slots available
  if (connectedClients.size() < MAX_CLIENTS) {
    Serial.printf("Scanning... (Connected: %d/%d)\n", (int)connectedClients.size(), MAX_CLIENTS);
    
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    pScan->start(0); 

    unsigned long scanStart = millis();
    while ((millis() - scanStart) < 5000) {
      delay(50);
    }

    NimBLEScanResults results = pScan->getResults();

    for (int i = 0; i < results.getCount(); i++) {
      const NimBLEAdvertisedDevice* d = results.getDevice(i);
      String name = d->getName().c_str();

      if (name.startsWith(PREFIX)) {
        // Found a Juxta device
        if (!isAlreadyConnected(d->getAddress())) {
          Serial.printf("Found new target: %s\n", name.c_str());
          
          NimBLEClient* pClient = NimBLEDevice::createClient();
          if (pClient->connect(d->getAddress())) {
            Serial.printf("Connected to %s\n", name.c_str());
            connectedClients.push_back(pClient);
            triggerConnectionBlink();
            
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

  // Cleanup disconnected clients
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

