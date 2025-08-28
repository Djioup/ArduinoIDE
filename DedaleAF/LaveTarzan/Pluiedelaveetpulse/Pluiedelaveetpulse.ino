#include <Adafruit_NeoPixel.h>

#ifdef __AVR__
 #include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif

#define PIN        9 // Pin 6 for NeoPixels
#define NUMPIXELS 300 // Number of NeoPixels
#define NUM_DROPLETS 3 // Number of droplets

Adafruit_NeoPixel strip(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// Droplet parameters
int dropletSpeed = 1; // Speed at which the "droplets" move along the strip
int dropletLength = 30; // Length of the "droplets"
float dropletPhase[NUM_DROPLETS]; // Phase of the sine wave for "droplet" motion
int dropletPosition[NUM_DROPLETS]; // Initial position of each "droplet"
unsigned long lastDropletTime; // Time of last droplet start
unsigned long nextDropletInterval; // Time interval for the next droplet

// Moving light parameters
int g = 0; // Initial position of the moving light
int y, x, z; // Variables for controlling the movement of the light
bool redEffect; // Flag to indicate if the red effect is active

void setup() {
  strip.begin();
  strip.show();

  // Initialize random seed
  randomSeed(analogRead(0));

  // Initialize droplet positions and phases
  for (int i = 0; i < NUM_DROPLETS; i++) {
    dropletPosition[i] = 100 * (i + 1);
    dropletPhase[i] = 0.33 * (i + 1); // Random phase between 0 and 1
  }

  // Initialize variables for the moving light effect
  y = random(91);
  z = random(1, 4);
  x = random(1, 4);
  redEffect = false;

  // Initialize time variables for the droplets
  lastDropletTime = millis();
  nextDropletInterval = random(3000, 8000); // Random interval between 3 and 8 seconds
}

void loop() {
  updateDroplets(); // Update the droplets effect
  updateMovingLight(); // Update the moving light effect
}

void updateDroplets() {
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
      // For the first 13 LEDs, set them to yellow
      if (i - dropletPosition[j] < 13) {
        strip.setPixelColor(i % NUMPIXELS, strip.Color(255, 15, 0)); // Yellow color
      } else {
        // For the next LEDs, create a trail effect by reducing brightness gradually
        int brightness = 255 - (i - dropletPosition[j] - 13) * 30; // Gradually decrease brightness
        brightness = max(brightness, 0); // Ensure brightness doesn't go negative
        int green = min(brightness, 25); // Limit green component to a maximum value of 25
        strip.setPixelColor(i % NUMPIXELS, strip.Color(255, green, 0)); // Yellow color with varying brightness
      }
    }

    // Reset the droplet position when it reaches the end of the strip
    if (dropletPosition[j] >= NUMPIXELS) {
      dropletPosition[j] = 0;
      dropletPhase[j] = random(1000) / 1000.0; // Random phase between 0 and 1
    }
  }

  strip.show();

  // Check if it's time to start a new droplet
  if (millis() - lastDropletTime >= nextDropletInterval) {
    // Choose a random position for the new droplet
    dropletPosition[random(NUM_DROPLETS)] = 0;

    // Update time variables for the next droplet
    lastDropletTime = millis();
    nextDropletInterval = random(3000, 8000); // Random interval between 3 and 8 seconds
  }
}

void updateMovingLight() {
  if (g < 2 && !redEffect) {
    // If the moving light reaches the beginning and the red effect is not active, turn all LEDs red
    strip.fill(strip.Color(255, 0, 0), 0, NUMPIXELS);
    strip.show();
    redEffect = true;
    delay(random(1000)); // Delay for red effect
  }
  
  if (g > y - 2 && g < y + 2) {
    // If the moving light approaches the center, randomly reset its position and speed
    int r = random(2);
    if (r == 0) {
      y = random(91);
      z = random(1, 4);
      x = random(1, 4);
    } else { 
      y = 0;
      z = random(1, 4);
      x = random(1, 4);
      redEffect = false; // Deactivate the red effect when the moving light returns to the beginning
    }
  } else if (g < y) {
    // Move the light towards the center
    g += z;
    g = constrain(g, 0, 90); // Constrain the position within the strip
  } else if (g > y) {
    // Move the light towards the beginning
    g -= x;
    g = constrain(g, 0, 90); // Constrain the position within the strip
  }

  // Set the color of the moving light and update the strip
  strip.fill(strip.Color(255, g / 3, 0), 0, NUMPIXELS);
  strip.show();
  delay(50); // Adjust the delay to control the speed of the moving light
}
