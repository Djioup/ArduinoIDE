#include <Adafruit_NeoPixel.h>

#ifdef __AVR__
 #include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif

// Which pin on the Arduino is connected to the NeoPixels?
#define PIN        6 // On Trinket or Gemma, suggest changing this to 1

// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS 100 // Popular NeoPixel ring size

// Number of droplets
#define NUM_DROPLETS 3

// When setting up the NeoPixel library, we tell it how many pixels,
// and which pin to use to send signals. Note that for older NeoPixel
// strips you might need to change the third parameter -- see the
// strandtest example for more information on possible values.
Adafruit_NeoPixel strip(300, PIN, NEO_GRB + NEO_KHZ800);

int red = 255; // Red value for all LEDs
int yellow = 15; // Yellow value for the "droplet"
int maxGreen = 25; // Maximum value for the green component
int dropletSpeed = 1; // Speed at which the "droplet" moves along the strip
int dropletLength = 30; // Length of the "droplet"
float dropletPhase[NUM_DROPLETS]; // Phase of the sine wave for "droplet" motion
int dropletPosition[NUM_DROPLETS]; // Initial position of each "droplet"

void setup() {
  strip.begin();
  strip.clear();
  strip.show();
  
  // Initialize random seed
  randomSeed(analogRead(0));
  
  // Initialize droplet positions and phases
  for (int i = 0; i < NUM_DROPLETS; i++) {
    dropletPosition[i] = 100 * (i +1);
    dropletPhase[i] = 0,33 * (i +1); // Random phase between 0 and 1
  }
}

void loop() {
  // Set all LEDs to red
  for (int i = 0; i < NUMPIXELS; i++) {
    strip.setPixelColor(i, strip.Color(255, 0, 0));
  }

  // Update positions of each "droplet"
  for (int j = 0; j < NUM_DROPLETS; j++) {
    // Calculate the position of the "droplet" along the strip using a sine wave
    dropletPhase[j] += 0.01; // Adjust this value to change the speed of droplet oscillation
    dropletPosition[j] += dropletSpeed;

    // Set the "droplet" pixels to yellow
    for (int i = dropletPosition[j]; i < dropletPosition[j] + dropletLength; i++) {
      // For the first 3 LEDs, set them to yellow
      if (i - dropletPosition[j] < 13) {
        strip.setPixelColor(i % NUMPIXELS, strip.Color(255, yellow, 0)); // Yellow color
      } else {
        // For the next 7 LEDs, create a trail effect by reducing brightness gradually
        int brightness = 255 - (i - dropletPosition[j] - 13) * 30; // Gradually decrease brightness
        brightness = max(brightness, 0); // Ensure brightness doesn't go negative
        int green = min(brightness, maxGreen); // Limit green component to a maximum value of 25
        green = constrain(green, 0, 25);
        strip.setPixelColor(i % NUMPIXELS, strip.Color(255, green, 0)); // Yellow color with varying brightness
      }
    }
  }

  strip.show();

  // Reset the droplet positions when they reach the end of the strip
  for (int j = 0; j < NUM_DROPLETS; j++) {
    if (dropletPosition[j] >= NUMPIXELS) {
      dropletPosition[j] = 0; // Reset the droplet position
      dropletPhase[j] = random(1000) / 1000.0; // Random phase between 0 and 1
    }
  }

  delay(50); // Adjust delay for droplet speed
}
