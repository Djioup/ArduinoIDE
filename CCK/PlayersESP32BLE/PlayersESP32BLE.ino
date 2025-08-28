#include <WiFi.h>
#include <PubSubClient.h>
#include "ESP32BleAdvertise.h"
#include <string>
#include <DFRobotDFPlayerMini.h>

SimpleBLE ble;

// Informations Wi-Fi
const char* ssid = "PelucheGang";
const char* password = "CACHE-CACHEKILLER";

// Adresse du broker MQTT
const char* mqtt_server = "192.168.0.139";

// Identifiants MQTT
const char* mqtt_user = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";
#define LED 13

const char* esp32_id = "Player6"; // Identifiant unique pour cet ESP32

bool detection = false;
bool invulnerable = false;
float reperage = 0;
float diff = 1;
float difflvl = 1;
int vol = 0;

String Player = String(2);
String Etat = "0"; // Etat initial = "0" (IR non détecté)

// MQTT
WiFiClient espClient;
PubSubClient client(espClient);

// Capteur IR
const int irSensorPin = 35; // Pin analogique pour le capteur IR
const int detectionThreshold = 4000; // Seuil de détection

// Temps entre deux publications MQTT
const unsigned long mqttInterval = 250; // En millisecondes
unsigned long invTime = millis();
unsigned long LedTime = millis();
unsigned long lastMqttPublishTime = 0;
float LedInt = 800;

// ***** Ajout de la gestion audio avec DFPlayerMini *****
#define RX_PIN 16  // Broche RX pour DFPlayer
#define TX_PIN 17  // Broche TX pour DFPlayer
DFRobotDFPlayerMini myDFPlayer;
bool isIRMode = false; // false : piste 1, true : piste 2
bool onPlay = false;

// Tâches
TaskHandle_t networkTaskHandle;
TaskHandle_t gameLogicTaskHandle;
TaskHandle_t bleAdvertisingTaskHandle; // Handle de la tâche FreeRTOS pour le BLE

// Prototypes
void connectToWiFi();
void reconnectMQTT();
void resetWiFi();
void callback(char* topic, byte* payload, unsigned int length);
void sendDetectionMessage(const char* message);
void networkTask(void* parameter);
void gameLogicTask(void* parameter);
void WiFiEvent(WiFiEvent_t event);
//void updateBLEAdvertising();


//---------------------------------------------------------------------------------------------------------------------------------------Initialisation  -------------------------------------------------------------------------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialisation du DFPlayerMini
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(2000);
  if (!myDFPlayer.begin(Serial2)) {
    Serial.println("Impossible de communiquer avec le DFPlayer Mini !");
    while (true);
  }
  Serial.println("DFPlayer Mini initialisé.");
  // La piste 1 est lue en boucle au volume 0
  delay(1000);
  myDFPlayer.volume(0);
  delay(2000);
  // myDFPlayer.play(1);
  // delay(2000);

  //
  Serial.println("Beacon initialized and advertising!");

  // Création des tâches sur les deux cœurs
  xTaskCreatePinnedToCore(
      networkTask,   // Fonction de la tâche
      "NetworkTask", // Nom de la tâche
      4096,          // Taille de la pile
      NULL,          // Paramètre passé à la tâche
      1,             // Priorité de la tâche
      &networkTaskHandle, // Handle de la tâche
      0              // Cœur 0 (Wi-Fi et MQTT)
  );

  xTaskCreatePinnedToCore(
      gameLogicTask, // Fonction de la tâche
      "GameLogicTask", // Nom de la tâche
      4096,            // Taille de la pile
      NULL,            // Paramètre passé à la tâche
      1,               // Priorité de la tâche
      &gameLogicTaskHandle, // Handle de la tâche
      1                // Cœur 1 (Logique de jeu)
  );
  
  xTaskCreatePinnedToCore(
    bleAdvertisingTask,              // Fonction de la tâche
    "BLEAdvertisingTask", // Nom de la tâche
    4096,                 // Taille de la pile
    NULL,                 // Paramètre passé à la tâche
    1,                    // Priorité de la tâche
    &bleAdvertisingTaskHandle,       // Handle de la tâche
    0                     // Cœur 0
  );
}

void loop() {
  // Rien ici, tout est géré dans les tâches.
}

//---------------------------------------------------------------------------------------------------------------------------------------Game Logic -------------------------------------------------------------------------------------------------------------------------------------------

// Tâche pour gérer la logique du jeu
void gameLogicTask(void* parameter) {
  int lastReperage;
  while (true) {
    int irValue = analogRead(irSensorPin);
    Serial.print("Valeur IR : ");
    Serial.println(irValue);

    // ---- Gestion du repérage ----
    if (irValue < detectionThreshold && !invulnerable) { // Si on détecte une lumière IR
      if (!onPlay && !invulnerable) {
        Serial.println("Détection IR - Lecture de la piste 1");
        // vTaskDelay(150 / portTICK_PERIOD_MS);
        myDFPlayer.volume((int)reperage * 2 + 3);
        vTaskDelay(150 / portTICK_PERIOD_MS);
        myDFPlayer.play(1);
        vTaskDelay(300 / portTICK_PERIOD_MS);
        onPlay = true;
      }   
    

      // Incrémentation du repérage
      if (millis() - lastMqttPublishTime > (mqttInterval * difflvl) && !invulnerable) {
        reperage += 1;
        lastMqttPublishTime = millis();
         Serial.println(reperage);
           // Mise à jour du volume en fonction du repérage
      vol = (int)(reperage * 2) + 3;
      //if (vol > 20) vol = 20;  // Limite le volume max
      // vTaskDelay(150 / portTICK_PERIOD_MS);
      myDFPlayer.volume(vol);
      Serial.println("Changement volume");
      // vTaskDelay(300 / portTICK_PERIOD_MS);
      }
    }

      // Déclenchement de la piste 2 si repérage atteint 10
      if (reperage >= 10 && !invulnerable) {
        Serial.println("Repérage max atteint - Lecture de la piste 2");
      // vTaskDelay(150 / portTICK_PERIOD_MS);
        myDFPlayer.volume(30);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        myDFPlayer.play(2);
        vTaskDelay(300 / portTICK_PERIOD_MS); // Attendre la fin de la piste 2
        Etat = "1";  // Passe en mode IR
        invTime = millis();
        reperage = 0;
        invulnerable = true;
        sendDetectionMessage("IR_detected");
        digitalWrite(LED, HIGH);
      }
    
    if (irValue > detectionThreshold && Etat != "1"){ // Si on ne détecte plus la lumière IR
      if (onPlay) {
        Serial.println("Perte du signal IR - Arrêt du son");
          // vTaskDelay(300 / portTICK_PERIOD_MS);
          myDFPlayer.volume(0);
          // vTaskDelay(300 / portTICK_PERIOD_MS);
          onPlay = false;
        }    
        reperage -= 0.3;
        if (reperage < 0)
        {
          reperage = 0;
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);    
      }
    

    //---- Gestion de l'invulnérabilité après la piste 2 ----
    // if (millis() - invTime < 10000 && detection) {
    //   invulnerable = true;
    //   detection = false;
    // }

    if (millis() - invTime >= 10000 && invulnerable) {
      invulnerable = false;
      reperage = 0;
      sendDetectionMessage("IR_notdetected");
      Etat = "0";  // Retour au mode normal
       onPlay = false;
    }

    vTaskDelay(100 / portTICK_PERIOD_MS); // Pause pour éviter une boucle trop rapide
  }
}


//---------------------------------------------------------------------------------------------------------------------------------------BLE et Wifi -------------------------------------------------------------------------------------------------------------------------------------------
// Tâche pour gérer le réseau (Wi-Fi et MQTT)
void networkTask(void* parameter) {  
  vTaskDelay(5000 / portTICK_PERIOD_MS);
  connectToWiFi();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  while (true) {
    if (!client.connected()) {
      reconnectMQTT();
    }
    client.loop();
    vTaskDelay(10 / portTICK_PERIOD_MS); // Délai pour éviter de saturer le processeur
  }
}

void bleAdvertisingTask(void* parameter) {
  while(true)
  {
    // Mise à jour périodique du BLE
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Délai pour éviter une boucle trop rapide

    // Combine Player et Etat dans une seule chaîne
    String combinedData = "P:" + Player + ";E:" + Etat;
    
    // Diffusion des données combinées
    ble.advertise(combinedData);
  }
}

//---------------------------------------------------------------------------------------------------------------------------------------Gestion Réseau et MQTT -------------------------------------------------------------------------------------------------------------------------------------------

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case WIFI_EVENT_STA_DISCONNECTED: // Remplace SYSTEM_EVENT_STA_DISCONNECTED
      Serial.println("Wi-Fi déconnecté. Tentative de reconnexion...");
      connectToWiFi();
      break;

    case IP_EVENT_STA_GOT_IP: // Remplace SYSTEM_EVENT_STA_GOT_IP
      Serial.println("Wi-Fi reconnecté !");
      Serial.print("Nouvelle adresse IP : ");
      Serial.println(WiFi.localIP());
      break;

    default:
      break;
  }
}

// Connexion au Wi-Fi
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
  String willTopic = String("esp32/") + String(esp32_id) + "/status";
  String willMessage = String(esp32_id) + ":offline";

  int attemptCount = 0;
  while (!client.connected()) {
    Serial.println("Connexion au broker MQTT...");
    
    if (client.connect(esp32_id, mqtt_user, mqtt_password, willTopic.c_str(), 1, true, willMessage.c_str())) {
      Serial.println("Connecté au broker MQTT");
      String onlineMessage = String(esp32_id) + ":online";
      client.publish(willTopic.c_str(), onlineMessage.c_str(), true);
      client.subscribe("unity/commandes");
      sendDetectionMessage("on");
      sendDetectionMessage("Request");
      attemptCount = 0; // Réinitialisation du compteur d'échec
    } else {
      Serial.print("Échec, rc=");
      Serial.print(client.state());
      Serial.println(". Nouvelle tentative dans 5 secondes.");
      attemptCount++;

      // Si trop d'échecs, redémarrer la connexion Wi-Fi
      if (attemptCount >= 5) {
        Serial.println("Trop d'échecs de connexion MQTT, redémarrage du Wi-Fi...");
        resetWiFi();
        attemptCount = 0; // Réinitialiser le compteur après reconnexion
      }

      vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
  }
}

void resetWiFi() {
  WiFi.disconnect(true); // Déconnecte du réseau et réinitialise les paramètres Wi-Fi
  vTaskDelay(2000 / portTICK_PERIOD_MS); // Pause pour garantir une réinitialisation propre
  connectToWiFi();       // Reconnecte au Wi-Fi
}

// Callback pour traiter les messages MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Message reçu sur le topic ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  if (message.startsWith(String("Player") + ":difficulte")) {
    int startIdx = message.indexOf("(") + 1;
    int endIdx = message.indexOf(")");
    String difficultyStr = message.substring(startIdx, endIdx);
    float difficultyLevel = difficultyStr.toFloat();
    difflvl = 1 - (difficultyLevel / 20);
    Serial.print("Commande de détection diff reçue : ");
    Serial.println(difflvl);
  }

    // Traiter les commandes reçues
  if (message.endsWith("WIN")) {
    Serial.println("Commande de WIN reçue de Unity.");
       
        vTaskDelay(100 / portTICK_PERIOD_MS);
        myDFPlayer.volume(30);
        vTaskDelay(200 / portTICK_PERIOD_MS);
        myDFPlayer.play(4);
        vTaskDelay(200 / portTICK_PERIOD_MS);
  }

     // Traiter les commandes reçues
  if (message.endsWith("LOOSE")) {
    Serial.println("Commande de WIN reçue de Unity.");
       
        vTaskDelay(100 / portTICK_PERIOD_MS);
        myDFPlayer.volume(30);
        vTaskDelay(200 / portTICK_PERIOD_MS);
        myDFPlayer.play(3);
        vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

// Envoi d'un message MQTT
void sendDetectionMessage(const char* message) {
  String messageToSend = String(esp32_id) + ":" + message;
  client.publish("esp32/donnees", messageToSend.c_str());
  Serial.println("Message envoyé : " + messageToSend);
}
