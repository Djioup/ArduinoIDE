#include <Adafruit_NeoPixel.h>

#define LED_PIN    6
#define LED_COUNT  300

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

int r;
int g;
int b;
int x;
int y;
int z;
bool OnPresence;
unsigned long LastPresence;

void setup() {
  pinMode(3, INPUT);
  pinMode(9, INPUT_PULLUP);

  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  Serial.begin(9600);
  randomSeed(analogRead(0));

  x = random(256);
  y = random(256);
  z = random(256);
}

void loop() {
  Serial.print("Variable1: ");
  Serial.print(r);
  Serial.print(" Variable2: ");
  Serial.print(g);
  Serial.print(" Variable3: ");
  Serial.println(b);

  for (int i = 0; i < LED_COUNT; i++) {
    if (r > x) {
    r -= 1;
  }
  else if (r < x) {
    r += 1;
  }
  else if (r == x && OnPresence) {
    x = random(256);
  }

  if (g > y) {
    g -= 1;
  }
  else if (g < y) {
    g += 1;
  }
  else if (g == y && OnPresence) {
    y = random(256);
  }

  if (b > z) {
    b -= 1;
  }
  else if (b < z) {
    b += 1;
  }
  else if (b == z && OnPresence) {
    z = random(256);
  }

    strip.setPixelColor(i, strip.Color(r, g, b));
    strip.show();    
  }  

  if (digitalRead(3) == HIGH || digitalRead(9) == LOW) {
    OnPresence = true;
    LastPresence = millis();
  }

  if (digitalRead(3) == LOW && digitalRead(9) == HIGH && OnPresence && millis() - LastPresence > 10000) {
    OnPresence = false;
    x = 0;
    y = 0;
    z = 0;
  }  
}
