#include <Adafruit_NeoPixel.h>

#define LED_PIN     6
#define NUMPIXELS   160
#define PIR_PIN     3       // Capteur de présence
#define BTN_PIN     9       // Bouton de test
int DELAY_MS = 30;      // Vitesse de l'effet

void initNewColor();
void changeColor();
void clearStrip();
void startupFlash();

Adafruit_NeoPixel strip(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Position du point
int currentPixel = 0;

// Composantes RGB
int rH, gH, bH;
// Incréments d’évolution
int drH, dgH, dbH;

// Données par pixel
uint8_t r[NUMPIXELS], g[NUMPIXELS], b[NUMPIXELS];      // valeurs 0–255
int8_t dr[NUMPIXELS], dg[NUMPIXELS], db[NUMPIXELS];    // variations -5 à +5

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
    strip.setPixelColor(prevPixel, rH, gH, bH);
    r[prevPixel] = rH;
    g[prevPixel] = gH;
    b[prevPixel] = bH;
    fadeLevel[prevPixel] = 255;
    strip.setPixelColor(currentPixel, 255, 255, 255);
    fadeLevel[currentPixel] = 255;
    strip.show();
    int vit = random(-3,4);
    DELAY_MS += vit;
    int NewVit = constrain(DELAY_MS,10,45);
    delay(NewVit);

    // Avance le pixel
    currentPixel++;

    // Évolution douce des couleurs
    rH = constrain(rH + drH, 0, 255);
    gH = constrain(gH + dgH, 0, 255);
    bH = constrain(bH + dbH, 0, 255);

    int totalBrightness = rH + gH + bH;
    if (totalBrightness <= 60)
    {
      drH = random(0, 6);
      dgH = random(0, 6);
      dbH = random(0, 6);
    }
    else if (totalBrightness >= 60)
    {
      drH += random(-1,2);
      dgH += random(-1,2);
      dbH += random(-1,2);
    }

    // Si on boucle, on recommence une nouvelle séquence
    if (currentPixel >= NUMPIXELS) {
      currentPixel = 0;
      //initNewColor();
      changeColor();
    }

    for(int i=0; i < NUMPIXELS; i++)
    {
  if (random(0, 100) < 5) dr[i] = random(-10, 11);
  if (random(0, 100) < 5) dg[i] = random(-10, 11);
  if (random(0, 100) < 5) db[i] = random(-10, 11);
    r[i] = constrain(r[i] + dr[i], 0, 255);
    g[i] = constrain(g[i] + dg[i], 0, 255);
    b[i] = constrain(b[i] + db[i], 0, 255);

    int brightness = r[i] + g[i] + b[i];
    if (brightness < 60) {
      r[i] = constrain(r[i] + 1, 0, 255);
      g[i] = constrain(g[i] + 1, 0, 255);
      b[i] = constrain(b[i] + 1, 0, 255);
    }

  }
  } else {
    // Effacement progressif
    bool stillLit = false;
    for (int i = 0; i < NUMPIXELS; i++) {
      if (fadeLevel[i] > 0) {
        fadeLevel[i] = max(0, fadeLevel[i] - 5);
        uint32_t col = strip.getPixelColor(i);
        uint8_t rF = (col >> 16) & 0xFF;
        uint8_t gF = (col >> 8) & 0xFF;
        uint8_t bF = col & 0xFF;
        rF = (rF * fadeLevel[i]) / 255;
        gF = (gF * fadeLevel[i]) / 255;
        bF = (bF * fadeLevel[i]) / 255;
        strip.setPixelColor(i, strip.Color(rF, gF, bF));
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
  rH = random(50, 200);
  gH = random(50, 200);
  bH = random(50, 200);
  drH = random(-5, 6);
  dgH = random(-5, 6);
  dbH = random(-5, 6);

    for(int i=0; i < NUMPIXELS; i++)
    {
    dr[i] = random(-10, 11);
    dg[i] = random(-10, 11);
    db[i] = random(-10, 11);
    }
}

void changeColor()
{
  drH = random(-5, 6);
  dgH = random(-5, 6);
  dbH = random(-5, 6);
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

