#include <Adafruit_NeoPixel.h>

#define PIN        9
#define NUMPIXELS_PER_STRIP 60
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

void setup() {
  strip.begin();
  strip.show();
}

void loop() {
  unsigned long currentMillis = millis();

  for (int i = 0; i < NUM_STRIPS; i++) {
    // Code for each strip
    if (g[i] < 2 && !red[i]) {
      strip.fill(strip.Color(255, 0, 0), i * NUMPIXELS_PER_STRIP + 1,(i+1) * NUMPIXELS_PER_STRIP);
      strip.show();
      temp[i] = currentMillis;
      red[i] = true;
    }

    if (currentMillis - temp[i] > interval[i] && g[i] > y[i] - 2 && g[i] < y[i] + 2) {
      int r = random(2);
      if (r == 0) {
        y[i] = random(91);
        z[i] = random(1, 4);
        x[i] = random(1, 4);
      } else {
        y[i] = 0;
        z[i] = random(1, 4);
        x[i] = random(1, 4);
        red[i] = false;
      }
      interval[i] = random(2000);
      previousMillis[i] = currentMillis;
    } else if (g[i] < y[i]) {
      g[i] += z[i];
      g[i] = constrain(g[i], 0, 90);
    } else if (g[i] > y[i]) {
      g[i] -= x[i];
      g[i] = constrain(g[i], 0, 90);
    }

    strip.fill(strip.Color(255, g[i] / 3, 0), i * NUMPIXELS_PER_STRIP + 1, (i+1) *NUMPIXELS_PER_STRIP );
  }
  strip.show();
  delay(10);
}
