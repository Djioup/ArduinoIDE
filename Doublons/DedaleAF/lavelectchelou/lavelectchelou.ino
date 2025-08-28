#include <FastLED.h>
#define LED_PIN             6           // Numéro de broche des LED
#define NUM_LEDS            100         // Nombre total de LED dans le ruban
#define BRIGHTNESS          100         // Luminosité (de 0 à 255)
int FADE_SPEED =          10;           // Vitesse de fondu entre les LED (plus la valeur est basse, plus le fondu est rapide)
#define MIN_BRIGHTNESS      0           // Luminosité minimale pour une LED allumée
#define MAX_BRIGHTNESS      150         // Luminosité maximale pour une LED allumée
#define MIN_TRAIL_LENGTH    5          // Longueur minimale de la traînée
#define MAX_TRAIL_LENGTH    10          // Longueur maximale de la traînée         // Espacement maximal entre les traînées
#define TRAIL_BRIGHTNESS_FACTOR 0.8     // Facteur de réduction de la luminosité de la traînée
#define CYCLE_DELAY         5           // Délai entre chaque cycle (en millisecondes)

CRGB leds[NUM_LEDS];

int trailLength = 5;
      int trailSpacing = 6;

void setup() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS); // Initialiser les LED
  FastLED.setBrightness(BRIGHTNESS); // Définir la luminosité initiale
}

void loop() {
  

  // Effet de trainées lumineuses
  for (int i = 0; i < NUM_LEDS; i++) {
    
      trailLength += random (-1,2);
      //trailSpacing = trailLength + 10;
      FADE_SPEED += random(-1,2);
      FADE_SPEED = constrain(FADE_SPEED, 30, 50);
      trailLength = constrain(trailLength, MIN_TRAIL_LENGTH, MAX_TRAIL_LENGTH);
      //trailSpacing = constrain(trailSpacing, MIN_TRAIL_SPACING, MAX_TRAIL_SPACING);
    
    
    // Dessiner la traînée
    for (int j = 0; j < trailLength; j++) {
      int ledIndex = (i + j * trailSpacing) % NUM_LEDS;
      int ledIndex2 = (i + 30 + j * trailSpacing) % NUM_LEDS;
      int ledIndex3 = (i + 60 + j * trailSpacing) % NUM_LEDS;
      int brightness = MAX_BRIGHTNESS - (j * ((MAX_BRIGHTNESS - MIN_BRIGHTNESS) / trailLength)); // Calculer la luminosité en fonction de la position dans la traînée
      leds[ledIndex] = CRGB(0, brightness * TRAIL_BRIGHTNESS_FACTOR, 0); // Couleur bleue avec luminosité réduite
      leds[ledIndex2] = CRGB(brightness * TRAIL_BRIGHTNESS_FACTOR,0, 0); // Couleur bleue avec luminosité réduite
      leds[ledIndex3] = CRGB(0,0, brightness * TRAIL_BRIGHTNESS_FACTOR); // Couleur bleue avec luminosité réduite
    }
    
    // Allumer le pixel principal
    leds[i] = CRGB(0, MAX_BRIGHTNESS, 0); // Couleur bleue avec luminosité maximale
    leds[i + 30] = CRGB(MAX_BRIGHTNESS, 0, 0); // Couleur bleue avec luminosité maximale
    leds[i + 60] = CRGB(0, 0, MAX_BRIGHTNESS); // Couleur bleue avec luminosité maximale
    FastLED.show();
    delay(FADE_SPEED);
    
    // Éteindre les LEDs
    for (int j = 0; j < trailLength; j++) {
      int ledIndex = (i + j * trailSpacing) % NUM_LEDS;
      int ledIndex2 = (i + 30 + j * trailSpacing) % NUM_LEDS;
      int ledIndex3 = (i + 60 + j * trailSpacing) % NUM_LEDS;
      leds[ledIndex] = CRGB::Black; // Éteindre la traînée
      leds[ledIndex2] = CRGB::Black; // Éteindre la traînée
    leds[ledIndex3] = CRGB::Black; // Éteindre la traînée
    }
    leds[i] = CRGB::Black; // Éteindre le pixel principal
    leds[(i + 30)%NUM_LEDS] = CRGB::Black;
    leds[(i + 60)%NUM_LEDS] = CRGB::Black;
  }
  delay(CYCLE_DELAY); // Délai entre chaque cycle
}
