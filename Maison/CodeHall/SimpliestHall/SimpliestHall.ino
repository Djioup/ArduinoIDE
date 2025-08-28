#include <FastLED.h>

#define LED_PIN     6
#define LED_COUNT  100
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB

CRGB leds[LED_COUNT];

int r = 0;
int g = 0;
int b = 0;
int x = 0;
int y = 0;
int z = 0;
int r1, y1, z1;

unsigned long Top;
bool OnPresence;

const int numReadings = 100;
int readings[numReadings];
int total = 0;
int average = 0;

void fillstrip(int red, int green, int blue) {
  for (int i = 0; i < LED_COUNT; i++) {
    leds[i] = CRGB(red, green, blue);
  }
  FastLED.show();
}

void setup() {
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, LED_COUNT);
  FastLED.show();
  Serial.begin(9600);

  pinMode(3, INPUT);
  pinMode(2, INPUT);
}

void loop() {
  Serial.println(average);
for (int i = 0; i < numReadings; i++) {
  total -= readings[i];
    readings[i] = analogRead(A0); // Lire la valeur du capteur
    total += readings[i]; // Ajouter la lecture à la somme
  }

  // Calcul de la moyenne
  average = abs(total / (float)numReadings);

if (!OnPresence && r == 0 && g == 0 && b == 0)
{
  FastLED.clear(); // Turn off all LEDs
  FastLED.show();  // Apply the changes
}
  // Update LED color based on the average light reading
  if (!OnPresence && average > 280 && r == 0 && g == 0 && b == 0) {
    x = 0;
    y = 50;
    z = 0;
    r1 = 1;
    y1 = 2;
    z1 = 1;
  }

  if (!OnPresence && average > 220 && average <= 280 && r == 0 && g == 0 && b == 0) {
    x = 50;
    y = 150;
    z = 0;
    r1 = 1;
    y1 = 3;
    z1 = 1;
  }

  if (!OnPresence && average > 160 && average <= 220 && r == 0 && g == 0 && b == 0) {
    x = 100;
    y = 250;
    z = 12;
    r1 = 2;
    y1 = 5;
    z1 = 1;
  }

  if (!OnPresence && average > 100 && average <= 160 && r == 0 && g == 0 && b == 0) {
    x = 250;
    y = 250;
    z = 150;
    r1 = 5;
    y1 = 5;
    z1 = 3;
  }

  if (!OnPresence && average <= 100 && r == 0 && g == 0 && b == 0) {
    x = 0;
    y = 0;
    z = 0;
    r1 = 1;
    y1 = 1;
    z1 = 2;
  }

  if (digitalRead(3) == HIGH || digitalRead(2) == HIGH) {
    OnPresence = true;
    Top = millis();
  } else if (digitalRead(3) == LOW && digitalRead(2) == LOW && millis() - Top > 8000) {
    OnPresence = false;
  }

  if (OnPresence && (r != x || g != y || b != z)) {
    if (r > x) {
      r -= r1;
      r = constrain(r, x, r);
    } else if (r < x) {
      r += r1;
      r = constrain(r, r, x);
    }

    if (g > y) {
      g -= y1;
      g = constrain(g, y, g);
    } else if (g < y) {
      g += y1;
      g = constrain(g, g, y);
    }

    if (b > z) {
      b -= z1;
      b = constrain(b, z, b);
    } else if (b < z) {
      b += z1;
      b = constrain(b, b, z);
    }

    fillstrip(r, g, b);
  }

  if (!OnPresence && (r != 0 || g != 0 || b != 0)) {
    if (r > 0) {
      r -= r1;
      r = constrain(r, 0, 255);
    }

    if (g > 0) {
      g -= y1;
      g = constrain(g, 0, 255);
    }

    if (b > 0) {
      b -= z1;
      b = constrain(b, 0, 255);
    }

    fillstrip(r, g, b);
  }

  delay(20);
}
