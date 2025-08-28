#include <FastLED.h>

#define LED_PIN     9
#define NUM_LEDS    30
#define BRIGHTNESS  255
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

CRGB leds[NUM_LEDS];

// Variables for the pulse effect
uint8_t pulseSpeed = 3;  // Smaller values for slower pulse
uint8_t minBrightness = 4; // Minimum brightness
uint8_t maxBrightness = 255; // Maximum brightness
int8_t brightnessDirection = 1;
uint8_t brightness = minBrightness;

void setup() {
  Serial.begin(9600); // Initialiser la communication série pour le débogage
  Serial.println("Initialisation...");
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  Serial.println("Initialisation terminée.");
}

void loop() {
  // Update brightness for pulse effect
  brightness += brightnessDirection * pulseSpeed;
  if (brightness >= maxBrightness || brightness <= minBrightness) {
    brightnessDirection = -brightnessDirection;  // Change direction
    brightness += brightnessDirection * pulseSpeed;
  }

  // Update LEDs
  for(int i = 0; i < NUM_LEDS; i++) {
     leds[i] = CRGB(brightness, 0, 0);  // Red with pulsing brightness
  }

  FastLED.show();
  delay(20);  // Adjust the delay to change the pulse speed

  // Debugging output
  Serial.print("Current brightness: ");
  Serial.println(brightness);
}
