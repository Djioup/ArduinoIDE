const int pinCapteurSon = A0; // Pin analogique pour le capteur de son
const int pinLED = 13; // Pin de la LED

int seuilSon = 455; // Seuil du capteur de son, à ajuster selon tes besoins

void setup() {
  pinMode(pinCapteurSon, INPUT); // Définir le pin du capteur de son en entrée
  pinMode(pinLED, OUTPUT); // Définir le pin de la LED en sortie
  Serial.begin(9600);
}

void loop() {
  Serial.print("Variable1:");
  Serial.println(analogRead(pinCapteurSon));
  int valeurSon = analogRead(pinCapteurSon); // Lire la valeur du capteur de son
  
  if (valeurSon > seuilSon) { // Si la valeur du son dépasse le seuil
    digitalWrite(pinLED, HIGH); // Allumer la LED
    Serial.println("ok");
  } else {
    digitalWrite(pinLED, LOW); // Éteindre la LED
  }
  
  delay(100); // Délai pour éviter les lectures trop rapides et stabiliser le processus
}
