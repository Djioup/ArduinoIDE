#include <FastLED.h>

#define LED_PIN     6
#define NUM_LEDS    30
#define BRIGHTNESS  255
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB

CRGB leds[NUM_LEDS];
int delayTime = 60; // Délai initial entre chaque déplacement de la lumière
int fadeAmount = 10; // Valeur de diminution de la luminosité pour le fondu

void setup() {
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
}

void loop() {
  // Trajet de la lumière d'un bout à l'autre de la bande
  for(int i=0; i<NUM_LEDS; i++) {
    // Afficher la lumière blanche sur la position actuelle
    leds[i] = CRGB::White;
    FastLED.show();
    delay(delayTime); // Utilisation de la variable delayTime pour contrôler la vitesse
    
    // Éteindre la LED précédente en la faisant diminuer progressivement en bleu turquoise
    for(int j=i-1; j>=0; j--) {
      if (leds[j] == CRGB::Blue) break; // Arrêter le fondu si la LED est éteinte
      leds[j] -= CRGB(fadeAmount/2, fadeAmount/10, fadeAmount); // Diminution de la luminosité avec une composante bleue plus importante
    }
    FastLED.show();
  }
  
  // Trajet de retour de la lumière
  for(int i=NUM_LEDS-1; i>=0; i--) {
    // Afficher la lumière blanche sur la position actuelle
    leds[i] = CRGB::White;
    FastLED.show();
    delay(delayTime); // Utilisation de la variable delayTime pour contrôler la vitesse
    
    // Éteindre la LED précédente en la faisant diminuer progressivement en bleu turquoise
    for(int j=i+1; j<NUM_LEDS; j++) {
      if (leds[j] == CRGB::Red) break; // Arrêter le fondu si la LED est éteinte
      leds[j] -= CRGB(fadeAmount/2, fadeAmount/10, fadeAmount); // Diminution de la luminosité avec une composante bleue plus importante
    }
    FastLED.show();
  }
}
