#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_NeoPixel.h>

// ------------------------------------------------------------ INIT ------------------------------------------------------------------------------
// variables
int turnCount = 0;
bool capteur1Passe = false;
bool capteur2Passe = false;
int NbrTour = 12;
int DechargeCharging = 10000;
int DechargeGame = 2000;
unsigned long LastturnCount = 0;
unsigned long StartGameState = 0;

//GPIO
const int MAG_SENSOR_1 = 27;
const int MAG_SENSOR_2 = 14;

#define ANNEAULED 4
#define NUM_LEDS 12
Adafruit_NeoPixel ring(NUM_LEDS, ANNEAULED, NEO_GRB + NEO_KHZ800);

// FREERTOS
enum GeneratorState {
  CHARGING,
  GAME
};

// -------------------------------------------------------------------- Game ------------------------------------------------------
void generatorTask(void* parameter) {
  while (true) {

    switch (genState) {
      case CHARGING:
        if (turnCount < 0) turnCount = 0;

        // 🔹 Détection du capteur secondaire (doit être activé avant le principal)
        if (digitalRead(MAG_SENSOR_1) == 0 && !capteur2Passe) {  // Anti-rebond
          capteur2Passe = true;
          capteur1Passe = false;
          turnCount += 1;
          LastturnCount = millis();
          Serial.println("🔸 Capteur secondaire activé !");
        }

        if (digitalRead(MAG_SENSOR_2) == 0 && !capteur1Passe) {
          capteur1Passe = true;
          capteur2Passe = false;
          turnCount += 1;
          LastturnCount = millis();
          Serial.println("🔸 Capteur premier activé !");
        }

        if (millis() > LastturnCount + DechargeCharging)
        {
          turnCount -= 1;
          LastTurnCount = millis();
        }

        ChargingLED(turnCount);

        if (turnCount >= NbrTour) {
          StartGameState = millis();
          genState = GAME;
        }

        break;

      case GAME:
      if (millis() > StartGameState + DechargeGame)
      {
        turnCount -= 1;
        StateGameState = millis();
      }
      
      if (turnCount == 0) genState = CHARGING;

      ChargedLED(turncount);

      

      break;
    }
  }
}


// ----------------------------------------- Fonctions Utilitaires -------------------------------------------------------------
void ChargingLED(int TurnCount)
{
  If (TurnCount == 0)
  {
    for(int i = 0, i < NUM_LEDS, i ++)
    {
      ring.setPixelColor(i,ring.Color(120,0,0))
    }
    ring.show();
  }

  else{
    for (int i = 0, i < TurnCount, i ++)
    {
      ring.setPixelColor(i, ring.Color(120,60,0));
    }
    for (int i = TurnCount + 1, i < NUM_LEDS, i ++)
    {
      ring.setPixelColor(i, ring.Color(0,0,0));
    }
    ring.show();
  }
}

void ChargedLED(int TurnCount){
  for (int i = 0, i < TurnCount, i ++)
  {
    ring.setPixelColor(i, ring.Color(0,120,0));
  }
  for (int i = TurnCount + 1, i < NUM_LEDS, i ++)
  {
    ring.setPixelColor(i, ring.Color(0,0,0));
  }
  ring.show();
}
