#include <Adafruit_NeoPixel.h>

#define PIN        6
#define NUMPIXELS_PER_STRIP 20
#define NUM_STRIPS 5
#define TOTAL_NUMPIXELS (NUMPIXELS_PER_STRIP * NUM_STRIPS)

Adafruit_NeoPixel strip(TOTAL_NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

int g[NUM_STRIPS] = {0}; // Variables for each strip
int y[NUM_STRIPS] = {0}; // Target values for each strip
int x[NUM_STRIPS] = {0}; // Random decrement for each strip
int z[NUM_STRIPS] = {0}; // Random increment for each strip
bool red[NUM_STRIPS] = {false}; // Red state for each strip
unsigned long temp[NUM_STRIPS] = {0}; // Temporal variables for each strip
unsigned long previousMillis[NUM_STRIPS] = {0}; // Store previous millis for each strip
unsigned long interval[NUM_STRIPS] = {0}; // Interval for each strip


int vitesseUpAnim = 5;
int vitesseDownAnim = 5;
int fluiditeAnim = 3;
int delayGlobal = 10;
int allRedTime = 2000;


void setup() {
  strip.begin();
  strip.show();
}

void loop() {
  unsigned long currentMillis = millis();

  for (int i = 0; i < NUM_STRIPS; i++) {
    // Code for each strip
    if (g[i] < vitesseDownAnim && !red[i]) {
      strip.fill(strip.Color(255, 0, 0), (i * NUMPIXELS_PER_STRIP) + 1,(i+1) * NUMPIXELS_PER_STRIP);
      strip.show();
      temp[i] = currentMillis;
      red[i] = true;
    }

    if (currentMillis - temp[i] > interval[i] && g[i] > y[i] - vitesseUpAnim && g[i] < y[i] + vitesseDownAnim) {
      int r = random(2);
      if (r == 0) {
        y[i] = random((30 * fluiditeAnim) + 1);
        z[i] = random(1, vitesseUpAnim);
        x[i] = random(1, vitesseDownAnim);
      } else {
        y[i] = 0;
        z[i] = random(1, vitesseUpAnim);
        x[i] = random(1, vitesseDownAnim);
        red[i] = false;
      }
      interval[i] = random(allRedTime);
      previousMillis[i] = currentMillis;
    } else if (g[i] < y[i]) {
      g[i] += z[i];
      g[i] = constrain(g[i], 0, 30 * fluiditeAnim);
    } else if (g[i] > y[i]) {
      g[i] -= x[i];
      g[i] = constrain(g[i], 0, 30 * fluiditeAnim);
    }

    strip.fill(strip.Color(255, g[i] / fluiditeAnim, 0), (i * NUMPIXELS_PER_STRIP) + 1, (i+1) *NUMPIXELS_PER_STRIP );
  }
  strip.show();
  delay(10);
}
