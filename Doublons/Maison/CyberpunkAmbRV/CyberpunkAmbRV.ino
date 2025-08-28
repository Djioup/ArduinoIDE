#include <FastLED.h>
 
#define PIN                 6
#define NUMPIXELS_PER_STRIP 10
#define NUM_STRIPS         10
#define TOTAL_NUMPIXELS     (NUMPIXELS_PER_STRIP * NUM_STRIPS)

CRGB leds[TOTAL_NUMPIXELS];

int g[NUM_STRIPS] = {0}; // Variables for each strip
int y[NUM_STRIPS] = {0}; // Target values for each strip
int x[NUM_STRIPS] = {0}; // Random decrement for each strip
int z[NUM_STRIPS] = {0}; // Random increment for each strip
bool red[NUM_STRIPS] = {false}; // Red state for each strip
unsigned long temp[NUM_STRIPS] = {0}; // Temporal variables for each strip
unsigned long previousMillis[NUM_STRIPS] = {0}; // Store previous millis for each strip
unsigned long interval[NUM_STRIPS] = {0}; // Interval for each strip

int vitesseUpAnim = 4;
int vitesseDownAnim = 4;
int fluiditeAnim = 6;
int delayGlobal = 11;
int allRedTime = 1500;
int RED = 2;
int blue = 0;

void setup() {
  FastLED.addLeds<WS2811, PIN, GRB>(leds, TOTAL_NUMPIXELS); // Initialize LEDs
  FastLED.setBrightness(255); // Set initial brightness
  FastLED.show(); // Show initial state
}

void loop() {
  unsigned long currentMillis = millis();
  for (int i = 0; i < NUM_STRIPS; i++) {
    // Code for each strip
    if (g[i] < vitesseDownAnim && !red[i]) {
      fillStrip(i, CRGB (0,255,  0));
      temp[i] = currentMillis;
      red[i] = true;
    }

    if (currentMillis - temp[i] > interval[i] && g[i] > y[i] - vitesseUpAnim && g[i] < y[i] + vitesseDownAnim) {
      int r = random(RED);
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

    fillStrip(i, CRGB(0,255, constrain(g[i] / fluiditeAnim,0,255)));
  }
  FastLED.show();
  delay(delayGlobal);
}

void fillStrip(int stripIndex, CRGB (color)) {
  int startIndex = stripIndex * NUMPIXELS_PER_STRIP;
  int endIndex = (stripIndex + 1) * NUMPIXELS_PER_STRIP;
  for (int i = startIndex; i < endIndex; i++) {
    leds[i] = color;
  }
}
