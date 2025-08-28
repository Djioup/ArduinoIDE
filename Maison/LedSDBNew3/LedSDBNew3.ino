#include <Adafruit_NeoPixel.h>

#define LED_PIN     6
#define NUMPIXELS   160
#define PIR_PIN     3
#define BTN_PIN     9
#define DELAY_MS    30

Adafruit_NeoPixel strip(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

uint8_t r[NUMPIXELS], g[NUMPIXELS], b[NUMPIXELS];
int8_t dr[NUMPIXELS], dg[NUMPIXELS], db[NUMPIXELS];
uint8_t fadeLevel[NUMPIXELS];

unsigned long lastPresenceTime = 0;
bool onPresence = false;

void setup() {
  strip.begin();
  strip.show();
  pinMode(PIR_PIN, INPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);
  randomSeed(analogRead(0));

  initPixels();
  startupFlash();
}

void loop() {
  // Présence
  if (digitalRead(PIR_PIN) == HIGH || digitalRead(BTN_PIN) == LOW) {
    lastPresenceTime = millis();
    if (!onPresence) {
      onPresence = true;
      initPixels();
      startupFlash();
    }
  } else if (millis() - lastPresenceTime > 30000) {
    onPresence = false;
  }

  if (onPresence) {
    for (int i = 0; i < NUMPIXELS; i++) {
      // Moyenne simple avec les voisins (1D blur)
      int prev = (i == 0) ? NUMPIXELS - 1 : i - 1;
      int next = (i == NUMPIXELS - 1) ? 0 : i + 1;

      r[i] = (r[prev] + r[i] + r[next]) / 3;
      g[i] = (g[prev] + g[i] + g[next]) / 3;
      b[i] = (b[prev] + b[i] + b[next]) / 3;

      // Ajout d’un bruit doux
      if (random(0, 100) < 10) dr[i] = random(-2, 3);
      if (random(0, 100) < 10) dg[i] = random(-2, 3);
      if (random(0, 100) < 10) db[i] = random(-2, 3);

      r[i] = constrain(r[i] + dr[i], 0, 255);
      g[i] = constrain(g[i] + dg[i], 0, 255);
      b[i] = constrain(b[i] + db[i], 0, 255);

      // Anti-noir
      if (r[i] + g[i] + b[i] < 60) {
        r[i] += 1; g[i] += 1; b[i] += 1;
      }

      fadeLevel[i] = 255;
      strip.setPixelColor(i, r[i], g[i], b[i]);
    }

    strip.show();
    delay(DELAY_MS);

  } else {
    bool stillLit = false;
    for (int i = 0; i < NUMPIXELS; i++) {
      if (fadeLevel[i] > 0) {
        fadeLevel[i] = max(0, fadeLevel[i] - 5);
        uint8_t fr = (r[i] * fadeLevel[i]) / 255;
        uint8_t fg = (g[i] * fadeLevel[i]) / 255;
        uint8_t fb = (b[i] * fadeLevel[i]) / 255;
        strip.setPixelColor(i, fr, fg, fb);
        stillLit = true;
      }
    }
    strip.show();
    delay(30);
    if (!stillLit) initPixels();
  }
}

void initPixels() {
  for (int i = 0; i < NUMPIXELS; i++) {
    r[i] = random(50, 200);
    g[i] = random(50, 200);
    b[i] = random(50, 200);
    dr[i] = random(-1, 2);
    dg[i] = random(-1, 2);
    db[i] = random(-1, 2);
    fadeLevel[i] = 255;
  }
}

void startupFlash() {
  for (int i = 0; i < NUMPIXELS; i++) {
    strip.setPixelColor(i, 255, 255, 255);
    fadeLevel[i] = 255;
  }
  strip.show();
  delay(500);
}
