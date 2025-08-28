// Définition de l'entrée analogique
#define ANALOG_IN_PIN A0
#define PinLED 11

// Floats pour la tension ADC et la tension d'entrée
float tension_adc = 0.0;
float tension_entree = 0.0;

// Floats pour les valeurs des résistances dans le diviseur (en ohms)
float R1 = 30000.0;
float R2 = 7500.0; 

// Float pour la tension de référence
float tension_reference = 5.0;

// Entier pour la valeur ADC
int valeur_adc = 0;

void setup(){
  // Configuration du Moniteur Série
  Serial.begin(9600);

  pinMode(PinLED, OUTPUT);
}

void loop(){
  // Lecture de l'entrée analogique
  valeur_adc = analogRead(ANALOG_IN_PIN);

  // Détermination de la tension à l'entrée de l'ADC
  tension_adc  = (valeur_adc * tension_reference) / 1024.0;

  // Calcul de la tension à l'entrée du diviseur
  tension_entree = tension_adc * (R1 + R2) / R2;

  // Affichage des résultats dans le Moniteur Série avec 2 décimales
  Serial.print("Tension d'Entrée = ");
  Serial.println(tension_entree, 2);

  if (tension_entree > 0.25)
digitalWrite(PinLED, HIGH);
else 
digitalWrite(PinLED, LOW);

  // Petite pause
  delay(100);
}