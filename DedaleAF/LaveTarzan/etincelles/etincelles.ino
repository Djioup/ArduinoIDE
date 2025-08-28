#include <Adafruit_NeoPixel.h>

#ifdef __AVR__
 #include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif

#define PIN        6
#define NUMPIXELS_PER_STRIP 20
#define NUM_STRIPS 5
#define TOTAL_NUMPIXELS (NUMPIXELS_PER_STRIP * NUM_STRIPS)

Adafruit_NeoPixel strip(TOTAL_NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

int animationDuration = 2000;
int alea;
bool newAlea;
unsigned long timer;

int g,y,z;

void setup() {
  strip.begin();
  strip.show();
}

void loop() {

  if (newAlea)
  {
  
    timer = millis ();
    newAlea = false;
    
  }

  if (millis () - timer > random (300,1500) && !newAlea)
  { 
    alea = random(TOTAL_NUMPIXELS); 
    newAlea = true;  
  }

    for (int i = 1; i % TOTAL_NUMPIXELS; i++) {
      uint32_t color;
   color = strip.getPixelColor(i);
          int8_t green = (color >> 8) & 0xFF;
     if (i >= alea && i < alea + 60) {
        if (abs(i - alea) < 30) {
          
          color = strip.Color(255, constrain(green+=1,0,abs(i - alea)), 0);
        } else if (abs(i - alea) > 30 && abs(i - alea) < 60) {
          
          color = strip.Color(255, constrain(green+=1,0,abs((alea + 60) - i)), 0);
        }
      } else {
        
        if (green != 0)
        color = strip.Color(255, green -=1, 0);
        else
        color = strip.Color(255,0,0);
      }
      strip.setPixelColor(i, color);
      
    }

 


    
    strip.show();
    delay(20);
  }

