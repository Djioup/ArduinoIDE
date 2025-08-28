#include <FastLED.h>

#define LED_PIN             6           // Numéro de broche des LED
#define NUM_LEDS            160         // Nombre total de LED dans le ruban
#define BRIGHTNESS          250         // Luminosité (de 0 à 255)
int x,y;
#define COLOR_TURQUOISE     CRGB(x, 255, 0)  // Couleur bleu turquoise
#define MAX_TRAIL_LENGTH    20          // Longueur maximale du trait
#define MIN_TRAIL_LENGTH    10           // Longueur minimale du trait
#define MAX_SPEED           10          // Vitesse maximale du trait
#define MIN_SPEED           1           // Vitesse minimale du trait
#define CYCLE_DELAY         10          // Délai entre chaque cycle (en millisecondes)

CRGB leds[NUM_LEDS];

void setup() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS); // Initialiser les LED
  FastLED.setBrightness(BRIGHTNESS); // Définir la luminosité initiale
  randomSeed(analogRead(0));
  y = 120;
  // Initialiser toutes les LED avec la couleur bleu turquoise
  fill_solid(leds, NUM_LEDS, COLOR_TURQUOISE);
  FastLED.show(); // Mettre à jour l'affichage
  Serial.begin(115200);
}

void loop() {
COLOR_TURQUOISE = CRGB(x,0,255);
Serial.println(x);


  
  int trailPosition = 0; // Position actuelle du trait
  int trailDirection = 1; // Direction actuelle du trait (1 pour avancer, -1 pour reculer)
  
  // Effet de trait traversant
  for (int i = 0; i < NUM_LEDS; i++) {
    int trailLength = map(i, 0, NUM_LEDS, MIN_TRAIL_LENGTH, MAX_TRAIL_LENGTH); // Longueur du trait en fonction de la position
    int speed = map(i, 0, NUM_LEDS, MAX_SPEED, MIN_SPEED); // Vitesse du trait en fonction de la position
    
    // Réinitialiser toute la bande LED à la couleur bleu turquoise
    fill_solid(leds, NUM_LEDS, COLOR_TURQUOISE);
      if (x>y)
  {
  x-=1;
  }
  else if (x<y)
  {
  x+=1;
  }
  else if (x == y)
  {
  y = random(256);
  }
    
    // Dessiner le nouveau trail
    for (int j = 0; j < trailLength; j++) {
      int ledIndex = (trailPosition + (trailDirection * j) + NUM_LEDS) % NUM_LEDS; // Position du trait sur la LED
      int brightness = map(j, 0, trailLength - 1, 0, 255); // Intensité du trait en fonction de sa position
      CRGB color = CRGB(255, 255, 255).fadeLightBy(brightness); // Ajuster la luminosité de la couleur blanche
      
      // Mélanger progressivement avec la couleur turquoise en fonction de la position dans le trail
      float mixAmount = (float)j / (float)trailLength;
      leds[ledIndex] = color.lerp8(COLOR_TURQUOISE, mixAmount * 255);
    }
    
    FastLED.show();
    delay(speed);

    trailPosition += trailDirection; // Déplacer la position du trait
    
    // Inverser la direction du trait lorsque celui-ci atteint la fin ou le début du ruban
    if (trailPosition >= NUM_LEDS - 1 || trailPosition <= 0) {
      trailDirection *= -1;
    }
  }
  delay(CYCLE_DELAY); // Délai entre chaque cycle
}
