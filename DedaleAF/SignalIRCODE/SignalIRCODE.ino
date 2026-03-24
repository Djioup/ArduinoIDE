#include <IRremote.hpp>

// Broche de la LED IR
const uint8_t irLedPin = 13;

void setup() {
  Serial.begin(115200);
  IrSender.begin(irLedPin);  // Initialisation de l'émetteur IR
  Serial.println("Émetteur IR prêt !");
}

void loop() {
  // Exemple : Envoyer le code NEC pour "Power On" (remplacez par votre code)
  uint32_t necCode = 0x00FF00FF;  // Exemple de code NEC (à adapter)

  Serial.print("Envoi du signal NEC : 0x");
  Serial.println(necCode, HEX);

  IrSender.sendNEC(necCode, 32);  // Envoi du code NEC sur 32 bits

  delay(5000);  // Attendre 5 secondes avant de renvoyer le signal
}
