#include <Adafruit_NeoPixel.h>

#define LED_PIN    13
#define LED_COUNT  30

int ry = 0;

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  strip.begin();
  strip.show();            // Éteint tout
  strip.setBrightness(150); // ≈30 % pour éviter la surconsommation
}

void loop() {

  for (int i = 0; i <= 255; i++) {
    setAll(strip.Color(0, 0, i));
    delay(50);
  }
  // 1. Bleu -> Vert
  for (int i = 0; i <= 255; i++) {
    setAll(strip.Color(0, i, 255 - i));
    delay(100);
  }

  // 2. Pointe de rouge (vert dominant)
  for (int r = 0; r <= 150; r++) {
    setAll(strip.Color(r, 255, 0));
    ry = r;
    delay(35);
  }

  // 3. Retour au bleu
  for (int i = 0; i <= 255; i++) {
    int ryi = ry - i;
    if (ryi < 0) ryi = 0;
    setAll(strip.Color(ryi, 255 - i, 0));
    delay(100);
  }

  delay(30000);
}

void setAll(uint32_t color) {
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}
