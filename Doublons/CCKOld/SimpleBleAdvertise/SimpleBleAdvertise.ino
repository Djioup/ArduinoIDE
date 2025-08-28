#include "ESP32BleAdvertise.h"
SimpleBLE ble;

void setup() {
    Serial.begin(115200);
    ble.begin("ESP32:Player1");  //sets the device name
}

void loop() {
    String Player = String(1);
    String Etat = String(random(0, 2));
    
    // Combine Player et Etat dans une seule chaîne
    String combinedData = "P:" + Player + ";E:" + Etat;
    
    // Utilisez un seul appel pour diffuser les données combinées
    ble.advertise(combinedData);
    delay(1000);
}
