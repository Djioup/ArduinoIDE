#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_NeoPixel.h>

// Informations Wi-Fi
const char* ssid = "PelucheGang";
const char* password = "CACHE-CACHEKILLER";
const char* mqtt_server = "192.168.0.139";
const char* mqtt_user = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";
const char* esp32_id = "Generator0";

// WiFi et MQTT
WiFiClient espClient;
PubSubClient client(espClient);
BLEScan* pBLEScan;
#define SCAN_TIME 5

// DFPlayer Mini Configuration
#define RX_PIN 16
#define TX_PIN 17
DFRobotDFPlayerMini myDFPlayer;
// Capteur et LEDs
const int MAG_SENSOR_1 = 27;
const int MAG_SENSOR_2 = 26;
const int GENERATOR_BUTTON_PIN = 23;
const int LED_BUTTON_PIN = 22;
const int LED_DECORATIVE_PIN = 21;
const int LED_DECORATIVE_PIN2 = 19;
const int LED_DECORATIVE_PIN3 = 18;
const int LED_DECORATIVE_PIN4 = 33;
#define ANNEAULED 13
#define NUM_LEDS 24
Adafruit_NeoPixel ring(NUM_LEDS, ANNEAULED, NEO_GRB + NEO_KHZ800);

unsigned long lastBlinkTime = 0;
bool blinkState = false;
const int blinkInterval = 300;  // millisecondes

bool isLooped = false;
bool JustOnce = false;
const int NbrTour = 2;
int SpottedPlayer = 0;
float diffLvl = 1;
unsigned long ShockTime;
static int lastAnnouncedTurn = 0;
unsigned long lastTurnDetectedTime = 0;  // Timestamp du dernier tour détecté


int playerRSSI;

bool capteur2Passe = false;      // Variable pour savoir si le capteur secondaire a été traversé
int turnCount = 0;               // Nombre de tours validés
unsigned long lastTurnTime = 0;  // Anti-rebond global

enum GeneratorState {
  COUNTING,
  WAITING,
  BUTTON_PHASE,
  COUNTING2,
  FINISHED,
  SHOCK,
  OUT,
  INITIATE
};
GeneratorState previousGenState;  // Variable pour stocker l’état précédent
GeneratorState genState = INITIATE;
unsigned long stateStartTime = 0;
unsigned long buttonHoldStartTime = 0;

// Tâche FreeRTOS
TaskHandle_t bleScanTaskHandle;
TaskHandle_t mqttTaskHandle;
TaskHandle_t gameTaskHandle;

// Variables pour gérer les reconnexions
const int maxReconnectAttempts = 5;  // Seuil avant de redémarrer le Wi-Fi
int reconnectAttempts = 0;           // Compteur d'échecs de reconnexion

//===================================================SETUP INITIALISATION========================================================


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

        // Stocke le RSSI du joueur détecté
        playerRSSI = advertisedDevice.getRSSI();
        Serial.print("🔻 RSSI du joueur : ");
        Serial.println(playerRSSI);

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
          SpottedPlayer = etat.toInt();
        }
      }
    }
  }
};


// --- SETUP ---
void setup() {
  delay(5000);
  Serial.begin(115200);
  delay(5000);
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(5000);

  Serial.println("Initialisation du DFPlayer Mini...");
  if (!myDFPlayer.begin(Serial2)) {
    Serial.println("❌ Erreur : Impossible de communiquer avec le DFPlayer Mini !");
    while (true)
      ;  // Bloquer en cas d'échec
  }
  Serial.println("✅ DFPlayer Mini initialisé avec succès.");

  // Vérification de la carte SD
  delay(2000);
  int sdStatus = myDFPlayer.readType();
  if (sdStatus != DFPlayerCardInserted) {
    Serial.println("⚠️ Aucune carte SD détectée ! Vérifiez la carte SD.");
  }

  // Définition des capteurs en entrée avec pull-up
  pinMode(MAG_SENSOR_1, INPUT_PULLUP);
  pinMode(MAG_SENSOR_2, INPUT_PULLUP);

  // Attacher les interruptions
  // attachInterrupt(digitalPinToInterrupt(MAG_SENSOR_1), onMagnet1Detected, RISING); // Détection de 1 → 0
  // attachInterrupt(digitalPinToInterrupt(MAG_SENSOR_2), onMagnet2Detected, RISING);

  pinMode(GENERATOR_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_BUTTON_PIN, OUTPUT);
  pinMode(LED_DECORATIVE_PIN, OUTPUT);
  pinMode(LED_DECORATIVE_PIN2, OUTPUT);
  pinMode(LED_DECORATIVE_PIN3, OUTPUT);
  pinMode(LED_DECORATIVE_PIN4, OUTPUT);
  digitalWrite(LED_BUTTON_PIN, HIGH);
  analogWrite(LED_DECORATIVE_PIN, 0);
  analogWrite(LED_DECORATIVE_PIN2, 0);
  analogWrite(LED_DECORATIVE_PIN3, 0);
  analogWrite(LED_DECORATIVE_PIN4, 0);

  ring.begin();
  ring.show();  // Initialisation (tout éteint)

  // Allume toutes les LEDs en rouge au départ
  for (int i = 0; i < NUM_LEDS; i++) {
    ring.setPixelColor(i, ring.Color(255, 0, 0));
  }
  ring.show();

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);  // Scan actif pour plus de données
  pBLEScan->setInterval(20);      // Réduit le temps entre scans (en ms)
  pBLEScan->setWindow(20);        // Augmente le temps d'écoute par canal (en ms)

  // Ajout d'une pause pour éviter les bugs d'initialisation
  delay(4000);

  // // Test de lecture audio
  // Serial.println("🎵 Test de  lecture audio (track 1)");
  // playLoopedTrack(1, 20);

  xTaskCreatePinnedToCore(mqttTask, "MQTT Task", 4096, NULL, 1, &mqttTaskHandle, 0);
  xTaskCreatePinnedToCore(generatorTask, "Generator Task", 8192, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(bleScanTask, "BLEScanTask", 4096, NULL, 1, &bleScanTaskHandle, 0);
}

//========================================GAME LOGIC=====================================================

// --- FONCTIONS AUDIO ---
void playTrackOnce(int track, int volume) {
  Serial.printf("Lecture de la piste %d avec volume %d\n", track, volume);
  vTaskDelay(300 / portTICK_PERIOD_MS);
  myDFPlayer.volume(volume);
  vTaskDelay(300 / portTICK_PERIOD_MS);
  myDFPlayer.play(track);
  vTaskDelay(300 / portTICK_PERIOD_MS);
}

void playLoopedTrack(int track, int volume) {
  if (!isLooped) {
    Serial.printf("Lecture en boucle de la piste %d avec volume %d\n", track, volume);
    vTaskDelay(300 / portTICK_PERIOD_MS);
    myDFPlayer.volume(volume);
    vTaskDelay(300 / portTICK_PERIOD_MS);
    myDFPlayer.loop(track);
    vTaskDelay(300 / portTICK_PERIOD_MS);
    isLooped = true;
  }
}

void stopLoopTrack() {
  Serial.println("Arrêt de la boucle.");
  vTaskDelay(300 / portTICK_PERIOD_MS);
  myDFPlayer.stop();
  vTaskDelay(500 / portTICK_PERIOD_MS);
  isLooped = false;
}


// --- TÂCHE GENERATOR (CORE 1) ---
void generatorTask(void* parameter) {
  Serial.println("Generator Task démarré.");
  stateStartTime = millis();

  while (true) {
    // Serial.print("Magnet 1 :");
    // Serial.println(digitalRead(27));
    // Serial.print("Magnet 2 :");
    // Serial.println(digitalRead(26));

    if (SpottedPlayer == 1 && playerRSSI > -70) {  // Vérifie que le joueur est détecté ET à moins de 5m
      previousGenState = genState;                 // Sauvegarde l’état actuel avant le changement
      // genState = SHOCK;
      ShockTime = millis();
      Serial.println("⚡ Choc déclenché ! Joueur trop proche !");
    }

    // Après 3 secondes, retour à l’état précédent
    if (genState == SHOCK && millis() - ShockTime >= 3000) {
      genState = previousGenState;
      Serial.println("↩ Retour à l’état précédent !");
    }

    //Serial.println(digitalRead(27));
    switch (genState) {
      case COUNTING:
        digitalWrite(LED_BUTTON_PIN, HIGH);
        analogWrite(LED_DECORATIVE_PIN, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
        analogWrite(LED_DECORATIVE_PIN2, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
        analogWrite(LED_DECORATIVE_PIN3, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
        analogWrite(LED_DECORATIVE_PIN4, map(turnCount, 0, NbrTour + diffLvl, 0, 255));

        updateProgressRing(turnCount, NbrTour + diffLvl);  // 🎯 Affichage LED progressif

        // 🔹 Détection du capteur secondaire (doit être activé avant le principal)
        if (digitalRead(MAG_SENSOR_1) == 0 && !capteur2Passe) {  // Anti-rebond
          capteur2Passe = true;
          Serial.println("🔸 Capteur secondaire activé !");
        }

        if (capteur2Passe && digitalRead(MAG_SENSOR_2) == 0) {  // Vérifie si capteur 2 a été activé avant
          turnCount++;
          Serial.printf("✅ Tour complet détecté ! Total : %d\n", turnCount);
          capteur2Passe = false;  // Réinitialisation
        }

        if (turnCount >= lastAnnouncedTurn + 1 && turnCount < NbrTour + diffLvl) {
          lastAnnouncedTurn = turnCount;
          playTrackOnce(2, 30);
        }

        if (turnCount >= NbrTour + diffLvl) {
          genState = WAITING;
          notifyMQTT("generateur lvl 2");
          stateStartTime = millis();
          stopLoopTrack();
          updateProgressRing(turnCount, NbrTour + diffLvl);  // 🎯 Affichage LED progressif
          Serial.println("✅ Niveau 1 validé, passage en WAITING.");
        }
        break;

      case WAITING:
        digitalWrite(LED_BUTTON_PIN, LOW);
        analogWrite(LED_DECORATIVE_PIN, 255);
        analogWrite(LED_DECORATIVE_PIN2, 255);
        analogWrite(LED_DECORATIVE_PIN3, 255);
        analogWrite(LED_DECORATIVE_PIN4, 255);

        if (JustOnce == false) {
          playTrackOnce(3, 30);
          JustOnce = true;
        }

        if (millis() - stateStartTime >= 45000 * diffLvl) {
          genState = COUNTING2;
          lastTurnDetectedTime = millis();  // Mise à jour de l'heure du dernier tour détecté
          turnCount = 0;
          lastAnnouncedTurn = 0;
          notifyMQTT("generateur lvl 3");

          Serial.println("⏳ 3 minutes écoulées, passage en BUTTON_PHASE");
        }
        break;

      case BUTTON_PHASE:
        if (digitalRead(GENERATOR_BUTTON_PIN) == LOW) {
          if (buttonHoldStartTime == 0) {
            buttonHoldStartTime = millis();
          } else if (millis() - buttonHoldStartTime >= 5000 + (diffLvl * 1000)) {
            genState = COUNTING2;
            lastTurnDetectedTime = millis();  // Mise à jour de l'heure du dernier tour détecté
            turnCount = 0;
            lastAnnouncedTurn = 0;
            Serial.println("✅ Bouton maintenu 5 sec.");
            stopLoopTrack();
          }
          digitalWrite(LED_BUTTON_PIN, HIGH);
        } else {
          buttonHoldStartTime = 0;
          digitalWrite(LED_BUTTON_PIN, (millis() / 500) % 2);
          playLoopedTrack(4, 30);
        }
        analogWrite(LED_DECORATIVE_PIN, random(0, 255));
        analogWrite(LED_DECORATIVE_PIN2, random(0, 255));
        analogWrite(LED_DECORATIVE_PIN3, random(0, 255));
        analogWrite(LED_DECORATIVE_PIN4, random(0, 255));
        break;

      case COUNTING2:
        playLoopedTrack(4, 30);
        digitalWrite(LED_BUTTON_PIN, HIGH);
        analogWrite(LED_DECORATIVE_PIN, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
        analogWrite(LED_DECORATIVE_PIN2, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
        analogWrite(LED_DECORATIVE_PIN3, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
        analogWrite(LED_DECORATIVE_PIN4, map(turnCount, 0, NbrTour + diffLvl, 0, 255));

        updateProgressRing2(turnCount, (NbrTour) + diffLvl);  // 🎯 Affichage LED progressif
        // 🔹 Détection du capteur secondaire (doit être activé avant le principal)
        if (digitalRead(MAG_SENSOR_1) == 0 && !capteur2Passe) {  // Anti-rebond
          capteur2Passe = true;
          Serial.println("🔸 Capteur secondaire activé !");
        }

        if (capteur2Passe && digitalRead(MAG_SENSOR_2) == 0) {  // Vérifie si capteur 2 a été activé avant
          turnCount++;
          lastTurnDetectedTime = millis();  // Mise à jour de l'heure du dernier tour détecté
          Serial.printf("✅ Tour complet détecté ! Total : %d\n", turnCount);
          capteur2Passe = false;  // Réinitialisation
        }

        if (turnCount >= lastAnnouncedTurn + 1 && turnCount < (NbrTour) + diffLvl) {
          lastAnnouncedTurn = turnCount;
          //playTrackOnce(2, 30);
        }

        if (turnCount >= (NbrTour) + diffLvl) {
          genState = FINISHED;
          notifyMQTT("generateur reparer");
          stateStartTime = millis();
          updateProgressRing2(turnCount,(NbrTour) + diffLvl); 
          stopLoopTrack();
          Serial.println("✅ Niveau 1 validé, passage en WAITING.");
        }

        // if (millis() - lastTurnDetectedTime >= 300000 - (diffLvl * 20000)) {  // 300000 ms = 5 minutes
        //   Serial.println("⚠️ Aucune activité détectée pendant 5 minutes. Retour en BUTTON_PHASE.");
        //   genState = BUTTON_PHASE;
        //   notifyMQTT("generateur lvl 3");  // Mise à jour MQTT
        // }
        break;

      case FINISHED:
        digitalWrite(LED_BUTTON_PIN, HIGH);
        analogWrite(LED_DECORATIVE_PIN, 255);
        analogWrite(LED_DECORATIVE_PIN2, 255);
        analogWrite(LED_DECORATIVE_PIN3, 255);
        analogWrite(LED_DECORATIVE_PIN4, 255);
        playLoopedTrack(5, 25);
        break;

      case SHOCK:
        playLoopedTrack(1, 30);
        digitalWrite((LED_BUTTON_PIN), random(0, 2));
        analogWrite(LED_DECORATIVE_PIN, random(0, 255));
        analogWrite(LED_DECORATIVE_PIN2, random(0, 255));
        analogWrite(LED_DECORATIVE_PIN3, random(0, 255));
        analogWrite(LED_DECORATIVE_PIN4, random(0, 255));
        break;

      case OUT:
        digitalWrite((LED_BUTTON_PIN), 0);
        analogWrite(LED_DECORATIVE_PIN, 0);
        analogWrite(LED_DECORATIVE_PIN2, 0);
        analogWrite(LED_DECORATIVE_PIN3, 0);
        analogWrite(LED_DECORATIVE_PIN4, 0);
        break;

      case INITIATE:
        digitalWrite((LED_BUTTON_PIN), 0);
        analogWrite(LED_DECORATIVE_PIN, 0);
        analogWrite(LED_DECORATIVE_PIN2, 0);
        analogWrite(LED_DECORATIVE_PIN3, 0);
        analogWrite(LED_DECORATIVE_PIN4, 0);
        turnCount = 0;
        lastAnnouncedTurn = 0;
        JustOnce = false;
        isLooped = false;
        stopLoopTrack();
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        playLoopedTrack(1, 20);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        notifyMQTT("generateur lvl 1");
        genState = COUNTING;
        break;
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

void updateProgressRing(int turnCount, int totalTours) {
  int ledsToLight = map(turnCount, 0, totalTours, 0, 12);
  ledsToLight = constrain(ledsToLight, 0, 12);
  int blinkingIndex = ledsToLight;  // La LED en cours de progression

  // Mise à jour clignotement
  if (millis() - lastBlinkTime >= blinkInterval) {
    blinkState = !blinkState;
    lastBlinkTime = millis();
  }

  for (int i = 0; i < 12; i++) {
    if (i < ledsToLight) {
      ring.setPixelColor(i, ring.Color(0, 255, 0));  // Vert : complété
    } else if (i == blinkingIndex && blinkState && ledsToLight < 12) {
      ring.setPixelColor(i, ring.Color(255, 255, 0));  // Jaune clignotant : en cours
    } else {
      ring.setPixelColor(i, ring.Color(255, 0, 0));  // Rouge : restant
    }
  }

  ring.show();
}

void updateProgressRing2(int turnCount, int totalTours) {
  int ledsToLight = map(turnCount, 0, totalTours, 12, NUM_LEDS);
  ledsToLight = constrain(ledsToLight, 12, NUM_LEDS);
  int blinkingIndex = ledsToLight;  // La LED en cours de progression

  // Mise à jour clignotement
  if (millis() - lastBlinkTime >= blinkInterval) {
    blinkState = !blinkState;
    lastBlinkTime = millis();
  }

  for (int i = 12; i < NUM_LEDS; i++) {
    if (i < ledsToLight) {
      ring.setPixelColor(i, ring.Color(0, 255, 0));  // Vert : complété
    } else if (i == blinkingIndex && blinkState && ledsToLight < NUM_LEDS) {
      ring.setPixelColor(i, ring.Color(255, 255, 0));  // Jaune clignotant : en cours
    } else {
      ring.setPixelColor(i, ring.Color(255, 0, 0));  // Rouge : restant
    }
  }

  ring.show();
}

void loop() {}

//--------------------------------------------------------------------------------------------------------WIFI et BLE ----------------------------------------------------------------------------------------------------------------------------------------------
// Tâche MQTT
void mqttTask(void* parameter) {
  vTaskDelay(5000 / portTICK_PERIOD_MS);
  connectToWiFi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  while (true) {
    if (!client.connected()) {
      reconnectMQTT();
    }
    client.loop();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// Tâche FreeRTOS pour scanner les messages BLE
void bleScanTask(void* parameter) {
  vTaskDelay(15000 / portTICK_PERIOD_MS);
  // Lancer un scan
  while (true) {
    Serial.println("Scan BLE en cours...");
    pBLEScan->start(SCAN_TIME, false);  // Scan en arrière-plan pour SCAN_TIME secondes
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// Gestionnaire d'événements Wi-Fi
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case WIFI_EVENT_STA_DISCONNECTED:  // Ancien SYSTEM_EVENT_STA_DISCONNECTED
      Serial.println("Wi-Fi déconnecté. Tentative de reconnexion...");
      connectToWiFi();
      break;

    case IP_EVENT_STA_GOT_IP:  // Ancien SYSTEM_EVENT_STA_GOT_IP
      Serial.println("Wi-Fi reconnecté !");
      Serial.print("Nouvelle adresse IP : ");
      Serial.println(WiFi.localIP());
      break;

    default:
      break;
  }
}


// Connexion Wi-Fi
void connectToWiFi() {
  Serial.println("Connexion au Wi-Fi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    Serial.print(".");
  }

  Serial.println("\nConnecté au Wi-Fi");
  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());
}

// Reconnexion MQTT
void reconnectMQTT() {
  String willTopic = String("esp32/") + String(esp32_id) + "/status";
  String willMessage = String(esp32_id) + ":offline";

  while (!client.connected()) {
    Serial.println("Connexion au broker MQTT...");
    if (client.connect(esp32_id, mqtt_user, mqtt_password, willTopic.c_str(), 1, true, willMessage.c_str())) {
      Serial.println("Connecté au broker MQTT");
      reconnectAttempts = 0;  // Réinitialise le compteur d'échecs
      String fullMessage = String(esp32_id) + ":" + "online";
      client.publish(willTopic.c_str(), fullMessage.c_str(), true);
      client.subscribe("unity/commandes");
      notifyMQTT("Request");
    } else {
      Serial.print("Échec, rc=");
      Serial.print(client.state());
      Serial.println(". Nouvelle tentative dans 5 secondes.");
      reconnectAttempts++;

      // Si le nombre d'échecs atteint le maximum, redémarre le Wi-Fi
      if (reconnectAttempts >= maxReconnectAttempts) {
        Serial.println("Trop d'échecs de reconnexion. Redémarrage du Wi-Fi...");
        resetWiFi();
        reconnectAttempts = 0;  // Réinitialise le compteur après redémarrage
      }

      vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
  }
}

// Fonction pour redémarrer le Wi-Fi
void resetWiFi() {
  WiFi.disconnect(true);                  // Déconnecte du réseau et réinitialise les paramètres Wi-Fi
  vTaskDelay(2000 / portTICK_PERIOD_MS);  // Pause pour garantir une réinitialisation propre
  connectToWiFi();                        // Reconnecte au Wi-Fi
}

// Callback MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Message reçu sur le topic ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  // Traiter les commandes reçues
  if (message.endsWith("reset")) {
    Serial.println("Commande de reset reçue de Unity.");
    genState = INITIATE;
  }

  if (message.startsWith(String("Generator:difficulte("))) {
    int startIdx = message.indexOf("(") + 1;
    int endIdx = message.indexOf(")");
    String difficultyStr = message.substring(startIdx, endIdx);
    float difficultyLevel = difficultyStr.toFloat();

    Serial.print("Niveau de difficulté reçu : ");
    Serial.println(difficultyLevel);

    diffLvl = difficultyLevel;
    Serial.print("Niveau de difficulté appliqué : ");
    Serial.println(diffLvl);
  }

  if (message == String(esp32_id) + ":generateur reparer") {
    turnCount = 0;
    lastAnnouncedTurn = 0;
    JustOnce = false;
    isLooped = false;
    genState = FINISHED;
  }

  if (message == String(esp32_id) + ":generateur lvl 1") {
    turnCount = 0;
    lastAnnouncedTurn = 0;
    JustOnce = false;
    isLooped = false;
    genState = COUNTING;
    notifyMQTT("generateur lvl 1");
  }

  if (message == String(esp32_id) + ":generateur lvl 2") {
    turnCount = 0;
    lastAnnouncedTurn = 0;
    JustOnce = false;
    isLooped = false;
    genState = WAITING;
  }

  if (message == String(esp32_id) + ":generateur lvl 3") {
    turnCount = 0;
    lastAnnouncedTurn = 0;
    JustOnce = false;
    isLooped = false;
    genState = BUTTON_PHASE;
  }

  if (message == String(esp32_id) + ":inactif") {
    Serial.println("🚨 Mode INACTIF activé via MQTT.");
    genState = OUT;
    notifyMQTT("off");
  }

  if (message == String(esp32_id) + ":actif") {
    Serial.println("✅ Mode ACTIF activé via MQTT.");
    genState = INITIATE;
    notifyMQTT("on");
  }
}

// Notifier le serveur MQTT
void notifyMQTT(const char* message) {
  String fullMessage = String(esp32_id) + ":" + message;  // Préfixer le message par l'identifiant
  client.publish("esp32/donnees", fullMessage.c_str());
}
