#include <IRremote.hpp>

// Broche du récepteur IR (TSOP)
const uint8_t irReceiverPin = 14;

void setup() {
  Serial.begin(115200);
  IrReceiver.begin(irReceiverPin, ENABLE_LED_FEEDBACK);  // Initialisation du récepteur IR
  Serial.println("Récepteur IR prêt !");
}

void loop() {
  // Vérification si un signal IR a été reçu
  if (IrReceiver.decode()) {
    // Affichage des informations du signal reçu
    Serial.print("Protocole : ");
    Serial.println(getProtocolString(IrReceiver.decodedIRData.protocol));
    Serial.print("Code : 0x");
    Serial.println(IrReceiver.decodedIRData.decodedRawData, HEX);
    Serial.print("Nombre de bits : ");
    Serial.println(IrReceiver.decodedIRData.numberOfBits);
    Serial.println("---");

    // Réinitialisation du récepteur pour le prochain signal
    IrReceiver.resume();
  }
  delay(100);  // Petite pause pour éviter de saturer le moniteur série
}
