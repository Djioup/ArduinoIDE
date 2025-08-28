#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>

#define SCAN_TIME 2 // Temps de scan en secondes

BLEScan* pBLEScan;

void setup() {
  Serial.begin(115200);
  Serial.println("Initialisation du Bluetooth...");

  // Initialisation du Bluetooth
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); // Obtenir l'objet scan BLE
  pBLEScan->setActiveScan(true);   // Activer le scan actif pour des données détaillées
  pBLEScan->setInterval(100);      // Intervalle entre les scans
  pBLEScan->setWindow(99);         // Fenêtre de scan
}

void loop() {
  Serial.println("Scan BLE en cours...");

  // Utiliser un pointeur pour recevoir les résultats du scan
  BLEScanResults* scanResults = pBLEScan->start(SCAN_TIME, false);

  if (!scanResults) {
    Serial.println("Erreur lors du scan.");
    delay(2000);
    return;
  }

  // Chercher un beacon avec l'UUID spécifiée
  const char* targetUUID = "FDA50693-A4E2-4FB1-AFCF-C6EB07647825";
  bool found = false;

  for (int i = 0; i < scanResults->getCount(); i++) {
    BLEAdvertisedDevice device = scanResults->getDevice(i);

    // Vérifier les données fabricant
    if (device.haveManufacturerData()) {
      String manufacturerData = device.getManufacturerData(); // Utiliser String directement

      // Afficher les données fabricant pour analyse
      Serial.print("Données fabricant : ");
      for (size_t j = 0; j < manufacturerData.length(); j++) {
        Serial.printf("%02X ", manufacturerData[j]);
      }
      Serial.println();

      // Vérifier si l'UUID cible est dans les données fabricant
      if (manufacturerData.length() >= 25) { // Longueur minimale pour inclure l'UUID
        char uuid[37];
        snprintf(uuid, sizeof(uuid),
                 "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                 manufacturerData[4], manufacturerData[5], manufacturerData[6], manufacturerData[7], // Partie 1
                 manufacturerData[8], manufacturerData[9],                                           // Partie 2
                 manufacturerData[10], manufacturerData[11],                                         // Partie 3
                 manufacturerData[12], manufacturerData[13],                                         // Partie 4
                 manufacturerData[14], manufacturerData[15], manufacturerData[16], manufacturerData[17],
                 manufacturerData[18], manufacturerData[19]); // Partie 5

        Serial.print("UUID détecté : ");
        Serial.println(uuid);

        if (strcmp(uuid, targetUUID) == 0) {
          found = true;
          Serial.println("Beacon trouvé !");
          Serial.print("Adresse MAC : ");
          Serial.println(device.getAddress().toString().c_str());
          Serial.print("Nom : ");
          Serial.println(device.haveName() ? device.getName().c_str() : "Inconnu");
          Serial.print("RSSI : ");
          Serial.println(device.getRSSI());
          break; // Arrêter après avoir trouvé le beacon
        }
      }
    }
  }

  if (!found) {
    Serial.println("Beacon non trouvé.");
  }

  // Libérer les résultats du scan
  pBLEScan->clearResults();
  delay(500); // Pause avant le prochain scan
}
