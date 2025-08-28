#include <Adafruit_NeoPixel.h>
#define LED_PIN    6
#define LED_COUNT  600

bool OnPresence;
unsigned long Top;
unsigned long LastTop;
unsigned long FirstTop;
unsigned long delai;
//unsigned long AnimTop;
//uint32_t color;
Adafruit_NeoPixel strip(LED_COUNT,LED_PIN, NEO_GRB + NEO_KHZ800);

int initialBrightness = 0;
int Brightness = 0;
int brightness = 0;
int a = 0;
int b = 0;
int c = 0;
int d = 0;
int e = 0;
int f = 0;
int x = 0;
int y = 0;
int z = 0;
uint8_t red;
uint8_t green;
uint8_t blue;

/*struct LEDAnimation {
  uint16_t index; // Index de la LED dans la bande
  uint32_t start_time; // Heure de début de l'animation
  bool fading_in; // Indique si la LED est en train de s'allumer ou de s'éteindre
};

// Tableau pour stocker les informations sur les LED en cours d'animation
LEDAnimation animations[40];*/

void setup() {
  // put your setup code here, to run once:
pinMode(3, INPUT);
pinMode(2, INPUT);
strip.begin ();
strip.show ();
}

void loop() {
  // put your main code here, to run repeatedly:
   
  //randomSeed(analogRead(0));

  /*updateAnimations();

  // Vérifier si une nouvelle LED doit être sélectionnée
  if (millis() % 10000 < 100) { // Changez 10000 à la valeur souhaitée pour la durée entre chaque sélection de LED
    selectNewLED();
  }*/

if (analogRead(0) < 200 && !OnPresence)
{
  a = 0;
  b = 0;
  c = 150;
  brightness = 100;
}

else if (analogRead(0) >= 200 && analogRead(0) < 500 && !OnPresence)
{
  a = 0;
  b = 50;
  c= 255;
  brightness = 200;
}

else if (analogRead(0) >= 500 && !OnPresence)
{
  a = 255;
  b = 75;
  c = 0;
  brightness = 255;
}

if (digitalRead(3) == HIGH ||digitalRead(2) == HIGH)
{
  OnPresence = true;
  Top = millis();
}

if (digitalRead(3) == LOW && digitalRead(2) == LOW && OnPresence && millis() - Top > 10000)
{
  OnPresence = false;
  LastTop = millis();
}

if (!OnPresence)
{   
// Extraire les composantes de couleur RVB
  FirstTop = millis ();
  delai = millis () - LastTop;
  initialBrightness = strip.getBrightness();
  if (delai < 2000)
  {
  d = map(delai, 0, 2000, x, 0);
  e = map(delai, 0, 2000, y, 0);
  f = map(delai, 0, 2000, z, 0);
  Brightness = map(delai, 0, 2000, initialBrightness, 0);
  strip.setBrightness(Brightness);
  strip.fill(strip.Color(d,e,f),0, 600);
  strip.show ();
  }
}

if (OnPresence)
{  
  delai = millis () - FirstTop;
  initialBrightness = strip.getBrightness();
  /*for (int i = 0; i < LED_COUNT; i +=2)
  {    
    uint32_t color = strip.getPixelColor(i); // Récupérer la couleur du pixel

// Extraire les composantes de couleur RVB
    uint8_t red = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8) & 0xFF;
    uint8_t blue = color & 0xFF;
    int d = map(delai, 0, 2000, red, a);
    int e = map(delai, 0, 2000, green, b);
    int f = map(delai, 0, 2000, blue, c);
    strip.setPixelColor(i,d, e, f);
  }*/

// Extraire les composantes de couleur RVB
if (delai < 2000)
{
    x = map(delai, 0, 2000, d, a);
    y = map(delai, 0, 2000, e, b);
    z = map(delai, 0, 2000, f, c);
    Brightness = map (delai, 0, 2000, initialBrightness,brightness);
    strip.setBrightness (Brightness);
    strip.fill(strip.Color(x,y,z),0, 600);
    strip.show ();
}

  /*for (int i = 0; i < 30; i++) {
    if (millis () - AnimTop > random(0,1500))
    {
    AnimTop = millis ();
    uint32_t color = strip.getPixelColor(i); // Récupérer la couleur du premier pixel

// Extraire les composantes de couleur RVB
    uint8_t red = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8) & 0xFF;
    uint8_t blue = color & 0xFF;
    int g = map (delai, 0, 5000, red, 0);
    int t = map (delai, 0, 5000, green, 0);
    int o = map (delai, 0, 5000, blue, 120);
    int randomLED = random(0,600); // Choisir une LED parmi toutes les LED
    strip.setPixelColor(randomLED, g, t, o);
    }
  }*/
 }
}

/*void updateAnimations() {
  unsigned long current_time = millis();
  for (int i = 0; i < 40; i++) {
    // Vérifier si une LED est en cours d'animation
    if (animations[i].index != 0xFFFF) {
      // Calculer le temps écoulé depuis le début de l'animation
      unsigned long elapsed_time = current_time - animations[i].start_time;
      
      // Mettre à jour la couleur de la LED en fonction du temps écoulé
      uint8_t brightness = 0;
      if (animations[i].fading_in) {
        brightness = map(constrain(elapsed_time, 0, 5000), 0, 5000, 0, 120);
      } else {
        brightness = map(constrain(elapsed_time, 0, 5000), 0, 5000, 120, 0);
      }
      strip.setPixelColor(animations[i].index, 0, 0, brightness);

      // Vérifier si l'animation est terminée
      if (elapsed_time >= 10000) { // Changez 10000 à la valeur souhaitée pour la durée totale de l'animation
        animations[i].index = 0xFFFF; // Marquer l'animation comme terminée
      }
    }
  }
  //strip.show();
}

void selectNewLED() {
  // Générer un nouvel index de LED
  uint16_t new_index = random(LED_COUNT);

  // Rechercher une place libre dans le tableau d'animations
  for (int i = 0; i < 40; i++) {
    if (animations[i].index == 0xFFFF) {
      // Stocker les informations sur la nouvelle LED
      animations[i].index = new_index;
      animations[i].start_time = millis();
      animations[i].fading_in = true;
      return;
    }
  }
}*/



