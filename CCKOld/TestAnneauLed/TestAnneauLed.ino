#include <Adafruit_NeoPixel.h>

#define PIN        13           // Pin de données
#define NUM_LEDS   16           // Nombre de LEDs
#define DELAY_MS   300          // Délai d'animation

Adafruit_NeoPixel ring(NUM_LEDS, PIN, NEO_GRB + NEO_KHZ800);
bool isGreen = false;          // État actuel des LEDs

void setup() {
  ring.begin();
  ring.show(); // Initialisation (tout éteint)

  // Allume toutes les LEDs en rouge au départ
  for (int i = 0; i < NUM_LEDS; i++) {
    ring.setPixelColor(i, ring.Color(255, 0, 0));
  }
  ring.show();
  delay(1000);
}

void loop() {
  if (!isGreen) {
    // Passage rouge → vert progressivement
    for (int i = 0; i < NUM_LEDS; i++) {
      ring.setPixelColor(i, ring.Color(0, 255, 0));
      ring.show();
      delay(DELAY_MS);
    }
    isGreen = true;
  } else {
    // Passage vert → rouge progressivement
    for (int i = 0; i < NUM_LEDS; i++) {
      ring.setPixelColor(i, ring.Color(255, 0, 0));
      ring.show();
      delay(DELAY_MS);
    }
    isGreen = false;
  }

  delay(500); // Pause entre les cycles
}
