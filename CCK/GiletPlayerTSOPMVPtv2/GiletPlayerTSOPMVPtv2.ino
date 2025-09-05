/***********************
 *  Cache-Cache Killer - Gilet Joueur (IR TSOP + Vibration + Son + MQTT/ACK)
 *  ESP32 WROOM
 ***********************/
#include <WiFi.h>
#include <PubSubClient.h>
#include "ESP32BleAdvertise.h"
#include <string>
#include <DFRobotDFPlayerMini.h>

// ===== IR (TSOP + IRremoteESP8266) =====
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

// ---------- Wi-Fi / MQTT ----------
const char* ssid          = "PelucheGang";
const char* password      = "CACHE-CACHEKILLER";
const char* mqtt_server   = "192.168.0.139";
const char* mqtt_user     = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";

// ---------- IDs / IO ----------
#define LED 13
const char* esp32_id = "Player6"; // Identifiant unique de CE gilet

// ---------- États jeu ----------
bool  invulnerable   = false;
float reperage       = 0;     // jauge de repérage (0..REP_MAX)
int   vol            = 0;     // volume DFPlayer
String Player = String(2);
String Etat   = "0";         // "0" (pas en mode touché), "1" (touché)

// ---------- MQTT ----------
WiFiClient espClient;
PubSubClient client(espClient);

// ---------- TSOP / IRremote ----------
static const uint8_t  RECV_PIN = 15;                 // Entrée TSOP
static const uint32_t KILLER_CODE = 0x00000A90;      // À CALER sur ta lampe
static const bool     ACCEPT_ANY_CODE = false;       // true => accepte tout code (debug)
static const uint32_t IR_BEAM_HOLD_MS = 250;         // fenêtre où on considère le faisceau "présent"
static const uint8_t RECV_PIN_MODE = INPUT;          // ou INPUT_PULLUP selon ton TSOP
  

IRrecv irrecv(RECV_PIN);
decode_results results;
volatile uint32_t lastValidIrMs = 0;                 // dernier décodage IR valide

// ---------- Timers / cadence ----------
const unsigned long mqttInterval = 250; // ms entre incréments de repérage
unsigned long invTime = 0;
unsigned long lastMqttPublishTime = 0;

// ---------- DFPlayer ----------
#define RX_PIN 16
#define TX_PIN 17
DFRobotDFPlayerMini myDFPlayer;
bool onPlay   = false;       // piste 1 en cours ?

// ---------- VIBRATION (ERM via MOSFET + PWM LEDC) ----------
#define VIB_PIN   25      // GPIO PWM vers MOSFET (via ~100Ω)
#define VIB_CH    0       // Canal LEDC (0..7)
#define VIB_FREQ  200     // Hz (150–250 Hz ok pour ERM)
#define VIB_RES   8       // bits -> duty 0..255
float vibLevel = 0.0f;        // 0..255
const float VIB_STEP  = 14.0f; // montée rapide
const float VIB_DECAY = 18.0f; // décroissance rapide
const uint8_t VIB_MAX = 255;

// ---------- Paramètres gameplay ----------
const float REP_MAX   = 10.0f;        // seuil pour valider un HIT
float difflvl         = 1.0f;         // influence sur vitesse de montée (venant de Unity)
const float DECAY_STEP = 0.3f;        // décroissance repérage si perte faisceau
const uint32_t DECAY_PERIOD_MS = 500; // période de décroissance
const uint32_t VOL_BASE = 3;          // volume offset
const uint32_t VOL_GAIN = 2;          // volume slope
const float SND_START = 2.0f;         // son ne démarre qu'au-delà de ce seuil

// ---------- FreeRTOS ----------
TaskHandle_t networkTaskHandle;
TaskHandle_t gameLogicTaskHandle;
TaskHandle_t bleAdvertisingTaskHandle;

// ---------- BLE ----------
SimpleBLE ble;

// ---------- Protos ----------
void connectToWiFi();
void reconnectMQTT();
void resetWiFi();
void callback(char* topic, byte* payload, unsigned int length);
void sendDetectionMessage(const char* message);
void networkTask(void* parameter);
void gameLogicTask(void* parameter);
void bleAdvertisingTask(void* parameter);
void WiFiEvent(WiFiEvent_t event);

// ---------- HIT_COUNT / ACK ----------
volatile uint32_t hitCount = 0;
uint32_t lastSentCount  = 0;
uint32_t lastAckedCount = 0;
unsigned long lastSentAt = 0;
const unsigned long RESYNC_PERIOD_MS = 2000;

inline void publishHitCount(uint32_t k) {
  String payload = String(esp32_id) + ":HIT_COUNT=" + String(k);
  client.publish("esp32/donnees", payload.c_str(), false);
  Serial.println("[HIT_COUNT] " + payload);
}

// ---------- Helpers ----------
static inline bool irBeamPresent() {
  return (millis() - lastValidIrMs) < IR_BEAM_HOLD_MS;
}
inline void vibWrite(uint8_t duty) { ledcWrite(VIB_CH, duty); }
inline void vibSet(float level) {
  if (level < 0) level = 0;
  if (level > VIB_MAX) level = VIB_MAX;
  vibWrite((uint8_t)level);
}

// ===================================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  // ---------- DFPlayer ----------
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  if (!myDFPlayer.begin(Serial2)) {
    Serial.println("DFPlayer Mini non détecté !");
    while (true) { vTaskDelay(100 / portTICK_PERIOD_MS); }
  }
  Serial.println("DFPlayer Mini OK.");
  myDFPlayer.volume(0);

  // ---------- Vibration PWM ----------
  ledcSetup(VIB_CH, VIB_FREQ, VIB_RES);
  ledcAttachPin(VIB_PIN, VIB_CH);
  vibSet(0);

  // ---------- TSOP / IRremote ----------
  pinMode(RECV_PIN, RECV_PIN_MODE);
  irrecv.enableIRIn();
  Serial.println("TSOP prêt.");

  // ---------- Wi-Fi events ----------
  WiFi.onEvent(WiFiEvent);

  // ---------- Tâches ----------
  xTaskCreatePinnedToCore(networkTask, "NetworkTask", 4096, NULL, 1, &networkTaskHandle, 0);
  xTaskCreatePinnedToCore(gameLogicTask, "GameLogicTask", 6144, NULL, 1, &gameLogicTaskHandle, 1);
  xTaskCreatePinnedToCore(bleAdvertisingTask, "BLEAdvertisingTask", 4096, NULL, 1, &bleAdvertisingTaskHandle, 0);
}

void loop() { /* tout est en tasks */ }

// ===================================================================================
//                                   GAME LOGIC
// ===================================================================================
void gameLogicTask(void* parameter) {
  uint32_t lastDecayTick = millis();

  while (true) {
    // ----- DÉCODE IR -----
    if (irrecv.decode(&results)) {
        
    Serial.print("Code reçu : 0x");
    Serial.println(results.value, HEX);
      bool valid = false;
      if (ACCEPT_ANY_CODE) {
        valid = true;
      } else {
        if (results.value == KILLER_CODE) valid = true;
      }

      if (valid && !invulnerable) {
        lastValidIrMs = millis();

        // --- vibration : démarre tout de suite + monte vite ---
        vibLevel += VIB_STEP;
        if (vibLevel > VIB_MAX) vibLevel = VIB_MAX;
        vibSet(vibLevel);

        // --- montée de repérage cadencée ---
        if (millis() - lastMqttPublishTime > (mqttInterval * difflvl)) {
          reperage += 1.0f;
          if (reperage > REP_MAX) reperage = REP_MAX;
          lastMqttPublishTime = millis();

          // démarre le son piste 1 seulement au-delà d'un petit seuil
          if (!onPlay && reperage >= SND_START) {
            Serial.println("Détection IR codée - Lecture piste 1 (retardée)");
            myDFPlayer.volume((int)reperage * VOL_GAIN + VOL_BASE);
            vTaskDelay(150 / portTICK_PERIOD_MS);
            myDFPlayer.play(1);
            vTaskDelay(300 / portTICK_PERIOD_MS);
            onPlay = true;
          }

          vol = (int)(reperage * VOL_GAIN) + VOL_BASE;
          myDFPlayer.volume(vol);
        }
      }

      irrecv.resume(); // prêt pour prochaine trame
    }

    // ----- VALIDATION D’UN HIT -----
    if (reperage >= REP_MAX && !invulnerable) {
      Serial.println("HIT ! (repérage max) → piste 2");
      myDFPlayer.volume(30);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      myDFPlayer.play(2);
      vTaskDelay(300 / portTICK_PERIOD_MS);

      Etat = "1";
      invTime = millis();
      reperage = 0;
      invulnerable = true;
      digitalWrite(LED, HIGH);

      // Pic haptique puis résiduel
      vibLevel = VIB_MAX; vibSet(vibLevel);
      vTaskDelay(120 / portTICK_PERIOD_MS);
      vibLevel = 80.0f;  vibSet(vibLevel);

      // Compteur + annonce
      hitCount++;
      lastSentCount = hitCount;
      lastSentAt = millis();
      publishHitCount(lastSentCount);
    }

    // ----- PERTE DU FAISCEAU : décroissance -----
    if (!irBeamPresent() && Etat != "1") {
      uint32_t now = millis();
      if ((now - lastDecayTick) >= DECAY_PERIOD_MS) {
        // coupe le son si on n'était pas vraiment “tenu”
        if (onPlay && reperage < SND_START) {
          myDFPlayer.volume(0);
          onPlay = false;
        }
        // vibration : décroissance rapide
        vibLevel -= VIB_DECAY;
        if (vibLevel < 0) vibLevel = 0;
        vibSet(vibLevel);

        // repérage : décroissance douce
        reperage -= DECAY_STEP;
        if (reperage < 0) reperage = 0;

        lastDecayTick = now;
      }
    }

    // ----- FIN D’INVULNÉRABILITÉ (10 s) -----
    if (millis() - invTime >= 10000 && invulnerable) {
      invulnerable = false;
      reperage = 0;
      sendDetectionMessage("IR_notdetected");
      Etat = "0";
      onPlay = false;
      digitalWrite(LED, LOW);
      vibLevel = 0; vibSet(0);
      myDFPlayer.volume(0);
    }

    // ----- RESYNC HIT_COUNT -----
    if (millis() - lastSentAt >= RESYNC_PERIOD_MS) {
      if (lastAckedCount != lastSentCount && lastSentCount != 0) {
        publishHitCount(lastSentCount);
        lastSentAt = millis();
      } else {
        lastSentAt = millis();
      }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ===================================================================================
//                                   NETWORK
// ===================================================================================
void networkTask(void* parameter) {
  vTaskDelay(3000 / portTICK_PERIOD_MS);
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
      Serial.println("Wi-Fi déconnecté. Reconnexion...");
      connectToWiFi();
      break;
    case IP_EVENT_STA_GOT_IP:
      Serial.println("Wi-Fi OK !");
      Serial.print("IP : ");
      Serial.println(WiFi.localIP());
      break;
    default: break;
  }
}

void connectToWiFi() {
  Serial.println("Connexion Wi-Fi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    Serial.print(".");
  }
  Serial.println("\nConnecté Wi-Fi");
  Serial.print("IP : "); Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  String willTopic = String("esp32/") + String(esp32_id) + "/status";
  String willMessage = String(esp32_id) + ":offline";

  int attemptCount = 0;
  while (!client.connected()) {
    Serial.println("Connexion MQTT...");
    if (client.connect(esp32_id, mqtt_user, mqtt_password, willTopic.c_str(), 1, true, willMessage.c_str())) {
      Serial.println("MQTT connecté");
      String onlineMessage = String(esp32_id) + ":online";
      client.publish(willTopic.c_str(), onlineMessage.c_str(), true);
      client.subscribe("unity/commandes");
      sendDetectionMessage("on");
      sendDetectionMessage("Request");

      if (lastSentCount != 0 && lastAckedCount != lastSentCount) {
        publishHitCount(lastSentCount);
        lastSentAt = millis();
      }
      attemptCount = 0;
    } else {
      Serial.print("MQTT échec, rc=");
      Serial.print(client.state());
      Serial.println(" → retry 5s");
      attemptCount++;
      if (attemptCount >= 5) {
        Serial.println("Trop d'échecs MQTT → reset Wi-Fi");
        resetWiFi();
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

  Serial.print("MQTT ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  // Difficulté joueurs : "Player:difficulte(x)"
  if (message.startsWith(String("Player") + ":difficulte")) {
    int startIdx = message.indexOf("(") + 1;
    int endIdx = message.indexOf(")");
    String difficultyStr = message.substring(startIdx, endIdx);
    float difficultyLevel = difficultyStr.toFloat();
    difflvl = 1 - (difficultyLevel / 20.0f);
    if (difflvl < 0.2f) difflvl = 0.2f; // borne basse de sécurité
    Serial.print("Difflvl = "); Serial.println(difflvl, 3);
  }

  // WIN / LOOSE
  if (message.endsWith("WIN")) {
    Serial.println("WIN");
    vTaskDelay(100 / portTICK_PERIOD_MS);
    myDFPlayer.volume(30);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    myDFPlayer.play(4);
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
  if (message.endsWith("LOOSE")) {
    Serial.println("LOOSE");
    vTaskDelay(100 / portTICK_PERIOD_MS);
    myDFPlayer.volume(30);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    myDFPlayer.play(3);
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }

  // ACK HIT_COUNT : "Player6:ACK_COUNT=N"
  if (message.startsWith(String(esp32_id) + ":ACK_COUNT=")) {
    int eq = message.lastIndexOf('=');
    if (eq >= 0) {
      uint32_t k = (uint32_t) strtoul(message.c_str() + eq + 1, nullptr, 10);
      if (k == lastSentCount) {
        lastAckedCount = k;
        Serial.printf("[ACK] ok for count=%u\n", (unsigned)k);
      } else {
        Serial.printf("[ACK] ignoré (attendu=%u, reçu=%u)\n",
                      (unsigned)lastSentCount, (unsigned)k);
      }
    }
  }

  // RESET
  if (message.startsWith("RESET")) {
    Serial.println("[RESET] remise à zéro");
    hitCount = 0;
    lastSentCount = 0;
    lastAckedCount = 0;
    lastSentAt = millis();
    publishHitCount(0);
    vibLevel = 0; vibSet(0);
    myDFPlayer.volume(0);
    onPlay = false;
    reperage = 0;
    invulnerable = false;
    Etat = "0";
    digitalWrite(LED, LOW);
  }
}

// ===================================================================================
//                              ENVOI GENERIQUE
// ===================================================================================
void sendDetectionMessage(const char* message) {
  String msg = String(esp32_id) + ":" + message;
  client.publish("esp32/donnees", msg.c_str(), false);
  Serial.println("Send : " + msg);
}

