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

const char* esp32_id = "Generator0"; // Identifiant unique pour cet ESP32

// MQTT
WiFiClient espClient;
PubSubClient client(espClient);

// LEDs et boutons
const int signalLedPins[] = {13, 12, 14, 27, 26}; // LEDs de signal
const int buttonLedPins[] = {22, 4};  // LEDs analogiques des boutons
const int buttonPins[] = {23, 15, 5, 18, 19};      // Boutons
const int ledLevel[] = {32, 33, 21}; // LEDs pour le level
 
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
unsigned long blinkTimerLL = 0; // Temps pour gérer le clignotement du bouton rouge
unsigned long lastPressTime = 0; // Temps du dernier appui pour gérer l'anti-rebond
// unsigned long effectStartTime = 0; // Temps de début pour l'effet visuel des LEDs des boutons
const unsigned long debounceDelay = 600; // Délai pour l'anti-rebond
bool blinkState = false; // État actuel du clignotement
bool blinkStateLL = false; // État actuel du clignotement
bool disableLedEffects = false; // Drapeau pour désactiver les effets LED
bool UnityOrderReset = false;
bool Celebrity = false;
int SpottedPlayer = 0;
unsigned long pauseTimer = 0; // Timer pour gérer la pause
bool inPause = false; // Indicateur de pause


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
  GAME_WON_FINAL,  
  INACTIF 
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

//-------------------------------------------------------------------------------------------------------- Initialisation ----------------------------------------------------------------------------------------------------------------------------------------------

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
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }

   for (int i = 0; i < 2; i++) {
    pinMode(buttonLedPins[i], OUTPUT);
    analogWrite(buttonLedPins[i], 0);
    vTaskDelay(10 / portTICK_PERIOD_MS);
   }

  for (int i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }

      for (int i = 0; i < 3; i++)
    {
      pinMode(ledLevel[i],OUTPUT);
      vTaskDelay(10 / portTICK_PERIOD_MS);
      // digitalWrite(ledLevel[i], HIGH);
    }

  xTaskCreatePinnedToCore(mqttTask, "MQTT Task", 4096, NULL, 1, &mqttTaskHandle, 0);
  xTaskCreatePinnedToCore(bleScanTask, "BLEScanTask", 4096, NULL, 1, &bleScanTaskHandle, 0);
  xTaskCreatePinnedToCore(gameTask, "Game Task", 8192, NULL, 1, &gameTaskHandle, 1);

  Serial.println("DualCore Configure");
}

void loop() {
  // Rien ici, tout est géré par les tâches FreeRTOS
}

//--------------------------------------------------------------------------------------------------------Game Logic ----------------------------------------------------------------------------------------------------------------------------------------------

void gameTask(void* parameter) {
    // Serial.println("GameTask Démarré");
  while (true) {
    if (SpottedPlayer == 1) {
      setupGameBlockedState();
      vTaskDelay(100 / portTICK_PERIOD_MS);  
      // Serial.println("GameBlock");      
      }
      //  
      // continue;  

    ledLevelTask ();    

    switch (currentState) {
      case INITIAL:
        handleInitialState();
        // Serial.println("Init");
        break;
      case GAME_STARTED:
        handleGameStartedState();
        // Serial.println("GameStart");
        break;
      case GAME_FAILED:
        handleGameFailedState();        
        // Serial.println("GameFailed");
        break;
      case GAME_WON_LEVEL:
        handleGameWonLevelState();        
        // Serial.println("GameWon");
        break;
      case GAME_BLOCKED:
        handleGameBlockedState();        
        // Serial.println("Gameblock2");
        break;
      case GAME_WON_FINAL:
        handleGameWonFinalState();                
        // Serial.println("GameWonFinal");
        break;
      case INACTIF:  // ✅ Ajouter ce cas
        handleGameInactiveState();
        break;
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
      
  }
}

void ledLevelTask() {
  
    if (millis() - blinkTimerLL >= 1000) {
      // Serial.println("Gametask : BlinkLedLvl"); 
      // blinkTimerLL = millis();
      // blinkStateLL = !blinkStateLL;
      
      // if (level > 0) {
      //   digitalWrite(ledLevel[level - 1], blinkStateLL);
      // }
      if (level > 1) {
        digitalWrite(ledLevel[level - 2], HIGH);
      }
      if (level > 2) {
        digitalWrite(ledLevel[level - 3], HIGH);
      }
      if (Celebrity)
      {
        digitalWrite(ledLevel[2], HIGH);
      }
    }
    vTaskDelay(15 / portTICK_PERIOD_MS); // Petit délai pour ne pas saturer le CPU
  
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

void setupGameInactiveState() {
    currentState = INACTIF;
    stateStartTime = millis();
    NotifyMQTT("off");
    Serial.println("⚠️ L'appareil est maintenant INACTIF.");

    // Éteindre toutes les LEDs
    for (int i = 0; i < numLeds; i++) {
        digitalWrite(signalLedPins[i], LOW);
        vTaskDelay(15 / portTICK_PERIOD_MS);
    }
    for (int i = 0; i < 2; i++) {
        analogWrite(buttonLedPins[i], 0);
        vTaskDelay(15 / portTICK_PERIOD_MS);
    }
}

void handleGameInactiveState() {
    // Rester dans cet état jusqu'à un message "actif"
    vTaskDelay(100 / portTICK_PERIOD_MS);
}


void setupInitialState() {
        // Serial.println("Gametask : Setup Initial"); 
  currentState = INITIAL;
  stateStartTime = millis();
  NotifyMQTT("on");
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(signalLedPins[i], LOW);        
    vTaskDelay(15 / portTICK_PERIOD_MS);
  }
    for (int i = 0; i < 2; i++) {    
    analogWrite(buttonLedPins[i], 0);        
    vTaskDelay(15 / portTICK_PERIOD_MS);
  }
  // Serial.println("Initial State Set");
}

void handleInitialState() {
          // Serial.println("Gametask : Handle Initial"); 
  if (millis() - blinkTimer >= 500) {
    blinkTimer = millis();
    blinkState = !blinkState;
    digitalWrite(signalLedPins[1], blinkState);
    analogWrite(buttonLedPins[1], blinkState*255);
  }

  if (digitalRead(buttonPins[1]) == LOW && millis() - lastPressTime > debounceDelay) {
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
    digitalWrite(signalLedPins[i], LOW);
    vTaskDelay(15 / portTICK_PERIOD_MS);
  }

  //   for (int i = 0; i < 2; i++) {
  //   analogWrite(buttonLedPins[i], 255);
  //   vTaskDelay(10 / portTICK_PERIOD_MS);
  // }

  generateRandomSequence();
  stepStartTime = millis();
  // Serial.println("Game Started State Set");
}

void handleGameStartedState() {
  if (playerFailed) {
    setupGameFailedState();
    return;
  }

 if (!inPause && millis() - stepStartTime > inputTimeout) {
    playerFailed = true;
    setupGameFailedState();
    return;
  }

  if (!inPause){
  digitalWrite(signalLedPins[sequence[currentInputIndex]], HIGH);
  vTaskDelay(15 / portTICK_PERIOD_MS);
  }

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
    vTaskDelay(15 / portTICK_PERIOD_MS);
  }
  for (int i = 0; i < 2; i++) {
    analogWrite(buttonLedPins[i], 0);
    vTaskDelay(15 / portTICK_PERIOD_MS);
  }
  digitalWrite(signalLedPins[1],HIGH);
  analogWrite(buttonLedPins[1], 255);
  // Serial.println("Game Failed State Set");
}

void handleGameFailedState() {
  if (millis() - stateStartTime >= 6000) {
    setupInitialState();
  }
}

void setupGameWonLevelState() {
  currentState = GAME_WON_LEVEL;
  stateStartTime = millis();
  level += 1;
  notifyMQTT((String("generateur lvl ") + String(level)).c_str());
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(signalLedPins[i],HIGH);
    vTaskDelay(15 / portTICK_PERIOD_MS);
  }
    for (int i = 0; i < 2; i++) {
    analogWrite(buttonLedPins[i], 255);
    vTaskDelay(15 / portTICK_PERIOD_MS);
  }
  // Serial.println("Game Won Level State Set");
}

void handleGameWonLevelState() {
  if (millis() - stateStartTime < 5000 && millis() - blinkTimer >= 500) {
    blinkTimer = millis();
    blinkState = !blinkState;
    for (int i = 0; i < numLeds; i++){
    digitalWrite(signalLedPins[i], blinkState);
    vTaskDelay(15 / portTICK_PERIOD_MS);
    }
    for (int i = 0; i < 2; i++){
    analogWrite(buttonLedPins[i], blinkState*255);
    vTaskDelay(15 / portTICK_PERIOD_MS);
    }
  }
  if (millis() - stateStartTime >= 5000) {
    setupInitialState();
  }
}

void setupGameBlockedState() {
  currentState = GAME_BLOCKED;
  stateStartTime = millis();
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(signalLedPins[i], LOW);
    vTaskDelay(15 / portTICK_PERIOD_MS);
  }
    for (int i = 0; i < 2; i++) {
    analogWrite(buttonLedPins[i], 0);
    vTaskDelay(15 / portTICK_PERIOD_MS);
  }
  // Serial.println("Game Blocked State Set");
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
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
    for (int i = 0; i < 2; i++) {
    analogWrite(buttonLedPins[i], 255);
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  celebrateWin();
  // Serial.println("Game Won Final State Set");
}

void handleGameWonFinalState() {
  // Remains in this state until reset command received
}

void generateRandomSequence() {
  // Générer le premier élément de la séquence (différent de buttonPins[1])
  do {
    sequence[0] = random(0, numButtons);
  } while (sequence[0] == 1); // Empêcher que le premier bouton soit `buttonPins[1]`

  // Générer le reste de la séquence en évitant les doublons successifs
  for (int i = 1; i < level + 5; i++) {
    int newButton;
    do {
      newButton = random(0, numButtons);
    } while (newButton == sequence[i - 1]); // Éviter les répétitions consécutives

    sequence[i] = newButton;
  }
}


  // // Affichage pour débogage
  // Serial.print("Séquence générée : ");
  // for (int i = 0; i < level + 5; i++) {
  //   Serial.print(sequence[i]);
  //   Serial.print(" ");
  // }
  // Serial.println();

void processPlayerInput() {
  int buttonPressed = -1;

  // Détection du bouton pressé
  for (int i = 0; i < numButtons; i++) {
    if (digitalRead(buttonPins[i]) == LOW) {
      buttonPressed = i;
      break; // Arrêter la boucle dès qu'un bouton est détecté
    }
  }

  // Gestion de la pause avant le prochain bouton
  if (inPause) {
    if (millis() - pauseTimer >= debounceDelay) {
      inPause = false; // Fin de la pause, on peut passer au bouton suivant      
      stepStartTime = millis(); // Reset du timer d'étape
      for (int i = 0; i < 2; i++) {
        analogWrite(buttonLedPins[i], 255); // Rallumer les LED des boutons
      }
    }
    return; // Sortir pour ne pas traiter d'autres entrées pendant la pause
  }

  // Vérification anti-rebond & appui du bon bouton
  if (buttonPressed != -1 && (millis() - lastPressTime > debounceDelay)) {
    lastPressTime = millis();

    if (buttonPressed == sequence[currentInputIndex]) {
      // Éteindre la LED associée au bouton correctement pressé
      digitalWrite(signalLedPins[sequence[currentInputIndex]], LOW);
      vTaskDelay(15/ portTICK_PERIOD_MS);
      Serial.println("Correct input");

      // Démarrer la pause
      for (int i = 0; i < 2; i++) {
        analogWrite(buttonLedPins[i], 0); // Éteindre LED des boutons
        vTaskDelay(15/ portTICK_PERIOD_MS);
      }
      pauseTimer = millis(); // Enregistrer le début de la pause
      inPause = true; // Passer en mode pause
      currentInputIndex++;
     

      // Vérifier si le joueur a terminé la séquence
      if (currentInputIndex >= level + 5) {
        if (level == 3) {
          setupGameWonFinalState();
        } else {
          setupGameWonLevelState();
        }
      }
    } else {
      playerFailed = true;
      Serial.println("Incorrect input");
      setupGameFailedState();
    }
  }
}




void updateButtonLedEffect() {
  // Vérifier si on est en pause : éteindre les LED des boutons et ne rien faire
  if (inPause) {
    for (int i = 0; i < 2; i++) {
      analogWrite(buttonLedPins[i], 0); // Éteindre LED des boutons
      vTaskDelay(15/ portTICK_PERIOD_MS);
    }
    return;
  }

  // Si pas en pause, faire l'effet normal
  unsigned long elapsed = millis() - stepStartTime;
  unsigned long brightness = map(elapsed, 0, inputTimeout, 255, 0);

  for (int i = 0; i < 2; i++) {
    analogWrite(buttonLedPins[i], brightness);
  }
}



// Réinitialisation du jeu
void resetGame() { 
 
  disableLedEffects = true; // Désactiver les effets LED

  for (int i = 0; i < numLeds; i++) {   
    digitalWrite(signalLedPins[i], LOW); // Éteindre les LEDs de signal 
    vTaskDelay(15/ portTICK_PERIOD_MS);    
  }
   for (int i = 0; i < 2; i++) {
    analogWrite(buttonLedPins[i], 0); // Éteindre les LEDs des boutons
    vTaskDelay(15/ portTICK_PERIOD_MS);
  }

  for (int i = 0; i < 3; i++)
  {
    digitalWrite (ledLevel[i], LOW);
  }
  level = 1;
  currentInputIndex = 0;
  playerFailed = false;
  //notifyMQTT("Generateur en attente");

  // Éteindre toutes les LEDs des boutons et de signal
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(signalLedPins[i], LOW);
    vTaskDelay(15/ portTICK_PERIOD_MS);
  }
    for (int i = 0; i < 2; i++) {
    analogWrite(buttonLedPins[i], 0);
    vTaskDelay(15/ portTICK_PERIOD_MS);
  }

  // generateRandomSequence();

  disableLedEffects = false; // Réactiver les effets LED après réinitialisation
  
}

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
        digitalWrite(signalLedPins[i], ledState ? HIGH : LOW);
        vTaskDelay(10/ portTICK_PERIOD_MS);
      }
      for (int i = 0; i < 2; i++) {
        analogWrite(buttonLedPins[i], ledState ? 255 : 0);
        vTaskDelay(10/ portTICK_PERIOD_MS);
      }
    }
  }
  // Éteindre toutes les LEDs après la victoire
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(signalLedPins[i], HIGH);
    vTaskDelay(10/ portTICK_PERIOD_MS);
  }
    for (int i = 0; i < 2; i++) {
    analogWrite(buttonLedPins[i], 255);
    vTaskDelay(10/ portTICK_PERIOD_MS);
  }

  disableLedEffects = false; // Réactiver les effets LED après la célébration
}
}

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
  // Lancer un scan
  while(true)
  {
  Serial.println("Scan BLE en cours...");
  pBLEScan->start(SCAN_TIME, false);  // Scan en arrière-plan pour SCAN_TIME secondes
  vTaskDelay(100/ portTICK_PERIOD_MS);
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
      reconnectAttempts = 0; // Réinitialise le compteur d'échecs
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
  if (message.endsWith("reset")) {
    Serial.println("Commande de reset reçue de Unity.");
    resetGame();
  }

  if (message.startsWith(String("Generator:difficulte("))) {
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

  if (message == String(esp32_id) + ":generateur reparer") {
      setupGameWonFinalState();
  }

  if (message == String(esp32_id) + ":generateur lvl 1") {
      level = 1;
  }

  if (message == String(esp32_id) + ":generateur lvl 2") {
      level = 2;
  }

  if (message == String(esp32_id) + ":inactif") {
    Serial.println("🚨 Mode INACTIF activé via MQTT.");
    setupGameInactiveState();
  }

  if (message == String(esp32_id) + ":actif") {
      Serial.println("✅ Mode ACTIF activé via MQTT.");
      setupInitialState();
  }
}

// Notifier le serveur MQTT
void notifyMQTT(const char* message) {
    String fullMessage = String(esp32_id) + ":" + message; // Préfixer le message par l'identifiant
    client.publish("esp32/donnees", fullMessage.c_str());
}
