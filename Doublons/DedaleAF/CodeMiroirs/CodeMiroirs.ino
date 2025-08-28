// Pin de la photoresistance
const int photoresistorPin = A1;

// Pin de la LED
const int ledPin = 12;

// Seuil de luminosité pour allumer la LED
const int seuilLuminosite = 115;

void setup() {
  // Initialisation de la communication série
  Serial.begin(9600);

  // Configuration des broches
  pinMode(photoresistorPin, INPUT);
  pinMode(ledPin, OUTPUT);
}

void loop() {
  // Lecture de la valeur de la photoresistance
  int valeurLuminosite = analogRead(photoresistorPin);

  // Affichage de la valeur de luminosité (facultatif)
  //Serial.print("Luminosite : ");
  //Serial.println(valeurLuminosite);

  // Vérification si la luminosité est en dessous du seuil
  if (valeurLuminosite > seuilLuminosite) {
    // Allumer la LED
    digitalWrite(ledPin, HIGH);
  } else {
    // Éteindre la LED
    digitalWrite(ledPin, LOW);
  }

  // Attendre un court laps de temps
  delay(200);
}
