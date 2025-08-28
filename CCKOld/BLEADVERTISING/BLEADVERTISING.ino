#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>

#define SCAN_TIME 2  // Réduction de la durée du scan
BLEScan* pBLEScan;

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // Vérifie si l'appareil a un nom et correspond au format attendu
    if (advertisedDevice.haveName() && String(advertisedDevice.getName().c_str()).startsWith("ESP32:Player")) {
      Serial.println("Cible détectée !");
      
      // Récupère le RSSI
      Serial.print("RSSI : ");
      Serial.println(advertisedDevice.getRSSI());

      // Analyse des données du champ Manufacturer Data
      if (advertisedDevice.haveManufacturerData()) {
        String manufacturerData = advertisedDevice.getManufacturerData();
        Serial.print("Combined Data: ");
        Serial.println(manufacturerData);

        // Séparer les données combinées
        int playerIndex = manufacturerData.indexOf("P:");
        int etatIndex = manufacturerData.indexOf("E:");
        if (playerIndex != -1 && etatIndex != -1) {
          String player = manufacturerData.substring(playerIndex + 2, etatIndex - 1);
          String etat = manufacturerData.substring(etatIndex + 2);
          Serial.print("Player: ");
          Serial.println(player);
          Serial.print("Etat: ");
          Serial.println(etat);
        }
      }

      //pBLEScan->stop();  // Arrête le scan immédiatement après détection
      Serial.println("\n-------------------\n");
    }
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Initialisation du scanner BLE...");

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);  // Scan actif pour plus de données
  pBLEScan->setInterval(20);      // Réduit le temps entre scans (en ms)
  pBLEScan->setWindow(20);        // Augmente le temps d'écoute par canal (en ms)
}

void loop() {
  Serial.println("Scan BLE en cours...");
  pBLEScan->start(SCAN_TIME, false);  // Scan en arrière-plan pour SCAN_TIME secondes
  delay(100);  // Petite pause avant de recommencer le scan
}
