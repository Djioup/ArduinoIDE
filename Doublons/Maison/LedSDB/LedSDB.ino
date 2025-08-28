#include <Adafruit_NeoPixel.h>

#define LED_PIN    6
#define NUM_LEDS   300
#define PIR_PIN    3 // Pin du capteur de présence

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

int a, b, c; // Variables pour les valeurs aléatoires
int d, e, f; // Variables pour les valeurs progressant vers les valeurs aléatoires
//int x,y,z;
unsigned long previousTime; // Temps précédent pour le changement de valeurs
unsigned long transitionTime; // Durée de la transition
bool transitionComplete = true; // Indicateur indiquant si la transition est terminée
bool OnPresence;
//int brightnessValue = 0; // Valeur de luminosité initiale
//int initialBrightness = 0;
unsigned long lastPresenceTime = 0; // Temps écoulé depuis la dernière détection de présence

void setup() {
  // Initialisation de la bande de LED
  strip.begin();
  //Serial.begin(9600);
  strip.show(); // Initialise toutes les LED à éteintes

  // Initialisation du capteur de présence
  pinMode(PIR_PIN, INPUT);
  pinMode(9, INPUT_PULLUP);
  //strip.setBrightness(255);

  // Initialisation de la génération de nombres aléatoires
  randomSeed(analogRead(0));

  // Initialisation des valeurs aléatoires
  //a = random(256);
  //b = random(256);
  //c = random(256);

  // Initialisation des valeurs de transition à 0
  d = e = f = 0;
}

void loop() {
//strip.show();
  // Vérifier si le capteur de présence détecte quelqu'un
  if (digitalRead(PIR_PIN) == HIGH || digitalRead(9) == LOW) {
    // Mettre à jour le temps de dernière détection de présence
    lastPresenceTime = millis();
    //Serial.println(digitalRead(9));
    // Augmenter la luminosité de 0 à 255 en 1 seconde
    //brightnessValue = map(1000, 0, 1000, 0, 255);
    //strip.setBrightness(brightnessValue);
    OnPresence = true;
    
  } else {
    // Si personne n'est détecté depuis plus de 10 secondes, diminuer la luminosité de 255 à 0 en 3 secondes
    if (millis() - lastPresenceTime > 30000) {
      OnPresence = false;
      //brightnessValue = map(3000, 0, 3000, 255, 0);  
    }
  }

  // Si la transition est terminée, générer de nouvelles valeurs aléatoires
  if (transitionComplete && OnPresence) {
    // Générer trois nouvelles valeurs aléatoires entre 0 et 255
    a = random(256);
    b = random(256);
    c = random(256);
    //brightnessValue = 255;

    // Choisir aléatoirement la durée de transition entre 10 et 30 secondes
    transitionTime = random(10000, 30000);

    // Réinitialiser les temps pour la transition
    previousTime = millis();
    transitionComplete = false;
    //strip.setBrightness(255);
  }

  if (transitionComplete && !OnPresence) {
    // Générer trois nouvelles valeurs aléatoires entre 0 et 255
    a = 0;
    b = 0;
    c = 0;
    d = 0;
    e = 0;
    f = 0;
    //brightnessValue = 0;
    // Choisir aléatoirement la durée de transition entre 10 et 30 secondes
    transitionTime = 500;

    // Réinitialiser les temps pour la transition
    previousTime = millis();
    transitionComplete = false;
    //strip.setBrightness(0);
  }

  // Temps écoulé depuis le début de la transition
  unsigned long elapsedTime = millis() - previousTime;
  //int brightness;
  //initialBrightness = strip.getBrightness();
  // Mettre à jour les valeurs en transition
  d = map(elapsedTime, 0, transitionTime, d, a);
  e = map(elapsedTime, 0, transitionTime, e, b);
  f = map(elapsedTime, 0, transitionTime, f, c);
  //brightness = map(elapsedTime, 0, 500,initialBrightness, brightnessValue);

  // Mettre à jour la couleur de toutes les LEDs
  //strip.setBrightness(brightness);
  uint32_t couleur = strip.Color(d, e, f);
  strip.fill(couleur, 0, NUM_LEDS);
  strip.show();
  

  // Vérifier si la transition est terminée
  if (elapsedTime >= transitionTime) {
    transitionComplete = true;
  }

  // Attendre un court instant
  //delay(100); // Vous pouvez ajuster ce délai pour augmenter la résolution de la transition
}
