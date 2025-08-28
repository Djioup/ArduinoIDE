#include <FastLED.h>

#define LED_PIN         6         // Numéro de broche des LED
#define NUM_LEDS        100       // Nombre total de LED dans le ruban
#define BRIGHTNESS      100       // Luminosité des LED
#define SWEEP_SPEED     5        // Vitesse du balayage (en millisecondes)

CRGB leds[NUM_LEDS];

void setup() {
  FastLED.addLeds<WS2811, LED_PIN, RGB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
}

void loop() {
  // Animation de balayage vers la droite
  for (int i = 0; i < NUM_LEDS; i++) {
    // Effacer toutes les LED
    fill_solid(leds, NUM_LEDS, CRGB (255,0,0));
    // Allumer la LED correspondant à l'index actuel
    leds[i] = CRGB (255,40,0);
    leds[i - 1] = CRGB (255,30,0);
    leds[i - 2] = CRGB (255,20,0);
    leds[i - 3] = CRGB (255,20,0);
    leds[i - 4] = CRGB (255,10,0);
    leds[i - 5] = CRGB (255,10,0);

    // Afficher les LED
    FastLED.show();
    // Attendre la vitesse du balayage
    delay(SWEEP_SPEED);
  }

  // Animation de balayage vers la gauche
  for (int i = NUM_LEDS - 1; i >= 0; i--) {
    // Effacer toutes les LED
    fill_solid(leds, NUM_LEDS, CRGB (255,0,0));
    // Allumer la LED correspondan
    leds[i] = CRGB (255,40,0);
    leds[i + 1] = CRGB (255,30,0);
    leds[i + 2] = CRGB (255,20,0);
    leds[i + 3] = CRGB (255,20,0);
    leds[i + 4] = CRGB (255,10,0);
    leds[i + 5] = CRGB (255,10,0);
    // Afficher les LED
    FastLED.show();
    // Attendre la vitesse du balayage
    delay(SWEEP_SPEED);
  }
}
