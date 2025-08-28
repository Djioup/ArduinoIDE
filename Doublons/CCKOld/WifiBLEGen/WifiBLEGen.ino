#include <WiFi.h>
#include <PubSubClient.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// Informations Wi-Fi
const char* ssid = "ESP32_Local_Network";
const char* password = "DjioopPod";

// Adresse du broker MQTT
const char* mqtt_server = "192.168.5.192";

// Identifiants MQTT
const char* mqtt_user = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";

const char* esp32_id = "ESP32_001"; // Identifiant unique pour cet ESP32

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
bool gamePaused = false;   // Indique si le jeu est mis en pause en raison de BLE spotted
unsigned long gameStartTime = 0; // Temps pour éviter un rebond initial après le démarrage
unsigned long stepStartTime = 0; // Temps de début pour chaque étape
unsigned long inputTimeout = 2500; // Temps limite par étape (2 secondes pour le niveau 1)
unsigned long blinkTimer = 0; // Temps pour gérer le clignotement du bouton rouge
unsigned long lastPressTime = 0; // Temps du dernier appui pour gérer l'anti-rebond
unsigned long effectStartTime = 0; // Temps de début pour l'effet visuel des LEDs des boutons
const unsigned long debounceDelay = 300; // Délai pour l'anti-rebond
bool blinkState = false; // État actuel du clignotement
bool disableLedEffects = false; // Drapeau pour désactiver les effets LED
bool UnityOrderReset = false;
bool Celebrity = false;

float diffLvl = 1;

// Variables pour gérer les reconnexions
const int maxReconnectAttempts = 5; // Seuil avant de redémarrer le Wi-Fi
int reconnectAttempts = 0;          // Compteur d'échecs de reconnexion

// Tâches FreeRTOS
TaskHandle_t mqttTaskHandle;
TaskHandle_t gameTaskHandle;
TaskHandle_t bleTaskHandle;

// BLE
BLEScan* bleScan;
const int scanTime = 5; // Temps de scan en secondes
const int rssiThreshold = -70; // RSSI pour une portée de 5 mètres

unsigned long calculateTimeout(int lvl); // Prototype pour calculer le délai
void playSequence();                     // Prototype pour jouer la séquence

// Prototypes des fonctions
void generateRandomSequence();
void resetGame();
void celebrateWin();
void resetGameForNextLevel();
void updateButtonLedEffect();
void notifyMQTT(const char* message);
void WiFiEvent(WiFiEvent_t event);
void resetWiFi();

// Callback pour le scan BLE
class CustomBLEAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (advertisedDevice.haveName()) {
      String deviceName = advertisedDevice.getName().c_str();
      String deviceData = advertisedDevice.getManufacturerData().c_str();

      if (deviceName == "ESP32" && deviceData.indexOf("player1:spotted") != -1) {
        Serial.println("ESP32 spotted nearby!");
        Serial.print("RSSI: ");
        Serial.println(advertisedDevice.getRSSI());

        if (advertisedDevice.getRSSI() > rssiThreshold) {
          if (!gamePaused) {
            Serial.println("Game interrupted: player1 spotted within range!");
            notifyMQTT("Game paused due to player1 spotted nearby.");
            gamePaused = true;
            resetGame(); // Retour à l'état initial du jeu
          }
        }
      } else if (deviceName == "ESP32" && deviceData.indexOf("player1:unspotted") != -1) {
        Serial.println("ESP32 unspotted!");
        if (gamePaused) {
          Serial.println("Game resumed: player1 is out of range.");
          notifyMQTT("Game resumed.");
          gamePaused = false;
        }
      }
    }
  }
};


// Initialisation BLE
void setupBLE() {
  BLEDevice::init("");
  bleScan = BLEDevice::getScan();
  bleScan->setAdvertisedDeviceCallbacks(new CustomBLEAdvertisedDeviceCallbacks());
  bleScan->setActiveScan(true);
}

// Tâche BLE
void bleTask(void* parameter) {
  setupBLE();

  while (true) {
    if (!gamePaused) {
      Serial.println("Scanning for nearby ESP32 devices...");
      bleScan->start(scanTime, false);
      bleScan->clearResults();
    } else {
      Serial.println("Game paused, skipping BLE scan.");
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
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

// Tâche Jeu
void gameTask(void* parameter) {
  setupGame();

  while (true) {
    if (gamePaused) {
      vTaskDelay(100 / portTICK_PERIOD_MS); // Réduit la charge CPU pendant la pause
      continue;
    }

    if (!StartGame) {
      handlePreGameState();
      Celebrity = false;
      vTaskDelay(10 / portTICK_PERIOD_MS); // Délai pour éviter de bloquer l'exécution
      continue; // Continuer à la prochaine itération
    }

    if (!playerFailed) {
      if (currentInputIndex < level + 5) {
        processStep();
        if (!disableLedEffects) {
          updateButtonLedEffect(); // Mettre à jour l'effet visuel
        }
      } else {
        if (level < 3) {
          level++;
          resetGameForNextLevel();
        } else {
          celebrateWin();
        }
      }
    } else {      
      resetGame();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

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
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnecté au Wi-Fi");
  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());
}

// Reconnexion MQTT
void reconnectMQTT() {
  const char* willTopic = "esp32/EPS32_001/status";
  const char* willMessage = "ESP32_001:offline";

  while (!client.connected()) {
    Serial.println("Connexion au broker MQTT...");
    if (client.connect("ESP32_001", mqtt_user, mqtt_password, willTopic, 1, true, willMessage)) {
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
  if (message == "ESP32_001:reset") {
    Serial.println("Commande de reset reçue de Unity.");
    UnityOrderReset = false;
    resetGame();
  }

if (message.startsWith("ESP32_001:difficulté(")) {
    int startIdx = message.indexOf("(") + 1;
    int endIdx = message.indexOf(")");
    String difficultyStr = message.substring(startIdx, endIdx);
    float difficultyLevel = difficultyStr.toFloat();

    Serial.print("Niveau de difficulté reçu : ");
    Serial.println(difficultyLevel);

    diffLvl = 1 - (difficultyLevel / 30);
    Serial.print("Niveau de difficulté appliqué : ");
    Serial.println(diffLvl);
  }
}

// Notifier le serveur MQTT
void notifyMQTT(const char* message) {
  String fullMessage = String(esp32_id) + ":" + message; // Préfixer le message par l'identifiant
  client.publish("esp32/donnees", fullMessage.c_str());
}

// Configuration initiale des LEDs et boutons
void setupGame() {
  for (int i = 0; i < numLeds; i++) {
    pinMode(signalLedPins[i], OUTPUT);
    digitalWrite(signalLedPins[i], LOW);
    pinMode(buttonLedPins[i], OUTPUT);
    analogWrite(buttonLedPins[i], 0);
    delay(10);
  }

  for (int i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }

  randomSeed(analogRead(0));
  generateRandomSequence();
  notifyMQTT("Generateur en attente");
}

// Gestion de l'état avant le début du jeu
void handlePreGameState() {
  // Clignotement du bouton rouge pour indiquer que le jeu n'a pas encore commencé
  if (millis() - blinkTimer >= 500) {
    blinkTimer = millis();
    blinkState = !blinkState;
    digitalWrite(signalLedPins[1], blinkState);
    delay(10);
    analogWrite(buttonLedPins[1], blinkState ? 255 : 0);
    delay(10);
  }

  // Détection de l'appui sur le bouton rouge pour démarrer le jeu
  if (digitalRead(buttonPins[1]) == LOW && (millis() - lastPressTime > debounceDelay)) {
    lastPressTime = millis();
    StartGame = true;
    gameStartTime = millis();
    currentInputIndex = 0;
    playerFailed = false;
    inputTimeout = calculateTimeout(level); // Initialiser le temps limite en fonction du niveau
    notifyMQTT("jeu en cours");

    // Allumer les LEDs des boutons et éteindre toutes les LEDs de signal
    for (int i = 0; i < numLeds; i++) {
      delay(10);
      analogWrite(buttonLedPins[i], 255);
      delay(10);
      digitalWrite(signalLedPins[i], LOW);
    }

    effectStartTime = millis(); // Démarrer l'effet visuel

    playSequence();
  }
}


void processStep() {
  int currentSignal = sequence[currentInputIndex];

  if (millis() - stepStartTime > inputTimeout) {
    playerFailed = true;
    return;
  }

  digitalWrite(signalLedPins[currentSignal], HIGH);

  for (int i = 0; i < numButtons; i++) {
    int buttonState = digitalRead(buttonPins[i]);

    if (buttonState == LOW && (millis() - lastPressTime > debounceDelay)) {
      lastPressTime = millis();
      Serial.print("Bouton ");
      Serial.print(i);
      Serial.print(" pressé. Attendu : ");
      Serial.println(currentSignal);

      if (i == currentSignal) {
        Serial.println("Correct");
        digitalWrite(signalLedPins[currentSignal], LOW);
        stepStartTime = millis(); // Redémarrer le timer pour la prochaine étape
        currentInputIndex++;
        effectStartTime = millis(); // Redémarrer l'effet visuel
        return;
      } else {
        Serial.println("Incorrect");
        notifyMQTT("generateur echec");
        playerFailed = true;
        return;
      }
    }
  }
}

// Effets lumineux des LEDs des boutons
void updateButtonLedEffect() {
  if (disableLedEffects) {
    return;
  }

  unsigned long elapsed = millis() - effectStartTime;
  unsigned long brightness = map(elapsed, 0, inputTimeout, 255, 0); // Diminue la luminosité en fonction du temps

  for (int i = 0; i < numLeds; i++) {
    analogWrite(buttonLedPins[i], brightness); // Ajuste la luminosité des LEDs des boutons
    delay(10);
  }
}

// Génération de la séquence aléatoire
void generateRandomSequence() {
  int numColors = level + 5;
  for (int i = 0; i < numColors; i++) {
    int attempts = 0;
    int randIndex;
    do {
      randIndex = random(0, numButtons);
      attempts++;
      if (attempts > 10) break;
    } while ((i > 0 && randIndex == sequence[i - 1]) || (i == 0 && level > 1 && randIndex == sequence[level + 3]));
    sequence[i] = randIndex;
  }
}

// Réinitialisation du jeu
void resetGame() {
  disableLedEffects = true; // Désactiver les effets LED

  for (int i = 0; i < numLeds; i++) {
    analogWrite(buttonLedPins[i], 0); // Éteindre les LEDs des boutons
    delay(15);
    digitalWrite(signalLedPins[i], LOW); // Éteindre les LEDs de signal
  }

  level = 1;
  currentInputIndex = 0;
  playerFailed = false;
  StartGame = false;
  notifyMQTT("Generateur en attente");

  for (int i = 0; i < numLeds; i++) {
    digitalWrite(signalLedPins[i], LOW);
    analogWrite(buttonLedPins[i], 0);
    delay(15);
  }

  generateRandomSequence();

  disableLedEffects = false; // Réactiver les effets LED après réinitialisation
}

void resetGameForNextLevel() {
  disableLedEffects = true; // Désactiver les effets LED pendant la pause

  for (int i = 0; i < numLeds; i++) {
    analogWrite(buttonLedPins[i], 0); // Éteindre les LEDs des boutons
    delay(15);
    digitalWrite(signalLedPins[i], LOW); // Éteindre les LEDs de signal
  }

  delay(2000); // Pause de 2 secondes

  for (int i = 0; i < numLeds; i++) {
    analogWrite(buttonLedPins[i], 255); // Rallumer les LEDs des boutons avec luminosité maximale
    delay(15);
  }

  currentInputIndex = 0;
  playerFailed = false;
  inputTimeout = calculateTimeout(level);
  generateRandomSequence();
  stepStartTime = millis(); // Réinitialiser le timer pour le nouveau niveau
  disableLedEffects = false; // Réactiver les effets LED après la pause

  Serial.println("Niveau suivant");
}

void celebrateWin() {
  if (Celebrity == false) {
    Celebrity = true;
    disableLedEffects = true; // Désactiver les effets LED pendant la célébration
    notifyMQTT("generateur reparer");

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
          delay(10);
          digitalWrite(signalLedPins[i], ledState ? HIGH : LOW);
        }
      }
    }

    for (int i = 0; i < numLeds; i++) {
      analogWrite(buttonLedPins[i], 0);
      delay(10);
      digitalWrite(signalLedPins[i], LOW);
    }

    disableLedEffects = false; // Réactiver les effets LED après la célébration
  }
}

void playSequence() {
  for (int i = 0; i < level + 5; i++) {
    int colorIndex = sequence[i];
    digitalWrite(signalLedPins[colorIndex], HIGH);
    delay(500);
    digitalWrite(signalLedPins[colorIndex], LOW);
    delay(250);
  }
  stepStartTime = millis();
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

void setup() {
  Serial.begin(115200);

  xTaskCreatePinnedToCore(mqttTask, "MQTT Task", 4000, NULL, 1, &mqttTaskHandle, 0);
  xTaskCreatePinnedToCore(gameTask, "Game Task", 4000, NULL, 1, &gameTaskHandle, 1);
  //xTaskCreatePinnedToCore(bleTask, "BLE Task", 4000, NULL, 1, &bleTaskHandle, 0);
}

void loop() {
  // Rien ici, tout est géré par les tâches FreeRTOS
}

