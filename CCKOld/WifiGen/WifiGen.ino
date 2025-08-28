#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>

// Informations Wi-Fi
const char* ssid = "ESP32_Local_Network";
const char* password = "DjioopPod";

// Adresse du broker MQTT
const char* mqtt_server = "192.168.5.192";

// Identifiants MQTT
const char* mqtt_user = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";

const char* esp32_id = "ESP32:Generator1"; // Identifiant unique pour cet ESP32

// MQTT
WiFiClient espClient;
PubSubClient client(espClient);

// LEDs et boutons
const int signalLedPins[] = {13, 12, 14, 27, 26}; // LEDs de signal
const int buttonLedPins[] = {33, 4, 25, 21, 22};  // LEDs analogiques des boutons
const int buttonPins[] = {23, 15, 5, 18, 19};      // Boutons

const int numLeds = sizeof(signalLedPins) / sizeof(signalLedPins[0]);
const int numButtons = sizeof(buttonPins) / sizeof(buttonPins[0]);

int sequence[8]; // Stocke la séquence de couleurs
int level = 1;   // Niveau actuel du jeu
int currentInputIndex = 0; // Indice de la séquence en cours de saisie
bool StartGame = false;
bool playerFailed = false; // Indique si le joueur a échoué pendant la séquence
// unsigned long gameStartTime = 0; // Temps pour éviter un rebond initial après le démarrage
unsigned long stepStartTime = 0; // Temps de début pour chaque étape
unsigned long inputTimeout = 2500; // Temps limite par étape (2 secondes pour le niveau 1)
unsigned long blinkTimer = 0; // Temps pour gérer le clignotement du bouton rouge
unsigned long lastPressTime = 0; // Temps du dernier appui pour gérer l'anti-rebond
// unsigned long effectStartTime = 0; // Temps de début pour l'effet visuel des LEDs des boutons
// const unsigned long debounceDelay = 300; // Délai pour l'anti-rebond
bool blinkState = false; // État actuel du clignotement
bool disableLedEffects = false; // Drapeau pour désactiver les effets LED
bool UnityOrderReset = false;
bool Celebrity = false;
int SpottedPlayer = 0;


float diffLvl = 1;

// Variables pour gérer les reconnexions
const int maxReconnectAttempts = 5; // Seuil avant de redémarrer le Wi-Fi
int reconnectAttempts = 0;          // Compteur d'échecs de reconnexion

#define SCAN_TIME 5  // Réduction de la durée du scan
BLEScan* pBLEScan;


// Tâche FreeRTOS
TaskHandle_t bleScanTaskHandle;
TaskHandle_t mqttTaskHandle;
TaskHandle_t gameTaskHandle;

enum GameState {
  INITIAL,
  GAME_STARTED,
  GAME_FAILED,
  GAME_WON_LEVEL,
  GAME_BLOCKED,
  GAME_WON_FINAL
};

GameState currentState = INITIAL;
unsigned long stateStartTime = 0;

unsigned long calculateTimeout(int lvl); // Prototype pour calculer le délai
void playSequence();                     // Prototype pour jouer la séquence

// Prototypes des fonctions
void generateRandomSequence();
void resetGame();
void celebrateWin();
// void resetGameForNextLevel();
void updateButtonLedEffect();
void notifyMQTT(const char* message);
void WiFiEvent(WiFiEvent_t event);
void resetWiFi();
void setupInitialState();
void handleInitialState();
void setupGameStartedState();
void handleGameStartedState();
void setupGameFailedState();
void handleGameFailedState();
void setupGameWonLevelState();
void handleGameWonLevelState();
void setupGameBlockedState();
void handleGameBlockedState();
void setupGameWonFinalState();
void handleGameWonFinalState();
void generateRandomSequence();

//--------------------------------------------------------------------------------------------------------Config Initial ----------------------------------------------------------------------------------------------------------------------------------------------

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
          SpottedPlayer = etat.toInt();
        }
      }

      //pBLEScan->stop();  // Arrête le scan immédiatement après détection
      Serial.println("\n-------------------\n");
    }
  }
};

// Configuration FreeRTOS
void setup() {
  Serial.begin(115200);

  Serial.println("Initialisation du scanner BLE...");

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);  // Scan actif pour plus de données
  pBLEScan->setInterval(20);      // Réduit le temps entre scans (en ms)
  pBLEScan->setWindow(20);        // Augmente le temps d'écoute par canal (en ms)

   for (int i = 0; i < numLeds; i++) {
    pinMode(signalLedPins[i], OUTPUT);
    digitalWrite(signalLedPins[i], LOW);
    pinMode(buttonLedPins[i], OUTPUT);
    analogWrite(buttonLedPins[i], 0);
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }

  for (int i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }

  xTaskCreatePinnedToCore(mqttTask, "MQTT Task", 4000, NULL, 1, &mqttTaskHandle, 0);
  xTaskCreatePinnedToCore(bleScanTask, "BLEScanTask", 4096, NULL, 1, &bleScanTaskHandle, 0);
  xTaskCreatePinnedToCore(gameTask, "Game Task", 4000, NULL, 1, &gameTaskHandle, 1);
}

void loop() {
  // Rien ici, tout est géré par les tâches FreeRTOS
}

//--------------------------------------------------------------------------------------------------------Game Logic ----------------------------------------------------------------------------------------------------------------------------------------------

//   setupGame();

//   while (true) {
//     if (SpottedPlayer == 1)
//     {
//       // Allumer les LEDs des boutons et éteindre toutes les LEDs de signal
//     for (int i = 0; i < numLeds; i++) {
//     vTaskDelay(10/ portTICK_PERIOD_MS);
//     analogWrite(buttonLedPins[i], 0);
//     vTaskDelay(10/ portTICK_PERIOD_MS);
//     digitalWrite(signalLedPins[i], LOW);
//     }
//     vTaskDelay(100 / portTICK_PERIOD_MS);
//     continue;
//     }

//     if (!StartGame) {
//       handlePreGameState();
//       Celebrity = false;
//       vTaskDelay(10 / portTICK_PERIOD_MS); // Délai pour éviter de bloquer l'exécution
//       continue; // Continuer à la prochaine itération
//     }

//     if (!playerFailed) {
//       if (currentInputIndex < level + 5) {
//         processStep();
//         if (!disableLedEffects) {
//           updateButtonLedEffect(); // Mettre à jour l'effet visuel
//         }
//       } else {
//         //notifyMQTT("Série Succès");
//         if (level < 3) {
//           level++;
//           resetGameForNextLevel();
//         } else {
//           celebrateWin();
//         }
//       }
//     } else {      
//       resetGame();
//     }
//     vTaskDelay(10 / portTICK_PERIOD_MS);
//   }
// }
void gameTask(void* parameter) {
  while (true) {
    if (SpottedPlayer == 1) {
      for (int i = 0; i < numLeds; i++) {
        analogWrite(buttonLedPins[i], 0);
        digitalWrite(signalLedPins[i], LOW);
      }
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }

    switch (currentState) {
      case INITIAL:
        handleInitialState();
        break;
      case GAME_STARTED:
        handleGameStartedState();
        break;
      case GAME_FAILED:
        handleGameFailedState();
        break;
      case GAME_WON_LEVEL:
        handleGameWonLevelState();
        break;
      case GAME_BLOCKED:
        handleGameBlockedState();
        break;
      case GAME_WON_FINAL:
        handleGameWonFinalState();
        break;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

unsigned long calculateTimeout(int lvl) {
  switch (lvl) {
    case 1:
      return 2500 * diffLvl;
    case 2:
      return 2000 * diffLvl;
    case 3:
      return 1500 * diffLvl;
    default:
      return 2500 * diffLvl;
  }
}

void setupInitialState() {
  currentState = INITIAL;
  stateStartTime = millis();
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(signalLedPins[i], LOW);
    analogWrite(buttonLedPins[i], 0);
  }
  Serial.println("Initial State Set");
}

void handleInitialState() {
  if (millis() - blinkTimer >= 500) {
    blinkTimer = millis();
    blinkState = !blinkState;
    digitalWrite(signalLedPins[1], blinkState);
    analogWrite(buttonLedPins[1], blinkState*255);
  }

  if (digitalRead(buttonPins[1]) == LOW && millis() - lastPressTime > 30 0) {
    lastPressTime = millis();
    setupGameStartedState();
  }
}

void setupGameStartedState() {
  currentState = GAME_STARTED;
  stateStartTime = millis();
  playerFailed = false;
  currentInputIndex = 0;
  inputTimeout = calculateTimeout(level);

  for (int i = 0; i < numLeds; i++) {
    analogWrite(buttonLedPins[i], 255);
    digitalWrite(signalLedPins[i], LOW);
  }

  generateRandomSequence();
  stepStartTime = millis();
  Serial.println("Game Started State Set");
}

void handleGameStartedState() {
  if (playerFailed) {
    setupGameFailedState();
    return;
  }

  if (millis() - stepStartTime > inputTimeout) {
    playerFailed = true;
    setupGameFailedState();
    return;
  }

  digitalWrite(signalLedPins[sequence[currentInputIndex]], HIGH);

  if (!disableLedEffects) {
    updateButtonLedEffect();
  }

  processPlayerInput();
}

void setupGameFailedState() {
  currentState = GAME_FAILED;
  stateStartTime = millis();
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(signalLedPins[i], LOW);
    analogWrite(buttonLedPins[i], 0);
  }
  digitalWrite(signalLedPins[1],HIGH);
  analogWrite(buttonLedPins[1], 255);
  Serial.println("Game Failed State Set");
}

void handleGameFailedState() {
  if (millis() - stateStartTime >= 60000) {
    setupInitialState();
  }
}

void setupGameWonLevelState() {
  currentState = GAME_WON_LEVEL;
  stateStartTime = millis();
  for (int i = 0; i < numLeds; i++) {
    analogWrite(buttonLedPins[i], 255);
    digitalWrite(signalLedPins[i],HIGH);
  }
  Serial.println("Game Won Level State Set");
}

void handleGameWonLevelState() {
  if (millis() - stateStartTime < 5000 && millis() - blinkTimer >= 500) {
    blinkTimer = millis();
    blinkState = !blinkState;
    for (int i = 0; i < numLeds; i++){
    digitalWrite(signalLedPins[i], blinkState);
    analogWrite(buttonLedPins[i], blinkState*255);
    }
  }
  if (millis() - stateStartTime >= 5000) {
    setupGameStartedState();
  }
}

void setupGameBlockedState() {
  currentState = GAME_BLOCKED;
  stateStartTime = millis();
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(signalLedPins[i], LOW);
    analogWrite(buttonLedPins[i], 0);
  }
  Serial.println("Game Blocked State Set");
}

void handleGameBlockedState() {
  if (millis() - stateStartTime >= 30000) {
    setupInitialState();
  }
}

void setupGameWonFinalState() {
  currentState = GAME_WON_FINAL;
  stateStartTime = millis();
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(signalLedPins[i], HIGH);
    analogWrite(buttonLedPins[i], 255);
  }
  celebrateWin();
  Serial.println("Game Won Final State Set");
}

void handleGameWonFinalState() {
  // Remains in this state until reset command received
}

void generateRandomSequence() {
  for (int i = 0; i < level + 5; i++) {
    sequence[i] = random(0, numButtons);
    Serial.print("Generated Step: ");
    Serial.println(sequence[i]);
  }
}

void processPlayerInput() {
  for (int i = 0; i < numButtons; i++) {
    if (digitalRead(buttonPins[i]) == LOW) {
      if (i == sequence[currentInputIndex]) {
        digitalWrite(signalLedPins[sequence[currentInputIndex]], LOW);
        currentInputIndex++;
        stepStartTime = millis();
        Serial.println("Correct input");
        return;
      } else {
        playerFailed = true;
        Serial.println("Incorrect input");
        setupGameFailedState();
        return;
      }
    }
  }
}

void updateButtonLedEffect() {
  unsigned long elapsed = millis() - stepStartTime;
  unsigned long brightness = map(elapsed, 0, inputTimeout, 255, 0);

  for (int i = 0; i < numLeds; i++) {
    analogWrite(buttonLedPins[i], brightness);
  }
}



// Configuration initiale des LEDs et boutons
// void setupGame() {
  // for (int i = 0; i < numLeds; i++) {
  //   pinMode(signalLedPins[i], OUTPUT);
  //   digitalWrite(signalLedPins[i], LOW);
  //   pinMode(buttonLedPins[i], OUTPUT);
  //   analogWrite(buttonLedPins[i], 0);
  //   vTaskDelay(10 / portTICK_PERIOD_MS);
  // }

  // for (int i = 0; i < numButtons; i++) {
  //   pinMode(buttonPins[i], INPUT_PULLUP);
  // }

//   randomSeed(analogRead(0));
//   generateRandomSequence();
//   notifyMQTT("Generateur en attente");
// }

// // Gestion de l'état avant le début du jeu
// void handlePreGameState() {
//   // Clignotement du bouton rouge pour indiquer que le jeu n'a pas encore commencé
//   if (millis() - blinkTimer >= 500) {
//     blinkTimer = millis();
//     blinkState = !blinkState;
//     digitalWrite(signalLedPins[1], blinkState);
//     vTaskDelay(10 / portTICK_PERIOD_MS);
//     analogWrite(buttonLedPins[1], blinkState ? 255 : 0);
//     vTaskDelay(10 / portTICK_PERIOD_MS);
//   }

//   // Détection de l'appui sur le bouton rouge pour démarrer le jeu
//   if (digitalRead(buttonPins[1]) == LOW && (millis() - lastPressTime > debounceDelay)) {
//     lastPressTime = millis();
//     StartGame = true;
//     gameStartTime = millis();
//     currentInputIndex = 0;
//     playerFailed = false;
//     inputTimeout = calculateTimeout(level); // Initialiser le temps limite en fonction du niveau
//     notifyMQTT("jeu en cours");

//     // Allumer les LEDs des boutons et éteindre toutes les LEDs de signal
//     for (int i = 0; i < numLeds; i++) {
//       vTaskDelay(10/ portTICK_PERIOD_MS);
//       analogWrite(buttonLedPins[i], 255);
//     vTaskDelay(10/ portTICK_PERIOD_MS);
//       digitalWrite(signalLedPins[i], LOW);
//     }

//     effectStartTime = millis(); // Démarrer l'effet visuel

//     //notifyMQTT ("Jeu démarrer");
//     playSequence();
//   }
// }

// void processStep() {
//   int currentSignal = sequence[currentInputIndex];

//   if (millis() - stepStartTime > inputTimeout) {
//     //notifyMQTT ("Temps Ecouler");
//     playerFailed = true;
//     return;
//   }

//   digitalWrite(signalLedPins[currentSignal], HIGH);

//   for (int i = 0; i < numButtons; i++) {
//     int buttonState = digitalRead(buttonPins[i]);

//     if (buttonState == LOW && (millis() - lastPressTime > debounceDelay)) {
//       lastPressTime = millis();
//       Serial.print("Bouton ");
//       Serial.print(i);
//       Serial.print(" pressé. Attendu : ");
//       Serial.println(currentSignal);

//       if (i == currentSignal) {
//         Serial.println("Correct");
//         digitalWrite(signalLedPins[currentSignal], LOW);
//         stepStartTime = millis(); // Redémarrer le timer pour la prochaine étape
//         currentInputIndex++;
//         effectStartTime = millis(); // Redémarrer l'effet visuel
//         return;
//       } else {
//         Serial.println("Incorrect");
//         notifyMQTT("generateur echec");
//         playerFailed = true;
//         return;
//       }
//     }
//   }
// }


// Effets lumineux des LEDs des boutons
// void updateButtonLedEffect() {
//   if (disableLedEffects) {
//     return;
//   }

//   unsigned long elapsed = millis() - effectStartTime;
//   unsigned long brightness = map(elapsed, 0, inputTimeout, 255, 0); // Diminue la luminosité en fonction du temps

//   for (int i = 0; i < numLeds; i++) {
//     analogWrite(buttonLedPins[i], brightness); // Ajuste la luminosité des LEDs des boutons
//     vTaskDelay(10/ portTICK_PERIOD_MS);
//   }
// }

// // Génération de la séquence aléatoire
// void generateRandomSequence() {
//   int numColors = level + 5;
//   for (int i = 0; i < numColors; i++) {
//     int attempts = 0;
//     int randIndex;
//     do {
//       randIndex = random(0, numButtons);
//       attempts++;
//       if (attempts > 10) break;
//     } while ((i > 0 && randIndex == sequence[i - 1]) || (i == 0 && level > 1 && randIndex == sequence[level + 3]));
//     sequence[i] = randIndex;
//   }
// }


// Réinitialisation du jeu
void resetGame() {
  if (UnityOrderReset == false)
  {
  disableLedEffects = true; // Désactiver les effets LED

  for (int i = 0; i < numLeds; i++) {
    analogWrite(buttonLedPins[i], 0); // Éteindre les LEDs des boutons
    vTaskDelay(15/ portTICK_PERIOD_MS);
    digitalWrite(signalLedPins[i], LOW); // Éteindre les LEDs de signal
  }

  level = 1;
  currentInputIndex = 0;
  playerFailed = false;
  notifyMQTT("Generateur en attente");

  // Éteindre toutes les LEDs des boutons et de signal
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(signalLedPins[i], LOW);
    analogWrite(buttonLedPins[i], 0);
    vTaskDelay(15/ portTICK_PERIOD_MS);
  }

  // generateRandomSequence();

  disableLedEffects = false; // Réactiver les effets LED après réinitialisation
  }
}

// void resetGameForNextLevel() {
//   // Pause avec toutes les LEDs éteintes avant le prochain niveau
//   disableLedEffects = true; // Désactiver les effets LED pendant la pause
//   for (int i = 0; i < numLeds; i++) {
//     analogWrite(buttonLedPins[i], 0); // Éteindre les LEDs des boutons
//     vTaskDelay(15/ portTICK_PERIOD_MS);
//     digitalWrite(signalLedPins[i], LOW); // Éteindre les LEDs de signal
//   }
//   vTaskDelay(2000/ portTICK_PERIOD_MS); // Pause de 2 secondes

//   // Rallumer toutes les LEDs des boutons après la pause
//   for (int i = 0; i < numLeds; i++) {
//     analogWrite(buttonLedPins[i], 255); // Rallumer les LEDs des boutons avec luminosité maximale
//     vTaskDelay(15/ portTICK_PERIOD_MS);
//   }
  
//   currentInputIndex = 0;
//   playerFailed = false;
//   inputTimeout = calculateTimeout(level);
//   generateRandomSequence();
//   stepStartTime = millis(); // Réinitialiser le timer pour le nouveau niveau
//   disableLedEffects = false; // Réactiver les effets LED après la pause

//   Serial.println("Niveau suivant");
// }

// void playSequence() {
//   for (int i = 0; i < level + 5; i++) {
//     Serial.print("Séquence générée : ");
//     Serial.println(sequence[i]);
//   }
//   stepStartTime = millis();
// }

// unsigned long calculateTimeout(int lvl) {
//   switch (lvl) {
//     case 1:
//       return 2500 * diffLvl;
//     case 2:
//       return 2000* diffLvl;
//     case 3:
//       return 1500* diffLvl;
//     default:
//       return 2500* diffLvl;
//   }
// }

// // Gestion de la victoire
void celebrateWin() {
  if (Celebrity == false)
  {
  Celebrity = true;
  disableLedEffects = true; // Désactiver les effets LED pendant la célébration
  notifyMQTT ("generateur reparer");

  unsigned long winStartTime = millis();
  unsigned long lastBlinkTime = millis();
  const unsigned long blinkInterval = 200;
  bool ledState = false;

  while (millis() - winStartTime < 5000) { // Clignotement pendant 5 secondes
    if (millis() - lastBlinkTime >= blinkInterval) {
      lastBlinkTime = millis();
      ledState = !ledState;
      for (int i = 0; i < numLeds; i++) {
        analogWrite(buttonLedPins[i], ledState ? 255 : 0);
        vTaskDelay(10/ portTICK_PERIOD_MS);
        digitalWrite(signalLedPins[i], ledState ? HIGH : LOW);
      }
    }
  }
  // Éteindre toutes les LEDs après la victoire
  for (int i = 0; i < numLeds; i++) {
    analogWrite(buttonLedPins[i], 255);
    vTaskDelay(10/ portTICK_PERIOD_MS);
    digitalWrite(signalLedPins[i], HIGH);
  }

  disableLedEffects = false; // Réactiver les effets LED après la célébration
}
}

//--------------------------------------------------------------------------------------------------------WIFI et BLE ----------------------------------------------------------------------------------------------------------------------------------------------
// Gestionnaire d'événements Wi-Fi
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case WIFI_EVENT_STA_DISCONNECTED: // Ancien SYSTEM_EVENT_STA_DISCONNECTED
      Serial.println("Wi-Fi déconnecté. Tentative de reconnexion...");
      connectToWiFi();
      break;

    case IP_EVENT_STA_GOT_IP: // Ancien SYSTEM_EVENT_STA_GOT_IP
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
  const char* willTopic = "esp32/EPS32:Generator1/status";
  const char* willMessage = "EPS32:Generator1:offline";

  while (!client.connected()) {
    Serial.println("Connexion au broker MQTT...");
    if (client.connect("EPS32:Generator1", mqtt_user, mqtt_password, willTopic, 1, true, willMessage)) {
      Serial.println("Connecté au broker MQTT");
      reconnectAttempts = 0; // Réinitialise le compteur d'échecs
      String fullMessage = String(esp32_id) + ":" + "online";
      client.publish(willTopic, fullMessage.c_str(), true);
      client.subscribe("unity/commandes");
    } else {
      Serial.print("Échec, rc=");
      Serial.print(client.state());
      Serial.println(". Nouvelle tentative dans 5 secondes.");
      reconnectAttempts++;

      // Si le nombre d'échecs atteint le maximum, redémarre le Wi-Fi
      if (reconnectAttempts >= maxReconnectAttempts) {
        Serial.println("Trop d'échecs de reconnexion. Redémarrage du Wi-Fi...");
        resetWiFi();
        reconnectAttempts = 0; // Réinitialise le compteur après redémarrage
      }

      vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
  }
}

// Fonction pour redémarrer le Wi-Fi
void resetWiFi() {
  WiFi.disconnect(true); // Déconnecte du réseau et réinitialise les paramètres Wi-Fi
  vTaskDelay(2000 / portTICK_PERIOD_MS); // Pause pour garantir une réinitialisation propre
  connectToWiFi();       // Reconnecte au Wi-Fi
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
  if (message == "EPS32:Generator1:reset") {
    Serial.println("Commande de reset reçue de Unity.");
    UnityOrderReset = false;
    resetGame();
  }

if (message.startsWith("EPS32:Generator1:difficulté(")) {
    int startIdx = message.indexOf("(") + 1;
    int endIdx = message.indexOf(")");
    String difficultyStr = message.substring(startIdx, endIdx);
    float difficultyLevel = difficultyStr.toFloat();

    Serial.print("Niveau de difficulté reçu : ");
    Serial.println(difficultyLevel);

    diffLvl = 1 - (difficultyLevel/30);
    Serial.print("Niveau de difficulté appliqué : ");
    Serial.println (diffLvl);
}

if (message.endsWith("DetectionDiff")) {
    //performBLEScan();
}
}

// Notifier le serveur MQTT
void notifyMQTT(const char* message) {
    String fullMessage = String(esp32_id) + ":" + message; // Préfixer le message par l'identifiant
    client.publish("esp32/donnees", fullMessage.c_str());
}

// Tâche FreeRTOS pour scanner les messages BLE
void bleScanTask(void* parameter) {
  // Lancer un scan
  while(true)
  {
  Serial.println("Scan BLE en cours...");
  pBLEScan->start(SCAN_TIME, false);  // Scan en arrière-plan pour SCAN_TIME secondes
  delay(100);  // Petite pause avant de recommencer le scan
  }
}

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
