#include <Adafruit_NeoPixel.h>

#define LED_PIN    6
#define LED_COUNT  100

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

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

  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  Serial.begin(9600);
  randomSeed(analogRead(0));

  x = random(2551);
  y = random(2551);
  z = random(2551);
}

void loop() {
  if (digitalRead(3) == HIGH || digitalRead(9) == LOW) {
    if (!OnPresence)
    {
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
  }
  else if (r < x) {
    r += 1 + d;
    r = constrain(r, 0, 2550);
  }
  else if (r == x && OnPresence) {
    int alea = random (4);
    if (alea == 0)
    {
      x = 0;
    }
    else if (alea != 0)
    {
    x = random(2551);
    d = 0;
    }
  }

  if (g > y) {
    g -= 1;
  }
  else if (g < y) {
    g += 1 + d;
    g = constrain(g, 0, 2550);
  }
  else if (g == y && OnPresence) {
    int alea = random (4);
    if (alea == 0)
    {
      y = 0;
    }
    else if (alea != 0)
    {
    y = random(2551);
    d = 0;
    }
  }

  if (b > z) {
    b -= 1;
  }
  else if (b < z) {
    b += 1 + d;
    b = constrain(b, 0, 2550);
  }
  else if (b == z && OnPresence) {
    int alea = random (4);
    if (alea == 0)
    {
      z = 0;
    }
    else if (alea != 0)
    {
    z = random(2551);
    d = 0;
    }
  }
  uint32_t color = strip.Color((r/10),(g/10),(b/10));
  strip.fill(color, 0, 300);
  strip.show();
delay(10);
}
