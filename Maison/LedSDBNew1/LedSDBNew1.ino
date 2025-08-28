#include <Adafruit_NeoPixel.h>

#define LED_PIN     6
#define NUMPIXELS   160
#define PIR_PIN     3       // Capteur de présence
#define BTN_PIN     9       // Bouton de test
int DELAY_MS = 30;      // Vitesse de l'effet

Adafruit_NeoPixel strip(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Position du point
int currentPixel = 0;

// Composantes RGB
int r, g, b;
// Incréments d’évolution
int dr, dg, db;

// Présence
unsigned long lastPresenceTime = 0;
bool onPresence = false;

// Effacement progressif
bool fadingOut = false;
uint8_t fadeLevel[NUMPIXELS]; // Opacité individuelle de chaque pixel

void setup() {
  strip.begin();
  strip.show();
  pinMode(PIR_PIN, INPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);
  randomSeed(analogRead(0));
  initNewColor();
  for (int i = 0; i < NUMPIXELS; i++) {
    fadeLevel[i] = 255;
  }
}

void loop() {
  // Détection de présence (capteur PIR ou bouton)
  if (digitalRead(PIR_PIN) == HIGH || digitalRead(BTN_PIN) == LOW) {
    lastPresenceTime = millis();
    if (!onPresence) {
      onPresence = true;
      startupFlash();
      initNewColor();
      //clearStrip();
    }
  } else {
    if (millis() - lastPresenceTime > 30000) {
      onPresence = false;
    }
  }

  if (onPresence) {
    // Affiche la couleur actuelle sur le pixel courant
    int prevPixel = (currentPixel == 0) ? NUMPIXELS - 1 : currentPixel - 1;
    strip.setPixelColor(prevPixel, r, g, b);
    fadeLevel[prevPixel] = 255;
    strip.setPixelColor(currentPixel, 255, 255, 255);
    fadeLevel[currentPixel] = 255;
    strip.show();
    int vit = random(-3,4);
    DELAY_MS += vit;
    int NewVit = constrain(DELAY_MS,10,40);
    delay(NewVit);

    // Avance le pixel
    currentPixel++;

    // Évolution douce des couleurs
    r = constrain(r + dr, 0, 255);
    g = constrain(g + dg, 0, 255);
    b = constrain(b + db, 0, 255);

    int totalBrightness = r + g + b;
    if (totalBrightness <= 60)
    {
      dr = random(0, 6);
      dg = random(0, 6);
      db = random(0, 6);
    }
    else if (totalBrightness >= 60)
    {
      dr += random(-1,2);
      dg += random(-1,2);
      db += random(-1,2);
    }

    // Si on boucle, on recommence une nouvelle séquence
    if (currentPixel >= NUMPIXELS) {
      currentPixel = 0;
      //initNewColor();
      changeColor();
    }
  } else {
    // Effacement progressif
    bool stillLit = false;
    for (int i = 0; i < NUMPIXELS; i++) {
      if (fadeLevel[i] > 0) {
        fadeLevel[i] = max(0, fadeLevel[i] - 5);
        uint32_t col = strip.getPixelColor(i);
        uint8_t r = (col >> 16) & 0xFF;
        uint8_t g = (col >> 8) & 0xFF;
        uint8_t b = col & 0xFF;
        r = (r * fadeLevel[i]) / 255;
        g = (g * fadeLevel[i]) / 255;
        b = (b * fadeLevel[i]) / 255;
        strip.setPixelColor(i, strip.Color(r, g, b));
        stillLit = true;
      }
    }
    strip.show();
    delay(30);

    if (!stillLit) {
      currentPixel = 0;
      clearStrip();
    }
  }
}

// Initialise une nouvelle couleur et ses variations
void initNewColor() {
  r = random(50, 200);
  g = random(50, 200);
  b = random(50, 200);
  dr = random(-5, 6);
  dg = random(-5, 6);
  db = random(-5, 6);
}

void changeColor()
{
  dr = random(-5, 6);
  dg = random(-5, 6);
  db = random(-5, 6);
}

// Éteint toutes les LEDs
void clearStrip() {
  for (int i = 0; i < NUMPIXELS; i++) {
    strip.setPixelColor(i, 0);
    fadeLevel[i] = 0;
  }
  strip.show();
}

void startupFlash() {
  for (int i = 0; i < NUMPIXELS; i++) {
    strip.setPixelColor(i, 255, 255, 255);
    fadeLevel[i] = 255;
  }
  strip.show();
  delay(500); // durée du flash blanc avant de commencer le cycle
}