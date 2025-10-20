#include <Arduino.h>
#include "ESP32BleAdvertise.h"

SimpleBLE ble;  // <-- instance globale

// Nom BLE (doit commencer par "Munition" pour être détecté par la lanterne)
String BLENAME = "Munition1";

void setup() {
  Serial.begin(115200);
  ble.begin(BLENAME);          // initialise BLE + nom
  Serial.println("Borne Munition BLE active !");
}

void loop() {
  // Données additionnelles (optionnelles)
  String data = "Munition";
  ble.advertise(data);         // envoie périodiquement le paquet

  delay(1000);                 // 2 s entre pubs = suffisant
}
