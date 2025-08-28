#include <FastLED.h>

#define LED_PIN     9
#define NUM_LEDS    30
#define BRIGHTNESS  255
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

CRGB leds[NUM_LEDS];

// Variables for the moving turquoise-white effect
int position = 0;
int direction = 1;
int y;
int g;
uint8_t turquoiseMax = 128; // Maximum intensity for turquoise color

void setup() {
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  randomSeed(analogRead(0)); // Seed for randomness
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(0, 0, 255);
  }
}

void loop() {
  // Set all LEDs to blue
  
    
if (g == y)
{
y = random(0,255);
}

    if (g > y)
    {
      g -= 1;
    }
    else if (g < y)
    {
      g += 1;
    }
  

  // Create a moving turquoise to white effect
  for (int i = 0; i < 5; i++) {  // Adjust the length of the effect
    int pos = random(0,NUM_LEDS);
    leds[pos] = CRGB(0, g, 255); // Turquoise color
    //turquoiseMax = max(0, turquoiseMax - 25); // Gradually reduce the turquoise component
  }

  turquoiseMax = 128; // Reset turquoiseMax for next loop

  FastLED.show();
  delay(10);  // Adjust the delay to change the speed of the effect

  // Update position for the moving effect
  position += direction;
  if (position == NUM_LEDS || position == -1) {
    direction = -direction;
    position += direction;
  }
  }
