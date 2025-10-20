/***********************
 *  Cache-Cache Killer - Gilet Joueur (IR TSOP -> rafales de codes)
 *  Logiciel: BURST_N décodages valides => +1 reperage => "tick" court
 ***********************/
#include <WiFi.h>
#include <PubSubClient.h>
#include "ESP32BleAdvertise.h"
#include <string>
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_NeoPixel.h>

// ===== IR (TSOP + Arduino-IRremote) =====
#include <IRremote.hpp>

// ---------- Wi-Fi ----------
const char* ssid = "PelucheGang";
const char* password = "CACHE-CACHEKILLER";
const char* mqtt_server = "192.168.0.139";
const char* mqtt_user = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";


#define RUBLED 14
#define NUM_LEDS 3
Adafruit_NeoPixel RubLed(NUM_LEDS, RUBLED, NEO_GRB + NEO_KHZ800);

// ---------- IDs / IO ----------
const int MOTOR_PIN = 13;          // GPIO13 = D13 sur beaucoup de cartes ESP32
const char* esp32_id = "Player2";  // Identifiant unique pour cet ESP32
String BLENAME = "Player:2";
String Player = String(2);
String Etat = "0";  // "0" normal, "1" touché

// ---------- États jeu ----------
bool invulnerable = false;
float reperage = 0;    // jauge 0..REP_MAX
float difflvl = 1.0f;  // influencé par Unity
int vol = 0;
unsigned long startEtat = 0;
int endEtat = 0;
unsigned long starting = 0;

int Heal = 0;
unsigned long lastTickTime = 0;
unsigned long startHealingBorne = 0;
bool HealingBorne = false;

#define LED1 26
#define LED2 27

// ---------- MQTT ----------
WiFiClient espClient;
PubSubClient client(espClient);

// ---------- TSOP / IRremote ----------

static const uint8_t RECV_PIN = 4;  // TSOP sur GPIO15
// static const uint32_t KILLER_CODE = 0xA90;       // À adapter exactement au code lampe
static const bool ACCEPT_ANY_CODE = false;  // true pour debug (accepte tout)
// static const uint8_t  RECV_PIN_MODE = INPUT;     // passer à INPUT_PULLUP si besoin

// decode_results results;

// ---------- Rafales / Incrément ----------
// static const int BURST_N = 11;                    // nb de codes pour 1 incrément
// static const uint32_t BURST_GAP_RESET_MS = 300;  // reset de la rafale si trou > 200ms
// int burstCount = 0;
// uint32_t lastBurstAt = 0;  // dernier décodage pris en compte
// int lastSeenID = -1;
int ID = -1;

int InvulnerableTimer = 0;
// Anti-spam du tick
// static const uint32_t MIN_TICK_SPACING_MS = 500;  // min 120ms entre 2 ticks audio
// uint32_t lastTickSoundAt = 0;

// ---------- Fenêtre “présence IR” utile au decay global ----------
static const uint32_t IR_BEAM_HOLD_MS = 500;  // pour le decay passif
volatile uint32_t lastValidIrMs = 0;

// ---------- Timers ----------
const unsigned long mqttInterval = 250;  // cadence de montée abstraite (garde-le si tu veux la difficulté côté Unity)
unsigned long invTime = 0;

// ---------- DFPlayer ----------
#define RX_PIN 16
#define TX_PIN 17
DFRobotDFPlayerMini myDFPlayer;

// Pistes (adapte si besoin)
static const uint8_t TRACK_TICK = 5;  // beep court sur incrément
static const uint8_t TRACK_HIT = 2;   // hit validé
static const uint8_t TRACK_LOSE = 3;  // loose
static const uint8_t TRACK_WIN = 4;   // win

// ---------- Gameplay ----------
// const float REP_MAX = 4.0f;             // seuil de hit
// const float DECAY_STEP = 0.3f;          // décroissance
// const uint32_t DECAY_PERIOD_MS = =000;  // période de décroissance
// const uint32_t VOL_BASE = 3;            // volume offset
// const uint32_t VOL_GAIN = 10;           // volume slope
// uint32_t now = millis();
bool vibration = false;
// bool Lastvibration = false;
bool firstIR = false;
bool findSent = false;
unsigned long firstIRTime = 0;
int TickLife = 0;
bool irIddle = false;
bool irShoot = false;

// ---------- FreeRTOS ----------
TaskHandle_t networkTaskHandle;
TaskHandle_t gameLogicTaskHandle;
TaskHandle_t bleAdvertisingTaskHandle;
TaskHandle_t audioTaskHandle;

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
// en haut, prototypes
void sendToLanterne(const char* message);
// ---------- HIT/TICK COUNTERS & ACKS ----------
volatile uint32_t hitCount = 0;
volatile uint32_t tickCount = 0;
// ✅ Séparer nettement les états HIT et TICK
uint32_t lastSentHitCount = 0;    // dernier hit envoyé
uint32_t lastAckedHitCount = 0;   // dernier hit ACKé
unsigned long lastHitSentAt = 0;  // timestamp dernier envoi HIT

uint32_t lastSentTickCount = 0;    // dernier tick envoyé
uint32_t lastAckedTickCount = 0;   // dernier tick ACKé
unsigned long lastTickSentAt = 0;  // timestamp dernier envoi TICK
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

// Avant la boucle decode, ajoute une fonction util pour mapper adresse/commande en killerID
// int mapIrToKillerID(const uint32_t address, const uint32_t command) {
//   // exemple simple — adapte les adresses/commandes réelles de tes lampes
//   if (address == 0x0000 && command == 0xA9) return 1;
//   if (address == 0x0001 && command == 0xA9) return 2;
//   if (address == 0x0002 && command == 0xA9) return 3;
//   // fallback : -1 inconnu
//   return -1;
// }

// ----- AUDIO COMMANDS -----
enum AudioCmdType { AUD_PLAY,
                    AUD_LOOP,
                    AUD_STOP,
                    AUD_VOL };
struct AudioCmd {
  AudioCmdType type;
  uint16_t track;  // pour PLAY/LOOP
  uint8_t volume;  // pour VOL (0..30)
};

QueueHandle_t audioQ = nullptr;

inline void audioPlay(uint16_t track) {
  AudioCmd c{ AUD_PLAY, track, 0 };
  xQueueSend(audioQ, &c, 0);
}
inline void audioLoop(uint16_t track) {
  AudioCmd c{ AUD_LOOP, track, 0 };
  xQueueSend(audioQ, &c, 0);
}
inline void audioStop() {
  AudioCmd c{ AUD_STOP, 0, 0 };
  xQueueSend(audioQ, &c, 0);
}
inline void audioSetVol(uint8_t v) {
  AudioCmd c{ AUD_VOL, 0, v };
  xQueueSend(audioQ, &c, 0);
}

// --- Lecteur minimal : 1 en cours + 1 en attente ---
volatile bool dfp_playing = false;
volatile bool dfp_looping = false;
volatile int dfp_current = -1;
volatile int dfp_next = -1;
volatile bool dfp_next_loop = false;
volatile uint32_t dfp_started_ms = 0;

const uint32_t DFP_GAP_MS = 300;          // délai entre 2 trames
const uint32_t DFP_WATCHDOG_MS = 180000;  // 3 min secours si pas d’événement

static void dfpStartNow(int track, bool loop) {
  if (loop) myDFPlayer.loop(track);
  else myDFPlayer.play(track);
  dfp_playing = true;
  dfp_looping = loop;
  dfp_current = track;
  dfp_started_ms = millis();
  vTaskDelay(pdMS_TO_TICKS(DFP_GAP_MS));
}

// --- Healing "breathing" ---
const uint16_t HEAL_PERIOD_MS = 1600;  // durée d'un cycle complet
const uint8_t  HEAL_MIN_LVL   = 6;     // intensité mini du bleu pendant healing
const uint8_t  HEAL_MAX_LVL   = 35;    // intensité maxi du bleu pendant healing

// calcule un niveau 0..255 selon une courbe cos lissée
inline uint8_t healBreathLevel() {
  uint32_t base = HealingBorne ? (millis() - startHealingBorne) : millis();
  float x = (base % HEAL_PERIOD_MS) / (float)HEAL_PERIOD_MS;     // 0..1
  float curve = (1.0f - cosf(2.0f * 3.14159f * x)) * 0.5f;       // 0..1 lissé
  float lvl = HEAL_MIN_LVL + curve * (HEAL_MAX_LVL - HEAL_MIN_LVL);
  return (uint8_t)lvl;
}


// --- LED render (non-bloquant) ---
const uint16_t INVUL_PERIOD_MS = 1000;  // période totale du clignotement
const uint16_t INVUL_FLASH_MS = 90;    // durée du flash blanc
const uint8_t BLUE_LEVEL = 20;         // intensité du bleu TickLife
const uint8_t WHITE_LEVEL = 20;        // intensité du flash blanc

// Affichage "de base" = TickLife en bleu
void renderLifeBase() {
  RubLed.clear();
  int n = constrain(TickLife, 0, NUM_LEDS);
  for (int i = 0; i < n; i++) {
    RubLed.setPixelColor(i, RubLed.Color(0, 0, BLUE_LEVEL));
  }
  RubLed.show();
}

// Overlay invulnérable : bref flash blanc puis retour au bleu TickLife
void renderLEDs() {
  // 1) Invulnérable : flash blanc court, puis on retombe sur le rendu "fond" (healing ou base)
  if (invulnerable) {
    uint32_t phase = (millis() - invTime) % INVUL_PERIOD_MS;
    if (phase < INVUL_FLASH_MS) {
      for (int i = 0; i < NUM_LEDS; i++) {
        RubLed.setPixelColor(i, RubLed.Color(WHITE_LEVEL, WHITE_LEVEL, WHITE_LEVEL));
      }
      RubLed.show();
      return; // le flash est prioritaire
    }
    // sinon on laisse continuer pour afficher le fond après le flash
  }

  // 2) HealingBorne : respiration du bleu, en gardant le nombre de LEDs = TickLife
  if (HealingBorne) {
    RubLed.clear();
    int n = constrain(TickLife, 0, NUM_LEDS);
    uint8_t lvl = healBreathLevel();      // 6..35 par défaut
    for (int i = 0; i < n; i++) {
      RubLed.setPixelColor(i, RubLed.Color(0, 0, lvl));
    }
    RubLed.show();
    return;
  }

  // 3) Fond "normal" = TickLife en bleu fixe
  renderLifeBase();
}



void VibrationManager() {
  unsigned long currentMillis = millis();
  // unsigned long nowVib = 0;

  if (currentMillis - starting > 3000 && !irIddle && !irShoot) {
    digitalWrite(LED1, LOW);
    digitalWrite(LED2, LOW);
  }

  if (currentMillis - starting < 3000) {
    analogWrite(MOTOR_PIN, 255);
    digitalWrite(LED1, HIGH);
    digitalWrite(LED2, HIGH);
  }

  else if (invulnerable) {
    const unsigned long cycle = (currentMillis - invTime) % 2000;
    // 0–100ms ON, 100–200 OFF, 200–300 ON, 300–1200 OFF
    if ((cycle < 300) || (cycle >= 600 && cycle < 900)) analogWrite(MOTOR_PIN, 200);
    else analogWrite(MOTOR_PIN, 0);
  }

  else if (irIddle) {
    analogWrite(MOTOR_PIN, 200);
    // digitalWrite(26, HIGH);
  }

  else if (HealingBorne) {
    // cycle de respiration total (ms)
    const unsigned long period = 1600;
    const int minPower = 30;
    const int maxPower = 150;

    unsigned long now = millis();
    float x = (now % period) / (float)period;                 // 0..1 dans le cycle
    float curve = (1.0f - cosf(2.0f * 3.14159f * x)) * 0.5f;  // courbe lisse montée/descente
    int pwm = (int)(minPower + curve * (maxPower - minPower));

    analogWrite(MOTOR_PIN, pwm);
  }


  else if (TickLife >= 3) {
    // Mode double vibration rythmée
    unsigned long cycle = currentMillis % 3000;  // Durée totale d’un cycle (1.2 seconde)

    // Pattern : vibration - 100ms, pause - 100ms, vibration - 100ms, pause - 900ms
    if ((cycle < 1000)) {
      analogWrite(MOTOR_PIN, 150);
    } else {
      analogWrite(MOTOR_PIN, 0);
    }
  }

  else {
    analogWrite(MOTOR_PIN, 0);

    // digitalWrite(26, LOW);
  }
}

// ===================================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(MOTOR_PIN, OUTPUT);
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);

  // ---------- DFPlayer ----------
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  vTaskDelay(1500 / portTICK_PERIOD_MS);
  if (!myDFPlayer.begin(Serial2)) {
    Serial.println("DFPlayer Mini non détecté !");
    // while (true) { vTaskDelay(100 / portTICK_PERIOD_MS); }
  }
  Serial.println("DFPlayer Mini OK.");
  myDFPlayer.volume(30);

  RubLed.begin();
  RubLed.setBrightness(20);
  RubLed.show();


  // ---------- TSOP / IRremote ----------
  pinMode(RECV_PIN, INPUT);  // PAS de pullup sur un TSOP 4838
  IrReceiver.begin(RECV_PIN);
  Serial.println("TSOP prêt (Arduino-IRremote).");

  Serial.println(esp32_id);

  ble.begin(BLENAME);

  audioQ = xQueueCreate(16, sizeof(AudioCmd));

  // ---------- Wi-Fi events ----------
  WiFi.onEvent(WiFiEvent);

  // ---------- Tâches ----------
  xTaskCreatePinnedToCore(networkTask, "NetworkTask", 8192, NULL, 2, &networkTaskHandle, 0);
  xTaskCreatePinnedToCore(gameLogicTask, "GameLogicTask", 8192, NULL, 4, &gameLogicTaskHandle, 1);
  xTaskCreatePinnedToCore(bleAdvertisingTask, "BLEAdvertisingTask", 4096, NULL, 1, &bleAdvertisingTaskHandle, 0);
  xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 3, &audioTaskHandle, 1);
}

void loop() { /* tout est en tasks */
}

void audioTask(void*) {
  const TickType_t idleDelay = pdMS_TO_TICKS(5);
  AudioCmd cmd;

  for (;;) {
    // 1) Récupère toutes les commandes envoyées par audioPlay/Loop/Stop/Vol
    while (xQueueReceive(audioQ, &cmd, 0) == pdTRUE) {
      switch (cmd.type) {
        case AUD_PLAY:
          if (!dfp_playing) dfpStartNow(cmd.track, false);
          else {
            dfp_next = (int)cmd.track;
            dfp_next_loop = false;
          }
          break;

        case AUD_LOOP:
          if (!dfp_playing) dfpStartNow(cmd.track, true);
          else {
            dfp_next = (int)cmd.track;
            dfp_next_loop = true;
          }
          break;

        case AUD_STOP:
          myDFPlayer.stop();
          dfp_playing = false;
          dfp_looping = false;
          dfp_current = -1;
          dfp_next = -1;  // on efface aussi l’attente
          vTaskDelay(pdMS_TO_TICKS(DFP_GAP_MS));
          break;

        case AUD_VOL:
          myDFPlayer.volume(constrain(cmd.volume, 0, 30));
          vTaskDelay(pdMS_TO_TICKS(DFP_GAP_MS));
          break;
      }
    }

    // 2) Fin de piste par événement série (safe)
    if (myDFPlayer.available()) {
      uint8_t t = myDFPlayer.readType();
      (void)myDFPlayer.read();  // consomme la valeur associée si présente
      if (t == DFPlayerPlayFinished) {
        dfp_playing = false;  // la piste non-loop vient de finir
      }
      // (option) gérer DFPlayerError ici
    }

    // 3) Watchdog de secours si l’événement n’arrive pas (clones capricieux)
    if (dfp_playing && !dfp_looping) {
      if (millis() - dfp_started_ms > DFP_WATCHDOG_MS) {
        dfp_playing = false;  // on considère la piste terminée
      }
    }

    // 4) Si libre et une piste est en attente, on la lance
    if (!dfp_playing && dfp_next != -1) {
      dfpStartNow(dfp_next, dfp_next_loop);
      dfp_next = -1;
    }

    vTaskDelay(idleDelay);
  }
}

// ===================================================================================
//                                   GAME LOGIC
// ===================================================================================
void gameLogicTask(void* parameter) {
  uint32_t lastDecayTick = millis();

  while (true) {

    VibrationManager();
    // life();
    renderLEDs();
    HealBorne();
    // audioLoop(5);
    // digitalWrite(26, HIGH);
    // Serial.println(TickLife);
    // now = millis();
    // // --- Logique rafale ---
    // if (now - lastBurstAt > BURST_GAP_RESET_MS) {
    //   burstCount = 0;  // trou trop long: on repart de zéro
    //   vibration = false;
    // }

    // if (now - lastTickSoundAt > MIN_TICK_SPACING_MS && Lastvibration && !invulnerable) {
    //   vibration = false;
    //   Lastvibration = false;
    //   if (burstCount <= 0) analogWrite(MOTOR_PIN, 0);  // PWM 0-255
    // }


    // ----- DÉCODE IR -----
    if (IrReceiver.decode()) {
      irIddle = false;
      irShoot = false;
      auto& d = IrReceiver.decodedIRData;
      IrReceiver.printIRResultShort(&Serial);  // résumé propre (protocole, adresse, commande)



      if (ACCEPT_ANY_CODE) {
        // valid = true;
      } else {
        if (d.address == 0x0000 && d.command == 0xA9) {
          ID = 1;
          irIddle = true;
          lastValidIrMs = millis();
        }
        if (d.address == 0x0001 && d.command == 0xA9) {
          ID = 1;
          irShoot = true;
          lastValidIrMs = millis();
        }

        if (d.address == 0x0010 && d.command == 0xA9) {
          ID = 2;
          irIddle = true;
          lastValidIrMs = millis();
        }
        if (d.address == 0x0011 && d.command == 0xA9) {
          ID = 2;
          irShoot = true;
          lastValidIrMs = millis();
        }
        // mappe l'adresse/commande vers un killerID
        // ID = mapIrToKillerID(d.address, d.command);
        // Serial.println(ID);
        // lastSeenID = ID;
        // if (ID >= 0) valid = true;
      }

      if ((irIddle || irShoot) && invulnerable) {
        digitalWrite(LED1, HIGH);
        digitalWrite(LED2, HIGH);
      }

      if (irIddle && !invulnerable) {
        if (!firstIR) {
          firstIRTime = millis();
          firstIR = true;
        }
        if (lastValidIrMs - firstIRTime > 3000 && firstIR && !findSent) {
          sendToLanterne("FIND");
          findSent = true;
        }
        // if (!vibration) {
        //   starting = millis();
        //   vibration = true;
        // }
      }

      if (irShoot && !invulnerable) {
        TickLife += 1;
        if (TickLife < 3) {
          sendToLanterne("TICK");
          tickCount++;
          Heal = 120000;
          lastTickTime = millis();
          lastSentTickCount = tickCount;
          lastTickSentAt = millis();
          publishTickCount(lastSentTickCount);
          // analogWrite(MOTOR_PIN, 255);
          starting = millis();
          EtatBLE(1, 20000);
          // invTime = millis();
          invulnerableF(10000);
          audioPlay(TRACK_TICK);
        }

        if (TickLife >= 3) {
          // TickLife = 0;
          sendToLanterne("HIT");
          Serial.println("HIT validé → piste HIT");


          EtatBLE(1, 60000);
          // invTime = millis();
          invulnerableF(30000);
          Heal = 120000;
          lastTickTime = millis();
          // Compteur + annonce (HIT_COUNT)
          hitCount++;
          lastSentHitCount = hitCount;
          lastHitSentAt = millis();
          publishHitCount(lastSentHitCount);
          // analogWrite(MOTOR_PIN, 255);

          starting = millis();

          audioPlay(2);
        }
      }

      // if (irIddle && !invulnerable) {
      //   const uint32_t nowMs = millis();
      //   lastValidIrMs = nowMs;
      //   lastBurstAt   = nowMs;
      //   sendToLanterne("FIND");
      //   burstCount++;
      //   if (!vibration) {
      //     analogWrite(MOTOR_PIN, 255);
      //     vibration = true;
      //   }

      //   const int burstThreshold = max(1, (int)ceilf((float)BURST_N - difflvl));
      //   if (burstCount >= burstThreshold) {
      //     burstCount = 0;
      //     Lastvibration = true;

      //     reperage += 1.0f;
      //     if (reperage > REP_MAX) reperage = REP_MAX;

      //     // vol = (int)(reperage * VOL_GAIN) + VOL_BASE;

      //     if ((nowMs - lastTickSoundAt) >= MIN_TICK_SPACING_MS && reperage < REP_MAX) {
      //       // myDFPlayer.volume(vol);
      //       vTaskDelay(100 / portTICK_PERIOD_MS);
      //       myDFPlayer.play(TRACK_TICK);
      //       vTaskDelay(400 / portTICK_PERIOD_MS);
      //       lastTickSoundAt = nowMs;
      //           // ✅ TICK_COUNT++
      //           tickCount++;
      //           lastSentTickCount = tickCount;
      //           lastTickSentAt = millis();
      //           publishTickCount(lastSentTickCount);    // ✅ corrige l’erreur : on publie bien un TICK, pas un HIT
      //           sendToLanterne("TICK");
      //     }
      //   }
      // }

      IrReceiver.resume();
    }



    // ----- VALIDATION D’UN HIT -----
    // if (reperage >= REP_MAX && !invulnerable) {
    //   Serial.println("HIT validé → piste HIT");
    //   // myDFPlayer.volume(30);
    //   vTaskDelay(100 / portTICK_PERIOD_MS);
    //   myDFPlayer.play(2);
    //   vTaskDelay(500 / portTICK_PERIOD_MS);

    //   Etat = "1";
    //   invTime = millis();
    //   analogWrite(MOTOR_PIN, 255); // PWM 0-255
    //   reperage = 0;
    //   invulnerable = true;
    //   // digitalWrite(LED, HIGH);

    //   // Compteur + annonce (HIT_COUNT)
    //   hitCount++;
    //   lastSentHitCount = hitCount;
    //   lastHitSentAt = millis();
    //   publishHitCount(lastSentHitCount);
    //   sendToLanterne("HIT");
    // }

    // ----- DÉCROISSANCE SI PLUS DE PRÉSENCE -----
    if (!irBeamPresent() && invulnerable) {
      digitalWrite(LED1, LOW);
      digitalWrite(LED2, LOW);
      irIddle = false;
      irShoot = false;
    }
    if (!irBeamPresent() && !invulnerable) {
      // uint32_t now = millis();
      // if ((now - lastDecayTick) >= DECAY_PERIOD_MS) {
      // reperage -= DECAY_STEP;
      // if (reperage < 0) reperage = 0;
      // lastDecayTick = now;
      // if (analogRead(MOTOR_PIN) != 0) analogWrite(MOTOR_PIN, 0);  // PWM 0-255
      if (findSent) {
        sendToLanterne("LOST");
        findSent = false;
      }
      firstIR = false;
      vibration = false;
      irIddle = false;
      irShoot = false;
      digitalWrite(LED1, LOW);
      digitalWrite(LED2, LOW);
      // sendToLanterne("LOST");
      // }
    }

    // ----- FIN D’INVULNÉRABILITÉ (10 s) -----
    if (millis() - invTime >= InvulnerableTimer && invulnerable) {
      invulnerable = false;
      // reperage = 0;
      sendDetectionMessage("IR_notdetected");
      // Etat = "0";
      // digitalWrite(LED, LOW);
      // myDFPlayer.volume(0);
    }

    if (millis() - startEtat >= endEtat && Etat == "1") {
      // invulnerable = false;
      // reperage = 0;
      // sendDetectionMessage("IR_notdetected");
      if (TickLife == 0) {
        Etat = "0";
      } else if (TickLife != 0) {
        Etat = "3";
      }
      // digitalWrite(LED, LOW);
      // myDFPlayer.volume(0);
    }

    if (millis() - lastTickTime > Heal && TickLife != 0 && !invulnerable && !HealingBorne) {
      HealTime();
      lastTickTime = millis();
    }

    //----- RESYNC HIT_COUNT (option réactivée si besoin) -----
    if (millis() - lastHitSentAt >= RESYNC_PERIOD_MS) {
      if (lastAckedHitCount != lastSentHitCount && lastSentHitCount != 0) {
        publishHitCount(lastSentHitCount);
        // sendToLanterne(lastSeenID, "HIT", lastSentHitCount);
      }
      lastHitSentAt = millis();
    }

    if (millis() - lastTickSentAt >= RESYNC_PERIOD_MS) {
      if (lastAckedTickCount != lastSentTickCount && lastSentTickCount != 0) {
        publishTickCount(lastSentTickCount);
        // sendToLanterne(lastSeenID, "TICK", lastSentTickCount);
      }
      lastTickSentAt = millis();
    }


    vTaskDelay(8 / portTICK_PERIOD_MS);
  }
}


// implémentation (place après sendDetectionMessage ou en bas)
void sendToLanterne(const char* message) {
  String msg = String(esp32_id) + ":" + message;
  client.publish("esp32/lanterne", msg.c_str(), false);
  Serial.println("Send : " + msg);
}

void invulnerableF(int invulTime) {
  invTime = millis();
  invulnerable = true;
  InvulnerableTimer = invulTime;
}

void EtatBLE(int e, int etatTimer) {
  Etat = String(e);
  endEtat = etatTimer;
  startEtat = millis();
}

void HealTime() {
  TickLife -= 1;
  if (TickLife < 0) TickLife = 0;
  lastTickTime = millis();
}

void HealBorne() {
  int HealingBorneTick = 30000;
  if (HealingBorne && !invulnerable) {
    if (millis() - startHealingBorne > HealingBorneTick) {
      TickLife = 0;
    }
  }
}

void life() {
  RubLed.clear();
  for (int i = 0; i < TickLife; i++) {
    RubLed.setPixelColor(i, RubLed.Color(0, 0, 30));
  }
  RubLed.show();
  // for (int y = TickLife; y < 3; y++)
  // {
  //   RubLed.setPixelColor(y, RubLed.Color(0,0,0));
  //   RubLed.show();
  // }
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
    Serial.print("Difflvl = ");
    Serial.println(difflvl, 3);
  }

  if (message.endsWith("WIN")) {
    Serial.println("WIN");
    audioStop();
    audioPlay(TRACK_WIN);
  }

  if (message.endsWith("LOOSE")) {
    Serial.println("LOOSE");
    audioStop();
    audioPlay(TRACK_LOSE);
  }

  // ✅ ACK HIT_COUNT : "Player0:ACK_COUNT=N"
  if (message.startsWith(String(esp32_id) + ":ACK_COUNT=")) {
    int eq = message.lastIndexOf('=');
    if (eq >= 0) {
      uint32_t k = (uint32_t)strtoul(message.c_str() + eq + 1, nullptr, 10);
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
      uint32_t k = (uint32_t)strtoul(message.c_str() + eq + 1, nullptr, 10);
      if (k == lastSentTickCount) {
        lastAckedTickCount = k;
        Serial.printf("[ACK TICK] ok for count=%u\n", (unsigned)k);
      } else {
        Serial.printf("[ACK TICK] ignoré (attendu=%u, reçu=%u)\n",
                      (unsigned)lastSentTickCount, (unsigned)k);
      }
    }
  }

  if (message.startsWith(String(esp32_id) + ":SABOTAGE")) {
    // audioPlay(); piste sabotage gen
  }

  if (message.startsWith(String(esp32_id) + ":Healing")) {
    HealingBorne = true;
    startHealingBorne = millis();
  }

  if (message.startsWith(String(esp32_id) + ":StopHealing")) {
    HealingBorne = false;
  }


  if (message.endsWith("RESET")) {
    Serial.println("[RESET] remise à zéro");

    // HIT
    hitCount = 0;
    lastSentHitCount = 0;
    lastAckedHitCount = 0;
    lastHitSentAt = millis();
    publishHitCount(0);

    // ✅ TICK
    tickCount = 0;
    lastSentTickCount = 0;
    lastAckedTickCount = 0;
    lastTickSentAt = millis();
    publishTickCount(0);

    reperage = 0;
    invulnerable = false;
    Etat = "0";
    TickLife = 0;
    // burstCount = 0;
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
