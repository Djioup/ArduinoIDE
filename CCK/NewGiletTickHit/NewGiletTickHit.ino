/***********************
 *  Cache-Cache Killer - Gilet Joueur (IR TSOP -> rafales de codes)
 *  Logiciel: BURST_N décodages valides => +1 reperage => "tick" court
 ***********************/
#include <WiFi.h>
#include <PubSubClient.h>
#include "ESP32BleAdvertise.h"
#include <string>
#include <DFRobotDFPlayerMini.h>

// ===== IR (TSOP + Arduino-IRremote) =====
#include <IRremote.hpp>

// ---------- Wi-Fi ----------
const char* ssid          = "PelucheGang";
const char* password      = "CACHE-CACHEKILLER";
const char* mqtt_server   = "192.168.0.139";
const char* mqtt_user     = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";

// ---------- IDs / IO ----------
const int MOTOR_PIN = 13; // GPIO13 = D13 sur beaucoup de cartes ESP32
const char* esp32_id = "Player2"; // Identifiant unique pour cet ESP32
String BLENAME = "Player:2";
String Player = String(2);
String Etat   = "0";           // "0" normal, "1" touché

// ---------- États jeu ----------
bool  invulnerable   = false;
float reperage       = 0;       // jauge 0..REP_MAX
float difflvl        = 1.0f;    // influencé par Unity
int   vol            = 0;


// ---------- MQTT ----------
WiFiClient espClient;
PubSubClient client(espClient);

// ---------- TSOP / IRremote ----------

static const uint8_t RECV_PIN = 15;   // TSOP sur GPIO15
// static const uint32_t KILLER_CODE = 0xA90;       // À adapter exactement au code lampe
static const bool     ACCEPT_ANY_CODE = false;   // true pour debug (accepte tout)
// static const uint8_t  RECV_PIN_MODE = INPUT;     // passer à INPUT_PULLUP si besoin

// decode_results results;

// ---------- Rafales / Incrément ----------
static const int  BURST_N = 11;               // nb de codes pour 1 incrément
static const uint32_t  BURST_GAP_RESET_MS = 3000;  // reset de la rafale si trou > 200ms
int  burstCount    = 0;
uint32_t  lastBurstAt   = 0;                      // dernier décodage pris en compte

// Anti-spam du tick
static const uint32_t MIN_TICK_SPACING_MS = 500; // min 120ms entre 2 ticks audio
uint32_t lastTickSoundAt = 0;

// ---------- Fenêtre “présence IR” utile au decay global ----------
static const uint32_t IR_BEAM_HOLD_MS = 3000;     // pour le decay passif
volatile uint32_t lastValidIrMs = 0;

// ---------- Timers ----------
const unsigned long mqttInterval = 250; // cadence de montée abstraite (garde-le si tu veux la difficulté côté Unity)
unsigned long invTime = 0;

// ---------- DFPlayer ----------
#define RX_PIN 16
#define TX_PIN 17
DFRobotDFPlayerMini myDFPlayer;

// Pistes (adapte si besoin)
static const uint8_t TRACK_TICK = 5;  // beep court sur incrément
static const uint8_t TRACK_HIT  = 2;  // hit validé
static const uint8_t TRACK_LOSE = 3;  // loose
static const uint8_t TRACK_WIN  = 4;  // win

// ---------- Gameplay ----------
const float REP_MAX          = 4.0f; // seuil de hit
const float DECAY_STEP       = 0.3f;  // décroissance
const uint32_t DECAY_PERIOD_MS = 4000; // période de décroissance
const uint32_t VOL_BASE      = 3;     // volume offset
const uint32_t VOL_GAIN      = 10;     // volume slope
uint32_t now = millis();
bool vibration = false;
bool Lastvibration = false;

// ---------- FreeRTOS ----------
TaskHandle_t networkTaskHandle;
TaskHandle_t gameLogicTaskHandle;
TaskHandle_t bleAdvertisingTaskHandle;

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
// ---------- HIT/TICK COUNTERS & ACKS ----------
volatile uint32_t hitCount  = 0;
volatile uint32_t tickCount = 0;
// ✅ Séparer nettement les états HIT et TICK
uint32_t lastSentHitCount     = 0;   // dernier hit envoyé
uint32_t lastAckedHitCount    = 0;   // dernier hit ACKé
unsigned long lastHitSentAt   = 0;   // timestamp dernier envoi HIT

uint32_t lastSentTickCount    = 0;   // dernier tick envoyé
uint32_t lastAckedTickCount   = 0;   // dernier tick ACKé
unsigned long lastTickSentAt  = 0;   // timestamp dernier envoi TICK
const unsigned long RESYNC_PERIOD_MS = 3000;

inline void publishHitCount(uint32_t k) {
  String payload = String(esp32_id) + ":HIT_COUNT=" + String(k);
  client.publish("esp32/donnees", payload.c_str(), false);
  Serial.println("[HIT_COUNT] " + payload);
}

inline void publishTickCount(uint32_t k) {
  String payload = String(esp32_id) + ":TICK_COUNT=" + String(k);
  client.publish("esp32/donnees", payload.c_str(), false);
  Serial.println("[TICK_COUNT] " + payload);
}

static inline bool irBeamPresent() {
  return (millis() - lastValidIrMs) < IR_BEAM_HOLD_MS;
}

// ===================================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(MOTOR_PIN, OUTPUT);

  // ---------- DFPlayer ----------
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  vTaskDelay(1500 / portTICK_PERIOD_MS);
  if (!myDFPlayer.begin(Serial2)) {
    Serial.println("DFPlayer Mini non détecté !");
    while (true) { vTaskDelay(100 / portTICK_PERIOD_MS); }
  }
  Serial.println("DFPlayer Mini OK.");
  myDFPlayer.volume(30);
  

  // ---------- TSOP / IRremote ----------
  pinMode(RECV_PIN, INPUT);           // PAS de pullup sur un TSOP 4838
  IrReceiver.begin(RECV_PIN);
  Serial.println("TSOP prêt (Arduino-IRremote).");

  Serial.println(esp32_id);

  ble.begin(BLENAME);

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
    // Serial.println (burstCount);
     now = millis();
     // --- Logique rafale ---
        if (now - lastBurstAt > BURST_GAP_RESET_MS) {
          burstCount = 0;  // trou trop long: on repart de zéro
          vibration = false;
        }
        
    if (now - lastTickSoundAt > MIN_TICK_SPACING_MS && Lastvibration && !invulnerable)
          {
            vibration = false;
            Lastvibration = false;
            if (burstCount <= 0) analogWrite(MOTOR_PIN, 0); // PWM 0-255
          }
 // ----- DÉCODE IR -----
if (IrReceiver.decode()) {
  bool valid = false;
  auto &d = IrReceiver.decodedIRData;

  if (ACCEPT_ANY_CODE) {
    valid = true;
  } else {
    if (d.protocol == NEC && d.address == 0x0000 && d.command == 0xA9) {
      valid = true;
    }
    // Compter les repeats NEC si tu veux :
    // if (d.flags & IRDATA_FLAGS_IS_REPEAT) valid = true;
  }

  if (valid && !invulnerable) {
    const uint32_t nowMs = millis();
    lastValidIrMs = nowMs;
    lastBurstAt   = nowMs;

    burstCount++;
    if (!vibration) {
      analogWrite(MOTOR_PIN, 255);
      vibration = true;
    }

    const int burstThreshold = max(1, (int)ceilf((float)BURST_N - difflvl));
    if (burstCount >= burstThreshold) {
      burstCount = 0;
      Lastvibration = true;

      reperage += 1.0f;
      if (reperage > REP_MAX) reperage = REP_MAX;

      // vol = (int)(reperage * VOL_GAIN) + VOL_BASE;

      if ((nowMs - lastTickSoundAt) >= MIN_TICK_SPACING_MS && reperage < REP_MAX) {
        // myDFPlayer.volume(vol);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        myDFPlayer.play(TRACK_TICK);
        vTaskDelay(400 / portTICK_PERIOD_MS);
        lastTickSoundAt = nowMs;
            // ✅ TICK_COUNT++
            tickCount++;
            lastSentTickCount = tickCount;
            lastTickSentAt = millis();
            publishTickCount(lastSentTickCount);    // ✅ corrige l’erreur : on publie bien un TICK, pas un HIT
      }
    }
  }

  IrReceiver.resume();
}



    // ----- VALIDATION D’UN HIT -----
    if (reperage >= REP_MAX && !invulnerable) {
      Serial.println("HIT validé → piste HIT");
      // myDFPlayer.volume(30);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      myDFPlayer.play(2);
      vTaskDelay(500 / portTICK_PERIOD_MS);

      Etat = "1";
      invTime = millis();
      analogWrite(MOTOR_PIN, 255); // PWM 0-255
      reperage = 0;
      invulnerable = true;
      // digitalWrite(LED, HIGH);

      // Compteur + annonce (HIT_COUNT)
      hitCount++;
      lastSentHitCount = hitCount;
      lastHitSentAt = millis();
      publishHitCount(lastSentHitCount);
    }

    // ----- DÉCROISSANCE SI PLUS DE PRÉSENCE -----
    if (!irBeamPresent() && Etat != "1") {
      uint32_t now = millis();
      if ((now - lastDecayTick) >= DECAY_PERIOD_MS) {
        reperage -= DECAY_STEP;
        if (reperage < 0) reperage = 0;
        lastDecayTick = now;
        analogWrite(MOTOR_PIN, 0); // PWM 0-255
      }
    }

    // ----- FIN D’INVULNÉRABILITÉ (10 s) -----
    if (millis() - invTime >= 10000 && invulnerable) {
      invulnerable = false;
      reperage = 0;
      sendDetectionMessage("IR_notdetected");
      Etat = "0";
      // digitalWrite(LED, LOW);
      // myDFPlayer.volume(0);
    }

    //----- RESYNC HIT_COUNT (option réactivée si besoin) -----
if (millis() - lastHitSentAt >= RESYNC_PERIOD_MS) {
      if (lastAckedHitCount != lastSentHitCount && lastSentHitCount != 0) {
        publishHitCount(lastSentHitCount);
      }
      lastHitSentAt = millis();
    }

    if (millis() - lastTickSentAt >= RESYNC_PERIOD_MS) {
      if (lastAckedTickCount != lastSentTickCount && lastSentTickCount != 0) {
        publishTickCount(lastSentTickCount);
      }
      lastTickSentAt = millis();
    }
      

    vTaskDelay(8 / portTICK_PERIOD_MS);
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
    default:
      break;
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
  Serial.print("IP : ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  String willTopic = String("esp32/") + String(esp32_id) + "/status";
  String willMessage = String(esp32_id) + ":offline";

  int attemptCount = 0;
  while (!client.connected()) {
    Serial.println("Connexion au broker MQTT...");
    if (client.connect(esp32_id, mqtt_user, mqtt_password, willTopic.c_str(), 1, true, willMessage.c_str())) {
      Serial.println("Connecté MQTT");
      String onlineMessage = String(esp32_id) + ":online";
      client.publish(willTopic.c_str(), onlineMessage.c_str(), true);
      client.subscribe("unity/commandes");
      sendDetectionMessage("on");
      sendDetectionMessage("Request");

     // ✅ Au reconnect, republier les derniers états non ACKés (HIT & TICK)
      if (lastSentHitCount != 0 && lastAckedHitCount != lastSentHitCount) {
        publishHitCount(lastSentHitCount);
        lastHitSentAt = millis();
      }
      if (lastSentTickCount != 0 && lastAckedTickCount != lastSentTickCount) {
        publishTickCount(lastSentTickCount);
        lastTickSentAt = millis();
      }
      attemptCount = 0;
    } else {
      Serial.print("Échec MQTT, rc=");
      Serial.print(client.state());
      Serial.println(". Retry dans 5s.");
      attemptCount++;
      if (attemptCount >= 5) {
        Serial.println("Trop d'échecs MQTT → reset Wi-Fi...");
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
    // difflvl influence peu ici, on la garde pour de futurs ajustements
    difflvl = difficultyLevel;
    Serial.print("Difflvl = "); Serial.println(difflvl, 3);
  }

  if (message.endsWith("WIN")) {
    Serial.println("WIN");
    vTaskDelay(80 / portTICK_PERIOD_MS);
    myDFPlayer.volume(30);
    vTaskDelay(120 / portTICK_PERIOD_MS);
    myDFPlayer.play(TRACK_WIN);
  }

  if (message.endsWith("LOOSE")) {
    Serial.println("LOOSE");
    vTaskDelay(80 / portTICK_PERIOD_MS);
    myDFPlayer.volume(30);
    vTaskDelay(120 / portTICK_PERIOD_MS);
    myDFPlayer.play(TRACK_LOSE);
  }

// ✅ ACK HIT_COUNT : "Player0:ACK_COUNT=N"
  if (message.startsWith(String(esp32_id) + ":ACK_COUNT=")) {
    int eq = message.lastIndexOf('=');
    if (eq >= 0) {
      uint32_t k = (uint32_t) strtoul(message.c_str() + eq + 1, nullptr, 10);
      if (k == lastSentHitCount) {
        lastAckedHitCount = k;
        Serial.printf("[ACK HIT] ok for count=%u\n", (unsigned)k);
      } else {
        Serial.printf("[ACK HIT] ignoré (attendu=%u, reçu=%u)\n",
                      (unsigned)lastSentHitCount, (unsigned)k);
      }
    }
  }

  // ✅ ACK TICK_COUNT : "Player0:ACK_TICK=N"
  if (message.startsWith(String(esp32_id) + ":ACK_TICK=")) {
    int eq = message.lastIndexOf('=');
    if (eq >= 0) {
      uint32_t k = (uint32_t) strtoul(message.c_str() + eq + 1, nullptr, 10);
      if (k == lastSentTickCount) {
        lastAckedTickCount = k;
        Serial.printf("[ACK TICK] ok for count=%u\n", (unsigned)k);
      } else {
        Serial.printf("[ACK TICK] ignoré (attendu=%u, reçu=%u)\n",
                      (unsigned)lastSentTickCount, (unsigned)k);
      }
    }
  }

  if (message.endsWith("RESET")) {
    Serial.println("[RESET] remise à zéro");

    // HIT
    hitCount = 0;
    lastSentHitCount   = 0;
    lastAckedHitCount  = 0;
    lastHitSentAt      = millis();
    publishHitCount(0);

    // ✅ TICK
    tickCount = 0;
    lastSentTickCount  = 0;
    lastAckedTickCount = 0;
    lastTickSentAt     = millis();
    publishTickCount(0);

    myDFPlayer.volume(30);
    reperage = 0;
    invulnerable = false;
    Etat = "0";
    burstCount = 0;
  }
}

// ===================================================================================
//                              ENVOI MESSAGES GENERIQUES
// ===================================================================================
void sendDetectionMessage(const char* message) {
  String msg = String(esp32_id) + ":" + message;
  client.publish("esp32/donnees", msg.c_str(), false);
  Serial.println("Send : " + msg);
}
