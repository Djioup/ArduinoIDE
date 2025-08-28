#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <BH1750.h>

#define LED_PIN 6
#define NUM_LEDS 600
#define PRESENCE_SENSOR_PIN 3
#define PRESENCE_SENSOR_PIN2 2
#define BH1750_ADDRESS A0;

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
BH1750 lightMeter(BH1750_ADDRESS);

unsigned long lastPresenceTime = 0;
bool presenceDetected = false;
unsigned long Onshow;
int lightLevel = 0;

void setup() {
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  pinMode(PRESENCE_SENSOR_PIN, INPUT);
  pinMode(PRESENCE_SENSOR_PIN2, INPUT);

  lightMeter.begin();

  // Initialisation de la luminosité à 0
  strip.setBrightness(0);
}

void loop() {
  if (digitalRead(PRESENCE_SENSOR_PIN) == HIGH || digitalRead(PRESENCE_SENSOR_PIN2) == HIGH) {
    // Si le capteur de présence détecte une présence
    if (!presenceDetected) {
      presenceDetected = true;
      lastPresenceTime = millis();
    }
    Onshow = millis();
    unsigned long elapsedTime = millis() - lastPresenceTime;

    // Éteindre les LEDs
    strip.setBrightness(0);
    strip.show();

    // Mesurer le niveau de luminosité uniquement lorsque les LEDs sont éteintes
    delay(100); // Attente pour que les LEDs s'éteignent complètement
    lightLevel = lightMeter.readLightLevel();

    // Contrôler la couleur des LEDs en fonction du niveau de luminosité
    if (lightLevel < 500) {
      // Faible luminosité : couleur bleue
      strip.fill(strip.Color(0, 0, 255),0, NUM_LEDS);
    } else if (lightLevel >= 500 && lightLevel < 1000) {
      // Luminosité moyenne : couleur orange-jaune
      strip.fill(strip.Color(255, 165, 0),0, NUM_LEDS);
    } else {
      // Forte luminosité : couleur noire
      strip.fill(strip.Color(0, 0, 0),0, NUM_LEDS);
    }

    // Augmenter la luminosité progressivement à partir de 0
    if (elapsedTime < 3000) {      
      uint8_t initialbrightness = strip.getBrightness();
      uint8_t brightness = map(elapsedTime, 0, 3000, initialbrightness, 255);
      strip.setBrightness(brightness);
      strip.show();
    }
  } else if (Onshow < millis() - 30000) {
    // Si le capteur de présence ne détecte pas de présence
    if (presenceDetected) {
      presenceDetected = false;
      lastPresenceTime = millis();
    }

    unsigned long elapsedTime = millis() - lastPresenceTime;

    // Réduire la luminosité progressivement à partir de 255
    if (elapsedTime < 10000) {
      uint8_t initialbrightness = strip.getBrightness();
      uint8_t brightness = map(elapsedTime, 0, 10000, initialbrightness, 0);
      strip.setBrightness(brightness);
      strip.show();
    }
  }
}