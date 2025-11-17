#ifndef WS2812B_SIMPLE_H
#define WS2812B_SIMPLE_H

#include <Arduino.h>

// Lightweight WS2812B driver - saves ~15-20KB vs Adafruit_NeoPixel
class WS2812B {
private:
  int pin;
  uint8_t brightness;
  
  void sendByte(uint8_t b) {
    for (int i = 7; i >= 0; i--) {
      digitalWrite(pin, HIGH);
      if (b & (1 << i)) {
        delayMicroseconds(0.7);
        digitalWrite(pin, LOW);
        delayMicroseconds(0.6);
      } else {
        delayMicroseconds(0.35);
        digitalWrite(pin, LOW);
        delayMicroseconds(0.8);
      }
    }
  }
  
public:
  WS2812B(int p) : pin(p), brightness(255) {
    // Don't initialize GPIO in constructor - causes boot crash
    // GPIO will be initialized in begin()
  }
  
  void begin() {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  
  void setPixelColor(int index, uint8_t r, uint8_t g, uint8_t b) {
    (void)index; // Only 1 LED
    uint8_t r_adj = (r * brightness) >> 8;
    uint8_t g_adj = (g * brightness) >> 8;
    uint8_t b_adj = (b * brightness) >> 8;
    
    noInterrupts();
    sendByte(g_adj); // GRB order
    sendByte(r_adj);
    sendByte(b_adj);
    interrupts();
    delayMicroseconds(50); // Reset pulse
  }
  
  void clear() {
    setPixelColor(0, 0, 0, 0);
  }
  
  void setBrightness(uint8_t b) {
    brightness = b;
  }
  
  void show() {
    // Already sent in setPixelColor
  }
};

#endif

