#include <FastLED.h>

#define LED_PIN    6
#define LED_COUNT  160

CRGB leds[LED_COUNT];

int r;
int g;
int b;
int x;
int y;
int z;
int d;
bool OnPresence;
unsigned long LastPresence;

void setup() {
  pinMode(3, INPUT);
  pinMode(9, INPUT_PULLUP);

  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_COUNT);

  Serial.begin(9600);
  randomSeed(analogRead(0));

  x = random(2551);
  y = random(2551);
  z = random(2551);
}

void loop() {
  if (digitalRead(3) == HIGH || digitalRead(9) == LOW) {
    if (!OnPresence) {
      x = 2550;
      y = 2550;
      z = 2550;
      d = 24;
    }
    OnPresence = true;
    LastPresence = millis();
  }

  if (digitalRead(3) == LOW && digitalRead(9) == HIGH && OnPresence && millis() - LastPresence > 10000) {
    OnPresence = false;
    x = 0;
    y = 0;
    z = 0;
  }

  if (r > x) {
    r -= 1;
  } else if (r < x) {
    r += 1 + d;
    r = constrain(r, 0, 2550);
  } else if (r == x && OnPresence) {
    int alea = random(4);
    if (alea == 0) {
      x = 0;
    } else {
      x = random(2551);
      d = 0;
    }
  }

  if (g > y) {
    g -= 1;
  } else if (g < y) {
    g += 1 + d;
    g = constrain(g, 0, 2550);
  } else if (g == y && OnPresence) {
    int alea = random(4);
    if (alea == 0) {
      y = 0;
    } else {
      y = random(2551);
      d = 0;
    }
  }

  if (b > z) {
    b -= 1;
  } else if (b < z) {
    b += 1 + d;
    b = constrain(b, 0, 2550);
  } else if (b == z && OnPresence) {
    int alea = random(4);
    if (alea == 0) {
      z = 0;
    } else {
      z = random(2551);
      d = 0;
    }
  }

  for (int i = 0; i < LED_COUNT; i++) {
    leds[i] = CRGB(r / 10, g / 10, b / 10);
  }

  FastLED.show();
  delay(10);
}
