#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DFRobotDFPlayerMini.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>

// Informations Wi-Fi
const char* ssid = "PelucheGang";
const char* password = "CACHE-CACHEKILLER";

// Adresse du broker MQTT
const char* mqtt_server = "192.168.0.139";
const char* mqtt_user = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";
const char* esp32_id = "Peluche2"; // Identifiant unique pour cet ESP32

// MQTT
WiFiClient espClient;
PubSubClient client(espClient);

// Définir les broches pour le capteur ultrason
#define TRIG_PIN 14  // GPIO pour Trig
#define ECHO_PIN 27  // GPIO pour Echo

// Capteur PIR
#define LED 12
#define LED3 5
#define LED2 15
#define LED4 19

// Définir les broches pour le DFPlayer Mini
#define RX_PIN 16
#define TX_PIN 17

#define MAX_PLAYERS 10

#define PIR_PIN 26

int led;
bool loose;
bool isplaying = false;
bool isplaying2 = false;
bool PelucheActive = false;
bool WinGate = false;
bool EndLoop = false;
unsigned long LastSpot = millis();
unsigned long CurrentTime = millis();
unsigned long End = millis();
unsigned long LoopTime = millis();
unsigned long LedTime = millis();
unsigned long ShortVoice = millis();
int vitDetection = 1000;
float vitDiff = 1;
int distanceDetection = 250;
DFRobotDFPlayerMini myDFPlayer;
bool noDetection = false;
bool mouvementDetecte = false;
bool OnMSG;
bool OffMSG;

int RSSI[MAX_PLAYERS];
int Player[MAX_PLAYERS];

int closestPlayer = -1;
int maxRSSI = -1000; // Valeur initiale très faible

// BLE
BLEScan* pBLEScan;
#define SCAN_TIME 5
int SpottedPlayer[MAX_PLAYERS];

// Tâches FreeRTOS
TaskHandle_t wifiTaskHandle;
TaskHandle_t gameLogicTaskHandle;
TaskHandle_t bleScanTaskHandle;

// Prototypes
void connectToWiFi();
void reconnectMQTT();
void callback(char* topic, byte* payload, unsigned int length);
void sendMQTTMessage(const char* message);
void wifiTask(void* parameter);
void gameLogicTask(void* parameter);
void bleScanTask(void* parameter);
void determineClosestPlayer();

//-------------------------------------------------------------------------------------------------------------------------- Initialisation -------------------------------------------------------------------------------------------------------------------------

// Classe pour gérer les callbacks BLE
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveName() && String(advertisedDevice.getName().c_str()).startsWith("ESP32:Player")) {
      Serial.println("Cible détectée !");
      Serial.print("RSSI : ");
      Serial.println(advertisedDevice.getRSSI());

      if (advertisedDevice.haveManufacturerData()) {
        String manufacturerData = advertisedDevice.getManufacturerData();
        Serial.print("Combined Data: ");
        Serial.println(manufacturerData);

        int playerIndex = manufacturerData.indexOf("P:");
        int etatIndex = manufacturerData.indexOf("E:");
        if (playerIndex != -1 && etatIndex != -1) {
          String player = manufacturerData.substring(playerIndex + 2, etatIndex - 1);
          String etat = manufacturerData.substring(etatIndex + 2);
          Serial.print("Player: ");
          Serial.println(player);
          Serial.print("Etat: ");
          Serial.println(etat);
          int playerId = player.toInt();
          if (playerId >= 0 && playerId < MAX_PLAYERS) {
            SpottedPlayer[playerId] = etat.toInt();
            RSSI[playerId] = advertisedDevice.getRSSI();
            Player[playerId] = playerId;
          }
        }
      }
    }
  }
};

void setup() {
  // Configuration des broches du capteur ultrason
  // pinMode(TRIG_PIN, OUTPUT);
  // pinMode(ECHO_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(LED4, OUTPUT);

  delay(1000);
  Serial.begin(115200);
  delay(2000);
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(2000);
  randomSeed(analogRead(0));  // Initialise l'aléatoire (exemple avec bruit analogique)


  // Initialisation du DFPlayer Mini
  Serial.println("Initialisation du DFPlayer Mini...");
  if (!myDFPlayer.begin(Serial2)) {
    Serial.println("Impossible de communiquer avec le DFPlayer Mini !");
    while (true);
  }
  Serial.println("DFPlayer Mini initialisé.");

  digitalWrite(LED, HIGH);
  digitalWrite(LED2, HIGH);
  digitalWrite(LED3, HIGH);
  digitalWrite(LED4, HIGH);

  // Initialisation BLE
  Serial.println("Initialisation du scanner BLE...");
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(20);
  pBLEScan->setWindow(20);

  // Création des tâches FreeRTOS
  xTaskCreatePinnedToCore(wifiTask, "WiFiTask", 4096, NULL, 1, &wifiTaskHandle, 0);
  xTaskCreatePinnedToCore(gameLogicTask, "GameLogicTask", 4096, NULL, 1, &gameLogicTaskHandle, 1);
  xTaskCreatePinnedToCore(bleScanTask, "BLEScanTask", 4096, NULL, 1, &bleScanTaskHandle, 0);
}

void loop() {
  // Rien ici, tout est géré dans les tâches FreeRTOS
}

//-------------------------------------------------------------------------------------------------------------------------- Wifi et BLE -------------------------------------------------------------------------------------------------------------------------

void wifiTask(void* parameter) {
  vTaskDelay(5000 / portTICK_PERIOD_MS);
  connectToWiFi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  while (true) {
    if (!client.connected()) {
      reconnectMQTT();
    }
    client.loop();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void bleScanTask(void* parameter) {
   vTaskDelay(15000 / portTICK_PERIOD_MS);
  while (true) {
    Serial.println("Scan BLE en cours...");
    pBLEScan->start(SCAN_TIME, false);
    determineClosestPlayer();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// Gestionnaire d'événements Wi-Fi
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case WIFI_EVENT_STA_DISCONNECTED:
      Serial.println("Wi-Fi déconnecté. Tentative de reconnexion...");
      connectToWiFi();
      break;

    case IP_EVENT_STA_GOT_IP:
      Serial.println("Wi-Fi reconnecté !");
      Serial.print("Nouvelle adresse IP : ");
      Serial.println(WiFi.localIP());
      break;

    default:
      break;
  }
}

void connectToWiFi() {
  Serial.println("Connexion au Wi-Fi...");
  WiFi.disconnect(true);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    Serial.print(".");
  }

  Serial.println("\nConnecté au Wi-Fi");
  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());
}


// Reconnexion MQTT avec LWT et gestion d'échecs multiples
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
      sendMQTTMessage("Request");
      attemptCount = 0; // Réinitialisation du compteur d'échec
    } else {
      Serial.print("Échec, rc=");
      Serial.print(client.state());
      Serial.println(". Nouvelle tentative dans 5 secondes.");
      attemptCount++;

      // Si trop d'échecs, redémarrer la connexion Wi-Fi
      if (attemptCount >= 5) {
        Serial.println("Trop d'échecs de connexion MQTT, redémarrage du Wi-Fi...");
        connectToWiFi();
        attemptCount = 0; // Réinitialiser le compteur après reconnexion
      }

      vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
  }
}


void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Message reçu sur ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

   if (message.endsWith(":reset")) {
    led = 0;
    isplaying = false;
    isplaying2 = false;
    EndLoop = false;
    WinGate = false;
    PelucheActive = false;
    Serial.println("Commande reset reçue");
  }

  if (message.startsWith(String("Peluche:difficulte(")))
  {
    Serial.println("Good");
    int startIdx = message.indexOf("(") + 1;
    int endIdx = message.indexOf(")");
    String difficultyStr = message.substring(startIdx, endIdx);
    float difficultyLevel = difficultyStr.toFloat();
    Serial.println("diffStr: " + String(difficultyLevel, 2)); // Affichage avec 2 décimales
    vitDiff = 1 - (difficultyLevel/20);
    Serial.println("diff: " + String(vitDiff, 2)); // Affichage avec 2 décimales
  }

  if (message == (String(esp32_id) + ":inactive"))
  {
    PelucheActive = false;
    OffMSG = true;
    sendMQTTMessage("off");
  }

  if (message == (String(esp32_id) + ":active"))
  {
    PelucheActive = true;
    OnMSG = true;
    Serial.println(PelucheActive);
    sendMQTTMessage("on");
  }  
   if (message == (String(esp32_id) + ":win2"))
  {
    PelucheActive = false;
    WinGate = true;
  }
}



void sendMQTTMessage(const char* message) {
  String fullMessage = String(esp32_id) + ":" + message;
  client.publish("esp32/donnees", fullMessage.c_str());
  Serial.println("Message envoyé : " + fullMessage);
}

void determineClosestPlayer() {  

  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (RSSI[i] > maxRSSI) {
      maxRSSI = RSSI[i];
      closestPlayer = Player[i];
    }
  }

  if (closestPlayer != -1) {
    Serial.print("Le joueur le plus proche est : ");
    Serial.print(closestPlayer);
    Serial.print(" avec un RSSI de : ");
    Serial.println(maxRSSI);
  } else {
    Serial.println("Aucun joueur détecté.");
  }
}

//-------------------------------------------------------------------------------------------------------------------------- Game Logic -------------------------------------------------------------------------------------------------------------------------

void gameLogicTask(void* parameter) {
  while (true) {

    // long duration =0;  // Timeout 30ms
    // bool distance = 0;  // Conversion en cm

    if (uint8_t state = myDFPlayer.readState() == 1)
    {
      noDetection = true;
    }
    else 
    {
      noDetection = false;
    }
    // if (!noDetection)
    // {
    // digitalWrite(TRIG_PIN, LOW);
    // delayMicroseconds(2);
    // digitalWrite(TRIG_PIN, HIGH);
    // delayMicroseconds(10);
    // digitalWrite(TRIG_PIN, LOW);
    
    // duration = pulseIn(ECHO_PIN, HIGH, 30000);  // Timeout 30ms
    // distance = (duration * 0.034) / 2;  // Conversion en cm
    // }

    int etatPIR = digitalRead(PIR_PIN);  

    // Attendre que le PIR revienne à LOW avant de réactiver la détection
    if (etatPIR == LOW && mouvementDetecte) {
        mouvementDetecte = false;  // Réinitialise la validation
    }

    if(!PelucheActive && WinGate)
    {
            digitalWrite(LED, HIGH);
        digitalWrite(LED2, HIGH);
        digitalWrite(LED3, HIGH);
        digitalWrite(LED4, HIGH);
        if(!EndLoop)
        {
        myDFPlayer.volume(30);
        myDFPlayer.loop(21);
        EndLoop = true;
        }
    }
    

    if (!PelucheActive)
    {
      digitalWrite(LED, LOW);
        digitalWrite(LED2, LOW);
        digitalWrite(LED3, LOW);
        digitalWrite(LED4, LOW);

        if (OffMSG)
        {
          vTaskDelay(pdMS_TO_TICKS(500));
    myDFPlayer.stop();
    vTaskDelay(pdMS_TO_TICKS(500));
        myDFPlayer.volume(15);
        vTaskDelay(pdMS_TO_TICKS(500));
        int valeur [] = {4,5,15};
        int randomTrack = random(0,3);
        myDFPlayer.play(valeur[randomTrack]);
        vTaskDelay(pdMS_TO_TICKS(500));
        OffMSG = false;
        }
    }
    //Serial.println(PelucheActive);
    if (PelucheActive){

      if (OnMSG)
      {
         vTaskDelay(pdMS_TO_TICKS(500));
    myDFPlayer.stop();
    vTaskDelay(pdMS_TO_TICKS(500));
        myDFPlayer.volume(20);
        vTaskDelay(pdMS_TO_TICKS(500));
        myDFPlayer.play(7);
        vTaskDelay(pdMS_TO_TICKS(500));
        OnMSG = false;
      }
      //Serial.print("Good");
    // long duration;
    // float distance;

    if (led > 3) {
      led = 3;
    }

    // distance = getStableDistance();
    // Serial.println(distance);
      if (millis() - LastSpot > (vitDetection * vitDiff) && etatPIR == HIGH && !mouvementDetecte && closestPlayer == 9){
        LastSpot = millis();
        noDetection = true;
        sendMQTTMessage("Killer detected");
        vTaskDelay(pdMS_TO_TICKS(300));
        myDFPlayer.volume(20);
        vTaskDelay(pdMS_TO_TICKS(300));
        int valeur [] = {17,18,19,20};
        int randomTrack = random(0,4);
         myDFPlayer.play(valeur[randomTrack]);
        vTaskDelay(pdMS_TO_TICKS(300));
      }

      if (millis() - LastSpot > (vitDetection * vitDiff) && etatPIR == HIGH && !mouvementDetecte && !noDetection && closestPlayer != 9) {
        LastSpot = millis();        
        mouvementDetecte = true;  // Bloque les nouvelles détections
        led += 1;
        CurrentTime = millis();
        End = millis();
      }


      if (led == 0) {
        digitalWrite(LED, HIGH);
        digitalWrite(LED2, HIGH);
        digitalWrite(LED3, HIGH);
        digitalWrite(LED4, HIGH);
        // if (millis() - ShortVoice > 3000) {
        //   uint8_t state = myDFPlayer.readState();
        //     if (state == 1) {
        //         Serial.println("Forçage de l'arrêt du DFPlayer...");
        //         myDFPlayer.stop();
        //         vTaskDelay(pdMS_TO_TICKS(500));
        //         // Serial.println("Réinitialisation du DFPlayer...");
        //         // myDFPlayer.reset();
        //         // vTaskDelay(pdMS_TO_TICKS(500));
        //     }
        // }
      }  if (led == 1) {
        if (!isplaying2) {
          noDetection = true;
          vTaskDelay(pdMS_TO_TICKS(300));
          myDFPlayer.volume(30);
          vTaskDelay(pdMS_TO_TICKS(300));
          int valeur [] = {2,6,8, 12, 13, 16};
          int randomTrack = random(0,6);
          myDFPlayer.play(valeur[randomTrack]);
          vTaskDelay(pdMS_TO_TICKS(300));
          isplaying2 = true;
          ShortVoice = millis();
        }

        // if (millis() - ShortVoice > 4000) {
        //   uint8_t state = myDFPlayer.readState();
        //     if (state == 1) {
        //         Serial.println("Forçage de l'arrêt du DFPlayer...");
        //         myDFPlayer.stop();
        //         vTaskDelay(pdMS_TO_TICKS(500));
        //         // Serial.println("Réinitialisation du DFPlayer...");
        //         // myDFPlayer.reset();
        //         // vTaskDelay(pdMS_TO_TICKS(500));
        //     }
        // }

        if (millis() - End >= 15000) {
          led = 0;
          noDetection = true;
          sendMQTTMessage("Player notdetected");
          vTaskDelay(pdMS_TO_TICKS(300));
          myDFPlayer.volume(30);
          vTaskDelay(pdMS_TO_TICKS(300));
          int valeur [] = {9,10,14};
          int randomTrack = random(0,3);
          myDFPlayer.play(valeur[randomTrack]);
          vTaskDelay(pdMS_TO_TICKS(300));
          ShortVoice = millis();
          isplaying2 = false;
          LastSpot = millis();
        }

        if (millis() - End < 15000) { 
          if (millis() - LedTime > 300) {
            if (digitalRead(LED) == 1) {
              digitalWrite(LED, LOW);
              digitalWrite(LED2, LOW);
              digitalWrite(LED3, LOW);
              digitalWrite(LED4, LOW);
              LedTime = millis();
            } else {
              digitalWrite(LED, HIGH);
              digitalWrite(LED2, HIGH);
              digitalWrite(LED3, HIGH);
              digitalWrite(LED4, HIGH);
              LedTime = millis();
            }
          }
        }
      } if (led >= 2) {
        if (!isplaying) {
          noDetection = true;
          vTaskDelay(pdMS_TO_TICKS(300));
          myDFPlayer.volume(30);
          vTaskDelay(pdMS_TO_TICKS(300));
          int valeur [] = {1,3};
          int randomTrack = random(0,2);
          myDFPlayer.play(valeur[randomTrack]);
          vTaskDelay(pdMS_TO_TICKS(300));
          sendMQTTMessage("Player detected");
          isplaying = true;
          isplaying2 = false;
        }

        LastSpot = millis();

        if (millis() - End >= 30000) {
          led = 0;
          sendMQTTMessage("Player notdetected");
          isplaying = false;
          LastSpot = millis();
        }

        if (millis() - End < 30000) {
          if (millis() - LedTime > 100) {
            if (digitalRead(LED) == 1) {
              digitalWrite(LED, LOW);
              digitalWrite(LED2, LOW);
              digitalWrite(LED3, LOW);
              digitalWrite(LED4, LOW);
              LedTime = millis();
            } else {
              digitalWrite(LED, HIGH);
              digitalWrite(LED2, HIGH);
              digitalWrite(LED3, HIGH);
              digitalWrite(LED4, HIGH);
              LedTime = millis();
            }
          }
        }
      }
    

    LoopTime = millis();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// float getStableDistance() {
//   int numSamples = 5;  // Nombre de mesures pour la moyenne
//   float total = 0;
//   int validReadings = 0;
  
//   for (int i = 0; i < numSamples; i++) {
//     digitalWrite(TRIG_PIN, LOW);
//     delayMicroseconds(2);
//     digitalWrite(TRIG_PIN, HIGH);
//     delayMicroseconds(10);
//     digitalWrite(TRIG_PIN, LOW);

//     long duration = pulseIn(ECHO_PIN, HIGH, 30000);  // Timeout 30ms
//     float distance = (duration * 0.034) / 2;  // Conversion en cm

//     // Filtrage des valeurs aberrantes
//     if (distance > 2 && distance < 300) {
//       total += distance;
//       validReadings++;
//     }

//     vTaskDelay(pdMS_TO_TICKS(10));  // Petite pause entre les mesures
//   }

//   if (validReadings > 0) {
//     return total / validReadings;  // Moyenne des lectures valides
//   } else {
//     return -1;  // Retourne -1 si aucune lecture valide
//   }
// }



