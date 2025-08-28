#include <Adafruit_NeoPixel.h>

#define LED_PIN 6
#define NUM_LEDS 600
#define PRESENCE_SENSOR_PIN 3
#define PRESENCE_SENSOR_PIN2 2

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

unsigned long lastPresenceTime = 0;
bool presenceDetected = false;
unsigned long Onshow;

void setup() {
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  pinMode(PRESENCE_SENSOR_PIN, INPUT);
pinMode(PRESENCE_SENSOR_PIN2 INPUT);
  // Initialisation de la luminosité à 0
  strip.setBrightness(0);
}

void loop() {
  if (digitalRead(PRESENCE_SENSOR_PIN == HIGH) || digitalRead(PRESENCE_SENSOR_PIN2 == HIGH)) {
    // Si le capteur de présence détecte une présence
    if (!presenceDetected) {
      presenceDetected = true;
      lastPresenceTime = millis();
    }
    Onshow = millis();
    unsigned long elapsedTime = millis() - lastPresenceTime;

    // Augmenter la luminosité progressivement à partir de 0
    if (elapsedTime < 3000) {      
      uint8_t initialbrightness = strip.getBrightness();
      uint8_t brightness = map(elapsedTime, 0, 3000, initialbrightness, 255);
      strip.setBrightness(brightness);
      strip.fill(strip.Color(0, (brightness/20), brightness), 0, NUM_LEDS);
      strip.show();
    }
  } else if ( Onshow < millis() - 30000){
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
      strip.fill(strip.Color(0, (brightness/20), brightness), 0, NUM_LEDS);
      strip.show();
    }
  }
}
