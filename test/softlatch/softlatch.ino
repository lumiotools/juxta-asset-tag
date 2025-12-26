
#include <PKAE_Timer.h>

#define MY_POWER  36       // Output pin used to Retain (HIGH) or Kill (LOW) supply power
#define Switch    37       // Input Pin for Push Switch 
#define LED 17
//==========================================================================================================
// SETUP                                                                                                   |
//==========================================================================================================

void setup() {

// MY_POWER Pin wired to Base of NPN, Set High too keep Power ON!
//---------------------------------------------------------------
  pinMode(MY_POWER, OUTPUT);          
  digitalWrite(MY_POWER, HIGH);

  pinMode(LED, OUTPUT);
  pinMode(Switch, INPUT_PULLUP);

}

//==========================================================================================================
// LOOP                                                                                                    |
//==========================================================================================================
void loop() {

  boolean lLEDstate = LOW, lReleased = true;

  PKAE_Timer StableLED(300);
  PKAE_Timer ButtonHeld(3000);

  while (true) {

    if (digitalRead(Switch) == LOW) {
      
      if (lReleased and StableLED.IsTimeUp()) {
        lLEDstate = !lLEDstate;
        digitalWrite(LED, lLEDstate);
      }
      lReleased = false;
    } 
    else {
      lReleased = true;
      ButtonHeld.Reset();
    }

    if (ButtonHeld.IsTimeUp()) KillPower();
  }
}
//==========================================================================================================
// KillPower - Turn Off Power I/O and Rapid Flash LED until power is lost.                                 |
//==========================================================================================================

void KillPower() {

  boolean lLEDstate = LOW;
  PKAE_Timer BlinkLED(100);
  digitalWrite(MY_POWER, LOW);

  // Rapid flash Onboard LED indicate power about to be lost
  while (true) {
    if (BlinkLED.IsTimeUp()) {
      lLEDstate = !lLEDstate;
      digitalWrite(LED, lLEDstate);
    }
  }
}
