#include <WiFi.h>
#include <PubSubClient.h>
#include "ESP32BleAdvertise.h"
#include <string>
#include <DFRobotDFPlayerMini.h>

// ===== IR (TSOP + IRremoteESP8266) =====
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

// ---------- Wi-Fi ----------
const char* ssid          = "PelucheGang";
const char* password      = "CACHE-CACHEKILLER";
const char* mqtt_server   = "192.168.0.139";
const char* mqtt_user     = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";

// ---------- IDs / IO ----------
#define LED 13
const char* esp32_id = "Player6"; // Identifiant unique pour cet ESP32

// ---------- États jeu ----------
bool  detection      = false;
bool  invulnerable   = false;
float reperage       = 0;
float diff           = 1;
float difflvl        = 1;
int   vol            = 0;

String Player = String(2);
String Etat   = "0"; // "0" (IR non détecté)

// ---------- MQTT ----------
WiFiClient espClient;
PubSubClient client(espClient);

// ---------- TSOP / IRremote ----------
static const uint8_t  RECV_PIN = 15;                 // Entrée TSOP
static const uint32_t KILLER_CODE = 0xA90;      // À adapter au code réel de ta lampe
static const bool     ACCEPT_ANY_CODE = false;       // true pour debug: accepte tout code décodé
static const uint32_t IR_BEAM_HOLD_MS = 650;         // fenêtre où on considère le faisceau "présent"
static const uint8_t RECV_PIN_MODE = INPUT_PULLUP;      // passe à INPUT_PULLUP si ton TSOP en a besoin

IRrecv irrecv(RECV_PIN);
decode_results results;
volatile uint32_t lastValidIrMs = 0;                 // dernier décodage IR valide

// ---------- Timers ----------
const unsigned long mqttInterval = 250; // ms
unsigned long invTime = 0;
unsigned long LedTime = 0;
unsigned long lastMqttPublishTime = 0;
float LedInt = 800;

// ---------- DFPlayer ----------
#define RX_PIN 16
#define TX_PIN 17
DFRobotDFPlayerMini myDFPlayer;
bool isIRMode = false; // false : piste 1, true : piste 2
bool onPlay   = false;

// ---------- FreeRTOS ----------
TaskHandle_t networkTaskHandle;
TaskHandle_t gameLogicTaskHandle;
TaskHandle_t bleAdvertisingTaskHandle; // BLE task

// ---------- BLE ----------
SimpleBLE ble;

// ---------- Prototypes ----------
void connectToWiFi();
void reconnectMQTT();
void resetWiFi();
void callback(char* topic, byte* payload, unsigned int length);
void sendDetectionMessage(const char* message);
void networkTask(void* parameter);
void gameLogicTask(void* parameter);
void bleAdvertisingTask(void* parameter);
void WiFiEvent(WiFiEvent_t event);

// ===================================================================================
//                  ***  NOUVELLE LOGIQUE HIT_COUNT / ACK  ***
// ===================================================================================

// Compteur total de hits validés
volatile uint32_t hitCount = 0;

// Dernier compteur envoyé / accusé réception par Unity
uint32_t lastSentCount  = 0;
uint32_t lastAckedCount = 0;

// Resync conditionnel
unsigned long lastSentAt = 0;
const unsigned long RESYNC_PERIOD_MS = 2000; // renvoyer seulement si pas d’ACK

// Publish helper (non-retained)
inline void publishHitCount(uint32_t k) {
  String payload = String(esp32_id) + ":HIT_COUNT=" + String(k);
  client.publish("esp32/donnees", payload.c_str(), false);
  Serial.println("[HIT_COUNT] " + payload);
}

// ===================================================================================

static inline bool irBeamPresent() {
  return (millis() - lastValidIrMs) < IR_BEAM_HOLD_MS;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  // ---------- DFPlayer ----------
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  if (!myDFPlayer.begin(Serial2)) {
    Serial.println("Impossible de communiquer avec le DFPlayer Mini !");
    while (true) { vTaskDelay(100 / portTICK_PERIOD_MS); }
  }
  Serial.println("DFPlayer Mini initialisé.");
  myDFPlayer.volume(0);

  // ---------- TSOP / IRremote ----------
  pinMode(RECV_PIN, RECV_PIN_MODE);  // INPUT par défaut; bascule en INPUT_PULLUP si nécessaire
  irrecv.enableIRIn();               // démarrage du décodeur
  Serial.println("Récepteur IR (TSOP) prêt.");

  // (Optionnel) écouter les événements Wi-Fi
  WiFi.onEvent(WiFiEvent);

  // ---------- Tâches ----------
  xTaskCreatePinnedToCore(
      networkTask,
      "NetworkTask",
      4096,
      NULL,
      1,
      &networkTaskHandle,
      0 // coeur 0
  );

  xTaskCreatePinnedToCore(
      gameLogicTask,
      "GameLogicTask",
      6144,
      NULL,
      1,
      &gameLogicTaskHandle,
      1 // coeur 1
  );

  xTaskCreatePinnedToCore(
      bleAdvertisingTask,
      "BLEAdvertisingTask",
      4096,
      NULL,
      1,
      &bleAdvertisingTaskHandle,
      0 // coeur 0
  );
}

void loop() {
  // Rien : tout est géré par les tâches
}

// ===================================================================================
//                                   GAME LOGIC
// ===================================================================================
void gameLogicTask(void* parameter) {
  const float REP_MAX = 10.0f;               // seuil de “hit” comme avant
  const uint32_t DECAY_PERIOD_MS = 500;      // période de décroissance si plus de faisceau
  const float DECAY_STEP = 0.3f;             // pas de décroissance
  const uint32_t VOL_BASE = 3;               // offset volume
  const uint32_t VOL_GAIN = 2;               // gain sur reperage
  uint32_t lastDecayTick = millis();

  while (true) {
    // ----- DÉCODAGE IR -----
    if (irrecv.decode(&results)) {
      // Debug minimal : type + valeur
      // Serial.printf("IR type=%d value=0x%llX\n", (int)results.decode_type, results.value);

      bool valid = false;
      if (ACCEPT_ANY_CODE) {
        valid = true;
      } else {
        // On compare à KILLER_CODE
        if (results.value == KILLER_CODE) valid = true;
      }

      if (valid && !invulnerable) {
        lastValidIrMs = millis();

        // Lancement ou maintien de la piste 1 (repérage)
        if (!onPlay) {
          Serial.println("Détection IR codée - Lecture piste 1");
          myDFPlayer.volume((int)reperage * VOL_GAIN + VOL_BASE);
          vTaskDelay(150 / portTICK_PERIOD_MS);
          myDFPlayer.play(1);
          vTaskDelay(300 / portTICK_PERIOD_MS);
          onPlay = true;
        }

        // Incrément progressif calé sur mqttInterval * difflvl (comme avant)
        if (millis() - lastMqttPublishTime > (mqttInterval * difflvl)) {
          reperage += 1.0f;
          if (reperage > REP_MAX) reperage = REP_MAX;
          lastMqttPublishTime = millis();
          vol = (int)(reperage * VOL_GAIN) + VOL_BASE;
          myDFPlayer.volume(vol);
        }
      }

      irrecv.resume(); // prêt pour la prochaine trame
    }

    // ----- VALIDATION D’UN HIT -----
    if (reperage >= REP_MAX && !invulnerable) {
      Serial.println("Repérage max atteint - Lecture piste 2 (HIT)");
      myDFPlayer.volume(30);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      myDFPlayer.play(2);
      vTaskDelay(300 / portTICK_PERIOD_MS);

      Etat = "1";               // mode IR
      invTime = millis();
      reperage = 0;
      invulnerable = true;
      digitalWrite(LED, HIGH);

      // Compteur + annonce HIT_COUNT (ACK attendu côté Unity)
      hitCount++;
      lastSentCount = hitCount;
      lastSentAt = millis();
      publishHitCount(lastSentCount);
    }

    // ----- PERTE DU FAISCEAU : décroissance + coupe son si plus de présence -----
    if (!irBeamPresent() && Etat != "1") {
      uint32_t now = millis();
      if ((now - lastDecayTick) >= DECAY_PERIOD_MS) {
        if (onPlay) {
          // On coupe le son seulement quand on sort de la fenêtre
          Serial.println("Perte du faisceau IR - Atténuation/arrêt");
          myDFPlayer.volume(0);
          onPlay = false;
        }
        reperage -= DECAY_STEP;
        if (reperage < 0) reperage = 0;
        lastDecayTick = now;
      }
    }

    // ----- FENÊTRE D’INVULNÉRABILITÉ -----
    if (millis() - invTime >= 10000 && invulnerable) {
      invulnerable = false;
      reperage = 0;
      sendDetectionMessage("IR_notdetected");
      Etat = "0";
      onPlay = false;
      digitalWrite(LED, LOW);
    }

    // // ----- RESYNC CONDITIONNEL HIT_COUNT -----
    // if (millis() - lastSentAt >= RESYNC_PERIOD_MS) {
    //   if (lastAckedCount != lastSentCount && lastSentCount != 0) {
    //     publishHitCount(lastSentCount);
    //     lastSentAt = millis();
    //   } else {
    //     lastSentAt = millis();
    //   }
    // }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ===================================================================================
//                                   NETWORK
// ===================================================================================
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
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void bleAdvertisingTask(void* parameter) {
  while (true) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    String combinedData = "P:" + Player + ";E:" + Etat;
    ble.advertise(combinedData);
  }
}

// ===================================================================================
//                               WIFI / MQTT HELPERS
// ===================================================================================
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case WIFI_EVENT_STA_DISCONNECTED:
      Serial.println("Wi-Fi déconnecté. Tentative de reconnexion...");
      connectToWiFi();
      break;
    case IP_EVENT_STA_GOT_IP:
      Serial.println("Wi-Fi reconnecté !");
      Serial.print("Adresse IP : ");
      Serial.println(WiFi.localIP());
      break;
    default:
      break;
  }
}

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

      // À la reconnexion, republier l’état courant si non ACK
      if (lastSentCount != 0 && lastAckedCount != lastSentCount) {
        publishHitCount(lastSentCount);
        lastSentAt = millis();
      }
      attemptCount = 0;
    } else {
      Serial.print("Échec, rc=");
      Serial.print(client.state());
      Serial.println(". Nouvelle tentative dans 5 secondes.");
      attemptCount++;
      if (attemptCount >= 5) {
        Serial.println("Trop d'échecs MQTT, redémarrage Wi-Fi...");
        // resetWiFi();
        attemptCount = 0;
      }
      vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
  }
}

void resetWiFi() {
  WiFi.disconnect(true);
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  connectToWiFi();
}

// ===================================================================================
//                                 MQTT CALLBACK
// ===================================================================================
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];

  Serial.print("Message reçu sur ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  // -------- Difficulté joueurs --------
  if (message.startsWith(String("Player") + ":difficulte")) {
    int startIdx = message.indexOf("(") + 1;
    int endIdx = message.indexOf(")");
    String difficultyStr = message.substring(startIdx, endIdx);
    float difficultyLevel = difficultyStr.toFloat();
    difflvl = 1 - (difficultyLevel / 20);
    Serial.print("Commande de détection diff reçue : ");
    Serial.println(difflvl);
  }

  // -------- WIN / LOOSE --------
  if (message.endsWith("WIN")) {
    Serial.println("Commande WIN reçue.");
    vTaskDelay(100 / portTICK_PERIOD_MS);
    myDFPlayer.volume(30);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    myDFPlayer.play(4);
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }

  if (message.endsWith("LOOSE")) {
    Serial.println("Commande LOOSE reçue.");
    vTaskDelay(100 / portTICK_PERIOD_MS);
    myDFPlayer.volume(30);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    myDFPlayer.play(3);
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }

  // --------- *** ACK HIT_COUNT *** ---------
  // Ex. "Player6:ACK_COUNT=3"
  if (message.startsWith(String(esp32_id) + ":ACK_COUNT=")) {
    int eq = message.lastIndexOf('=');
    if (eq >= 0) {
      uint32_t k = (uint32_t) strtoul(message.c_str() + eq + 1, nullptr, 10);
      if (k == lastSentCount) {
        lastAckedCount = k; // on arrête les renvois pour cette valeur
        Serial.printf("[ACK] ok for count=%u\n", (unsigned)k);
      } else {
        Serial.printf("[ACK] ignoré (attendu=%u, reçu=%u)\n", (unsigned)lastSentCount, (unsigned)k);
      }
    }
  }

  // --------- (Optionnel) RESET ---------
  if (message.startsWith("RESET")) {
    Serial.println("[RESET] Reçu. Remise à zéro des compteurs.");
    hitCount = 0;
    lastSentCount = 0;
    lastAckedCount = 0;
    lastSentAt = millis();
    publishHitCount(0); // annonce l’état 0 si tu veux
  }
}

// ===================================================================================
//                              ENVOI MESSAGES GENERIQUES
// ===================================================================================
void sendDetectionMessage(const char* message) {
  String msg = String(esp32_id) + ":" + message;
  client.publish("esp32/donnees", msg.c_str(), false);
  Serial.println("Message envoyé : " + msg);
}
