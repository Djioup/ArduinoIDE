#include <WiFi.h>
#include <PubSubClient.h>
#include "ESP32BleAdvertise.h"
#include <string>

SimpleBLE ble;

// Informations Wi-Fi
const char* ssid = "ESP32_Local_Network";
const char* password = "DjioopPod";

// Adresse du broker MQTT
const char* mqtt_server = "192.168.5.192";

// Identifiants MQTT
const char* mqtt_user = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";
#define LED 13

const char* esp32_id = "Player1";  // Identifiant unique pour cet ESP32

bool detection = false;
bool invulnerable = false;
float reperage = 0;
float diff = 1;
float difflvl = 1;

String Player = String(1);
String Etat;
// MQTT
WiFiClient espClient;
PubSubClient client(espClient);

// Capteur IR
const int irSensorPin = 35;           // Pin analogique pour le capteur IR
const int detectionThreshold = 4000;  // Seuil de détection

// Temps entre deux publications MQTT
const unsigned long mqttInterval = 250;  // En millisecondes
unsigned long invTime = millis();
unsigned long LedTime = millis();
unsigned long lastMqttPublishTime = 0;
float LedInt = 800;


// Tâches
TaskHandle_t networkTaskHandle;
TaskHandle_t gameLogicTaskHandle;
TaskHandle_t bleAdvertisingTaskHandle;  // Handle de la tâche FreeRTOS pour le BLE

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

  // Configuration de la broche du capteur IR
  pinMode(irSensorPin, INPUT);
  pinMode(LED, OUTPUT);

  // Gestionnaire d'événements Wi-Fi
  WiFi.onEvent(WiFiEvent);

  ble.begin("ESP32:Player1");  //sets the device name

  Serial.println("Beacon initialized and advertising!");

  // Création des tâches sur les deux cœurs
  xTaskCreatePinnedToCore(
    networkTask,         // Fonction de la tâche
    "NetworkTask",       // Nom de la tâche
    4096,                // Taille de la pile
    NULL,                // Paramètre passé à la tâche
    1,                   // Priorité de la tâche
    &networkTaskHandle,  // Handle de la tâche
    0                    // Cœur 0 (Wi-Fi et MQTT)
  );

  xTaskCreatePinnedToCore(
    gameLogicTask,         // Fonction de la tâche
    "GameLogicTask",       // Nom de la tâche
    4096,                  // Taille de la pile
    NULL,                  // Paramètre passé à la tâche
    1,                     // Priorité de la tâche
    &gameLogicTaskHandle,  // Handle de la tâche
    1                      // Cœur 1 (Logique de jeu)
  );
  xTaskCreatePinnedToCore(
    bleAdvertisingTask,         // Fonction de la tâche
    "BLEAdvertisingTask",       // Nom de la tâche
    4096,                       // Taille de la pile
    NULL,                       // Paramètre passé à la tâche
    1,                          // Priorité de la tâche
    &bleAdvertisingTaskHandle,  // Handle de la tâche
    0                           // Cœur 0
  );
}


void loop() {
  // Rien ici, tout est géré dans les tâches.
}

//---------------------------------------------------------------------------------------------------------------------------------------Game Logic -------------------------------------------------------------------------------------------------------------------------------------------

// Tâche pour gérer la logique du jeu
void gameLogicTask(void* parameter) {
  while (true) {
    int irValue = analogRead(irSensorPin);
    Serial.print("Valeur IR : ");
    Serial.println(irValue);

    if (irValue < 4000 && millis() - lastMqttPublishTime > (mqttInterval * difflvl) && !invulnerable) {
      reperage += 1;
      lastMqttPublishTime = millis();
      if (!detection) {
        //sendDetectionMessage("WarningPlayer");
      }
      detection = true;

      if (reperage >= 10 && !invulnerable) {
        sendDetectionMessage("IR_detected");
        Etat = String(1);
        invTime = millis();
        reperage = 0;
        detection = false;
        digitalWrite(LED, HIGH);
      }
    }

    if (millis() - invTime < 5000) {
      invulnerable = true;
    }

    if (millis() - invTime >= 5000 && invulnerable) {
      invulnerable = false;
      sendDetectionMessage("IR_notdetected");
      Etat = String(0);
    }

    if (reperage > 0 && !invulnerable) {
      if (millis() - LedTime > (LedInt / reperage)) {
        digitalWrite(LED, digitalRead(LED) == HIGH ? LOW : HIGH);
        LedTime = millis();
      }
    }

    if (reperage <= 0 && !invulnerable) {
      digitalWrite(LED, LOW);
    }

    if (irValue >= 4000 && millis() - lastMqttPublishTime > (mqttInterval * difflvl)) {
      lastMqttPublishTime = millis();
      if (reperage > 0) {
        reperage -= 0.3;
      }
      if (reperage <= 0 && detection) {
        sendDetectionMessage("IR_notdetected");
        detection = false;
        reperage = 0;
      }
    }

    vTaskDelay(80 / portTICK_PERIOD_MS);  // Délai pour éviter une boucle trop rapide
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
    vTaskDelay(10 / portTICK_PERIOD_MS);  // Délai pour éviter de saturer le processeur
  }
}

void bleAdvertisingTask(void* parameter) {

  while (true) {
    // Example: Update player state periodically
    vTaskDelay(1000 / portTICK_PERIOD_MS);  // Délai pour éviter une boucle trop rapide

    // Combine Player et Etat dans une seule chaîne
    String combinedData = "P:" + Player + ";E:" + Etat;

    // Utilisez un seul appel pour diffuser les données combinées
    ble.advertise(combinedData);
  }
}


// Gestionnaire d'événements Wi-Fi
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case WIFI_EVENT_STA_DISCONNECTED:  // Remplace SYSTEM_EVENT_STA_DISCONNECTED
      Serial.println("Wi-Fi déconnecté. Tentative de reconnexion...");
      connectToWiFi();
      break;

    case IP_EVENT_STA_GOT_IP:  // Remplace SYSTEM_EVENT_STA_GOT_IP
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
      attemptCount = 0;  // Réinitialisation du compteur d'échec
    } else {
      Serial.print("Échec, rc=");
      Serial.print(client.state());
      Serial.println(". Nouvelle tentative dans 5 secondes.");
      attemptCount++;

      // Si trop d'échecs, redémarrer la connexion Wi-Fi
      if (attemptCount >= 5) {
        Serial.println("Trop d'échecs de connexion MQTT, redémarrage du Wi-Fi...");
        resetWiFi();
        attemptCount = 0;  // Réinitialiser le compteur après reconnexion
      }

      vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
  }
}

void resetWiFi() {
  WiFi.disconnect(true);                  // Déconnecte du réseau et réinitialise les paramètres Wi-Fi
  vTaskDelay(2000 / portTICK_PERIOD_MS);  // Pause pour garantir une réinitialisation propre
  connectToWiFi();                        // Reconnecte au Wi-Fi
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
}

// Envoi d'un message MQTT
void sendDetectionMessage(const char* message) {
  String messageToSend = String(esp32_id) + ":" + message;
  client.publish("esp32/donnees", messageToSend.c_str());
  Serial.println("Message envoyé : " + messageToSend);
}
