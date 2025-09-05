/***********************
 *  Cache-Cache Killer - Gilet Joueur (IR TSOP -> rafales de codes)
 *  Logiciel: BURST_N décodages valides => +1 reperage => "tick" court
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

// ---------- Wi-Fi ----------
const char* ssid          = "PelucheGang";
const char* password      = "CACHE-CACHEKILLER";
const char* mqtt_server   = "192.168.0.139";
const char* mqtt_user     = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";

// ---------- IDs / IO ----------
const int MOTOR_PIN = 13; // GPIO13 = D13 sur beaucoup de cartes ESP32
const char* esp32_id = "Player7"; // Identifiant unique pour cet ESP32

// ---------- États jeu ----------
bool  invulnerable   = false;
float reperage       = 0;       // jauge 0..REP_MAX
float difflvl        = 1.0f;    // influencé par Unity
int   vol            = 0;
String Player = String(2);
String Etat   = "0";           // "0" normal, "1" touché

// ---------- MQTT ----------
WiFiClient espClient;
PubSubClient client(espClient);

// ---------- TSOP / IRremote ----------
static const uint8_t  RECV_PIN = 15;             // Entrée TSOP
static const uint32_t KILLER_CODE = 0xA90;       // À adapter exactement au code lampe
static const bool     ACCEPT_ANY_CODE = false;   // true pour debug (accepte tout)
static const uint8_t  RECV_PIN_MODE = INPUT;     // passer à INPUT_PULLUP si besoin

IRrecv irrecv(RECV_PIN);
decode_results results;

// ---------- Rafales / Incrément ----------
static const uint8_t  BURST_N = 11;               // nb de codes pour 1 incrément
static const uint32_t BURST_GAP_RESET_MS = 3000;  // reset de la rafale si trou > 200ms
uint8_t  burstCount    = 0;
uint32_t lastBurstAt   = 0;                      // dernier décodage pris en compte

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
bool vibration;
bool Lastvibration;

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
  myDFPlayer.volume(0);

  // ---------- TSOP / IRremote ----------
  pinMode(RECV_PIN, RECV_PIN_MODE);
  irrecv.enableIRIn();
  Serial.println("TSOP prêt (logique rafales).");

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
    Serial.println (burstCount);
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
    if (irrecv.decode(&results)) {
      bool valid = false;

      // Option debug
      if (ACCEPT_ANY_CODE) {
        valid = true;
      } else {
        // On considère valide si c’est le code attendu
        if (results.value == KILLER_CODE) 
        {
          // burstCount++;
          valid = true;
        }

        // Si ta lampe est NEC et envoie des "repeats", on peut compter le repeat aussi :
        // if (results.repeat) valid = true;  // <- décommente si utile dans tes tests
      }

      if (valid && !invulnerable) {
        lastValidIrMs = now;
        lastBurstAt = now;

        burstCount++;
        if (burstCount > 0 && !vibration)
        {
          analogWrite(MOTOR_PIN, 255); // PWM 0-255
          vibration = true;
        }
        // Quand on atteint BURST_N, on incrémente la jauge
        if (burstCount >= (BURST_N - difflvl)) {
          burstCount = 0;
          Lastvibration = true;
          // Incrément de repérage (on garde la cadence “difficulte” si tu veux lisser)
          // Ici on peut reprendre le pas "1", le rythme général étant fixé par la rafale
          reperage += 1.0f;
        
          if (reperage > REP_MAX) reperage = REP_MAX;

          // Volume lié à la jauge (si utile)
          vol = (int)(reperage * VOL_GAIN) + VOL_BASE;
          myDFPlayer.volume(vol);

          // Tick son court anti-spam
          if (now - lastTickSoundAt >= MIN_TICK_SPACING_MS && reperage < REP_MAX) {
            myDFPlayer.play(TRACK_TICK);
            // analogWrite(MOTOR_PIN, 255); // PWM 0-255
            lastTickSoundAt = now;
          }

          // Debug
          // Serial.printf("REP=%.1f (tick)\n", reperage);
        }
      }

      irrecv.resume(); // prêt pour prochaine trame
    }

    // ----- VALIDATION D’UN HIT -----
    if (reperage >= REP_MAX && !invulnerable) {
      Serial.println("HIT validé → piste HIT");
      myDFPlayer.volume(30);
      vTaskDelay(80 / portTICK_PERIOD_MS);
      myDFPlayer.play(2);

      Etat = "1";
      invTime = millis();
      analogWrite(MOTOR_PIN, 255); // PWM 0-255
      reperage = 0;
      invulnerable = true;
      // digitalWrite(LED, HIGH);

      // Compteur + annonce
      hitCount++;
      lastSentCount = hitCount;
      lastSentAt = millis();
      publishHitCount(lastSentCount);
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
      myDFPlayer.volume(0);
    }

    //----- RESYNC HIT_COUNT (option réactivée si besoin) -----
    if (millis() - lastSentAt >= RESYNC_PERIOD_MS) {
      if (lastAckedCount != lastSentCount && lastSentCount != 0) {
        publishHitCount(lastSentCount);
        lastSentAt = millis();
      } else {
        lastSentAt = millis();
      }
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

      if (lastSentCount != 0 && lastAckedCount != lastSentCount) {
        publishHitCount(lastSentCount);
        lastSentAt = millis();
      }
      attemptCount = 0;
    } else {
      Serial.print("Échec MQTT, rc=");
      Serial.print(client.state());
      Serial.println(". Retry dans 5s.");
      attemptCount++;
      if (attemptCount >= 5) {
        Serial.println("Trop d'échecs MQTT → reset Wi-Fi...");
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

  if (message.endsWith("RESET")) {
    Serial.println("[RESET] remise à zéro");
    hitCount = 0;
    Serial.println(hitCount);
    lastSentCount = 0;
    lastAckedCount = 0;
    lastSentAt = millis();
    publishHitCount(0);
    myDFPlayer.volume(0);
    reperage = 0;
    invulnerable = false;
    Etat = "0";
    // digitalWrite(LED, LOW);
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
