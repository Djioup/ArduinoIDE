#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>
// #include <BLEDevice.h>
// #include <BLEUtils.h>
// #include <BLEScan.h>
#include <NimBLEDevice.h>
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_NeoPixel.h>

// Informations Wi-Fi
const char* ssid = "PelucheGang";
const char* password = "CACHE-CACHEKILLER";
const char* mqtt_server = "192.168.0.139";
const char* mqtt_user = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";
const char* esp32_id = "Generator5";

// WiFi et MQTT
WiFiClient espClient;
PubSubClient client(espClient);
NimBLEScan* pBLEScan;
#define SCAN_TIME 3000
// BLEScan* pBLEScan;
// #define SCAN_TIME 3

// DFPlayer Mini Configuration
#define RX_PIN 16
#define TX_PIN 17
DFRobotDFPlayerMini myDFPlayer;
// Capteur et LEDs
const int MAG_SENSOR_1 = 27;
const int MAG_SENSOR_2 = 14;  // 14
const int GENERATOR_BUTTON_PIN = 23;
const int LED_BUTTON_PIN = 22;
const int LED_DECORATIVE_PIN = 5;  //5
// const int LED_DECORATIVE_PIN2 = 19;
// const int LED_DECORATIVE_PIN3 = 18;
// const int LED_DECORATIVE_PIN4 = 33;
// INTERUPT1 // 21
// INTERUPT2 // 19
// INTERUPT3 // 18
// rubanLEd // 4

// // === MINI-JEU "PINCE CROCO" (actif uniquement en COUNTING) ===================
// #define LED_OK 2                  // LED intégrée comme feedback
// int MG_PORTS[] = { 25, 32, 33 };  // ports à toucher avec la pince (GND)
// const int MG_N = sizeof(MG_PORTS) / sizeof(MG_PORTS[0]);

// int mgTargetIdx = -1;  // index du port cible (dans MG_PORTS)
// bool mgHasTarget = false;
// --- Pince croco (contacts vers GND, pullup) ---
const int CROCO_PINS[] = { 26, 32, 33 };
const int CROCO_N = sizeof(CROCO_PINS) / sizeof(CROCO_PINS[0]);

// --- Interrupteurs (contacts vers GND, pullup) ---
const int SW_PINS[] = { 18, 19, 21 };
const int SW_N = sizeof(SW_PINS) / sizeof(SW_PINS[0]);
const bool SW_ACTIVE_LOW = true;

// --- Potentiomètres (ADC) ---
#define POT1 34
#define POT2 35

// #define LED_OK 2


#define ANNEAULED 13
#define NUM_LEDS 24
Adafruit_NeoPixel ring(NUM_LEDS, ANNEAULED, NEO_GRB + NEO_KHZ800);

#define IND_PIN 4  // <— choisis une pin dispo
#define IND_NUM 3  // 3 voyants: [0]=croco, [1]=switches, [2]=pots
Adafruit_NeoPixel indicators(IND_NUM, IND_PIN, NEO_GRB + NEO_KHZ800);


unsigned long lastBlinkTime = 0;
bool blinkState = false;
const int blinkInterval = 300;  // millisecondes
int KillerTurn = 0;

bool isLooped = false;
bool JustOnce = false;
bool JustOnce2 = false;
const int NbrTour = 9;
int SpottedPlayer = 0;
float diffLvl = 1;
unsigned long ShockTime;
static int lastAnnouncedTurn = 0;
unsigned long lastTurnDetectedTime = 0;  // Timestamp du dernier tour détecté
unsigned long KillerTime = 0;
bool ReglageOK = true;
int TimeDecharge = 30000;
unsigned long Decharge = 0;
int RegFalse = 0;
int NbTrue = 0;
int playerNameSpotted = -1;

enum GeneratorState {
  COUNTING,
  WAITING,
  BUTTON_PHASE,
  COUNTING2,
  FINISHED,
  SHOCK,
  OUT,
  KILLERHOLDING,
  INITIATE,
  WAITP3,
  PHASE3
};
GeneratorState previousGenState;  // Variable pour stocker l’état précédent
GeneratorState genState = INITIATE;
unsigned long stateStartTime = 0;
unsigned long buttonHoldStartTime = 0;

int playerRSSI;

bool capteur2Passe = false;      // Variable pour savoir si le capteur secondaire a été traversé
int turnCount = 0;               // Nombre de tours validés
unsigned long lastTurnTime = 0;  // Anti-rebond global
bool Shocked = false;
bool Killerholding = false;

unsigned long previousMillis = 0;
float tLED = 0;
// Variables bouton
unsigned long buttonPressStart = 0;
bool buttonWasPressed = false;
const unsigned long LONG_PRESS_TIME = 10000;  // 3 secondes

TaskHandle_t bleScanTaskHandle;
TaskHandle_t mqttTaskHandle;
TaskHandle_t gameTaskHandle;
TaskHandle_t audioTaskHandle;

// Variables pour gérer les reconnexions
const int maxReconnectAttempts = 5;  // Seuil avant de redémarrer le Wi-Fi
int reconnectAttempts = 0;           // Compteur d'échecs de reconnexion

// --- Utils couleurs & état animations ---
inline uint32_t RGB(uint8_t r, uint8_t g, uint8_t b) {
  return ring.Color(r, g, b);
}

// Etat SHOCK
struct ShockAnim {
  unsigned long t = 0;
  bool alt = false;
  unsigned long burstT = 0;
} shockAnim;

// Etat KILLERHOLDING
struct HoldAnim {
  unsigned long t = 0;
  int pos = 0;
  int lastTurn = -1;
  unsigned long flashStart = 0;
} holdAnim;

// --- Tracking multi-joueurs (max 8 gilets) ---
struct PlayerSeen {
  String key;       // "PlayerX"
  int spotted = 0;  // E: 0/1
  int rssi = -127;  // dernier RSSI
  unsigned long lastSeen = 0;
};

PlayerSeen players[8];                // petit cache statique
const unsigned long STALE_MS = 5000;  // périmé si > 4s

int findSlotByKey(const String& k) {
  for (int i = 0; i < 8; i++)
    if (players[i].key == k) return i;
  return -1;
}
int findFreeOrOldestSlot() {
  int freeIdx = -1;
  unsigned long oldest = ULONG_MAX;
  int oldestIdx = -1;
  for (int i = 0; i < 8; i++) {
    if (players[i].key.length() == 0) {
      freeIdx = i;
      break;
    }
    if (players[i].lastSeen < oldest) {
      oldest = players[i].lastSeen;
      oldestIdx = i;
    }
  }
  return (freeIdx != -1) ? freeIdx : oldestIdx;
}

// Retourne true s'il existe AU MOINS UN player pour lequel spotted==1 ET RSSI OK ET récent
bool anyThreat(int rssiThreshold /*ex: -100*/) {
  unsigned long now = millis();
  for (int i = 0; i < 8; i++) {
    if (players[i].key.length() == 0) continue;
    bool fresh = (now - players[i].lastSeen) <= STALE_MS;
    if (fresh && players[i].spotted == 1 && players[i].rssi > rssiThreshold)
    {
      playerNameSpotted = i;
      return true;
    } 
  }
  return false;
}

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

// ====== États/cibles par mini-jeu (évalués en continu) ======
enum MiniGameId { MG_CROC,
                  MG_SWITCHES,
                  MG_POTS };

// CROCO
int crocoTargetIdx = -1;  // index 0..2
bool crocoOK = false;
unsigned long crocoLowSince = 0;

// SWITCHES (cible 3 bits 0..7, stabilité 3 s)
byte swTarget = 0;
bool swOK = false;
bool swInMatch = false;
unsigned long swMatchSince = 0;

// POTS (cible somme, fenêtre & stabilité 3 s)
int potTarget = 0;
bool potsOK = false;
bool potInRange = false;
unsigned long potGoodSince = 0;

// Fenêtres/temporisations (ajuste si besoin)
// const unsigned long CROCO_HOLD_MS = 50;
// const unsigned long MATCH_HOLD_MS = 3000;
// const int POT_TIGHT = 80;  // “parfait”
// const int POT_NEAR = 500;  // “on s'approche”

// ---- À mettre avec tes autres constantes (haut du fichier) ----
const unsigned long CROCO_HOLD_MS = 50;    // LOW maintenu
const unsigned long SW_DEBOUNCE_MS = 20;   // stabilité par switch
const unsigned long MATCH_HOLD_MS = 3000;  // maintien combinaison OK
const int POT_TIGHT = 500;                 // fenêtre "parfait"
const int POT_NEAR = 1000;                 // feedback proche (non validant)
const int POT_HYST = 40;                   // hystérésis autour de POT_TIGHT
const int POT_OVERSAMPLE = 4;              // x lectures pour réduire le bruit
const float POT_EMA_ALPHA = 0.25f;         // lissage EMA (0..1)

// ---- Statics de filtrage (à mettre en global, près des états mini-jeux) ----
// CROCO
static int croco_lastPin = -1;
static bool croco_latchedLow = false;

// SWITCHES (un petit filtre par broche)
static bool sw_lastRaw[3] = { false, false, false };
static bool sw_stable[3] = { false, false, false };
static unsigned long sw_since[3] = { 0, 0, 0 };

// POTS (EMA sur la somme)
static float pot_emaSum = 0.0f;

// Front descendant de ReglageOK (true -> false) sans variable globale explicite
static inline bool onFall_ReglageOK(bool cur) {
  static bool init = false;
  static bool old;
  if (!init) {
    old = cur;
    init = true;
    return false;
  }                           // synchro initiale
  bool fire = (!cur && old);  // true -> false
  old = cur;
  JustOnce = false;
  return fire;
}

static inline bool onRise_ReglageOK(bool cur) {
  static bool init = false;
  static bool old;
  if (!init) {
    old = cur;
    init = true;
    return false;
  }                           // synchro initiale
  bool fire = (cur && !old);  // false -> true
  old = cur;
  JustOnce = false;
  return fire;
}


static inline bool readSwitchPin(int pin) {
  int v = digitalRead(pin);
  return SW_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

void initAllTargets() {
  // Croco: choisir une borne
  crocoTargetIdx = random(0, CROCO_N);
  crocoOK = false;
  crocoLowSince = 0;
  Serial.printf("[Init] Croco cible GPIO %d\n", CROCO_PINS[crocoTargetIdx]);

  // Switches: 3 bits
  swTarget = (byte)random(0, 8);
  swOK = false;
  swInMatch = false;
  swMatchSince = 0;
  Serial.printf("[Init] Switches cible = %d%d%d\n",
                (swTarget >> 2) & 1, (swTarget >> 1) & 1, swTarget & 1);

  // Pots: somme
  potTarget = random(600, 7000);
  potsOK = false;
  potInRange = false;
  potGoodSince = 0;
  Serial.printf("[Init] Pots cible somme = %d\n", potTarget);
}

// Change la cible d’UN seul jeu (au hasard, ou impose id=MG_CROC/MG_SWITCHES/MG_POTS)
void pickNewTarget(int id = -1) {
  if (id < 0 || id > 2) id = random(0, 3);
  switch ((MiniGameId)id) {
    case MG_CROC:
      {
        int old = crocoTargetIdx;
        int idx;
        do { idx = random(0, CROCO_N); } while (CROCO_N > 1 && idx == old);
        crocoTargetIdx = idx;
        crocoOK = false;
        crocoLowSince = 0;
        Serial.printf("[Target] Croco -> GPIO %d\n", CROCO_PINS[crocoTargetIdx]);
      }
      break;

    case MG_SWITCHES:
      {
        byte old = swTarget;
        byte tgt;
        do { tgt = (byte)random(0, 8); } while (tgt == old);
        swTarget = tgt;
        swOK = false;
        swInMatch = false;
        swMatchSince = 0;
        Serial.printf("[Target] Switches -> %d%d%d\n",
                      (swTarget >> 2) & 1, (swTarget >> 1) & 1, swTarget & 1);
      }
      break;

    case MG_POTS:
      {
        int old = potTarget;
        int tgt;
        do { tgt = random(600, 7000); } while (tgt == old);
        potTarget = tgt;
        potsOK = false;
        potInRange = false;
        potGoodSince = 0;
        Serial.printf("[Target] Pots -> somme %d\n", potTarget);
      }
      break;
  }
}
void updateAllMiniGames() {
  unsigned long now = millis();

  // ----- CROCO -----
  // ---------------- CROCO ----------------
  {
    const int pin = CROCO_PINS[crocoTargetIdx];
    // (1) Réaffirme PULLUP chaque passe
    pinMode(pin, INPUT_PULLUP);

    // (2) Triple sample ultracourt pour lisser
    bool sampleLow = (digitalRead(pin) == LOW);
    if (sampleLow) {
      for (int i = 0; i < 3; ++i) {
        delayMicroseconds(300);
        if (digitalRead(pin) != LOW) {
          sampleLow = false;
          break;
        }
      }
    }

    // (3) Latch + temporisation (exactement ton mgUpdateLED)
    if (pin != croco_lastPin) {
      croco_latchedLow = false;
      crocoLowSince = 0;
      croco_lastPin = pin;
    }

    if (sampleLow) {
      if (!croco_latchedLow) {
        croco_latchedLow = true;
        crocoLowSince = now;
      }
    } else {
      croco_latchedLow = false;
      crocoLowSince = 0;
    }

    crocoOK = (croco_latchedLow && (now - crocoLowSince >= CROCO_HOLD_MS));
  }

  // ----- SWITCHES -----
  {
    bool s0 = readSwitchPin(SW_PINS[0]);
    bool s1 = readSwitchPin(SW_PINS[1]);
    bool s2 = readSwitchPin(SW_PINS[2]);
    byte cur = (s2 << 2) | (s1 << 1) | (s0 << 0);

    if (cur == swTarget) {
      if (!swInMatch) {
        swInMatch = true;
        swMatchSince = now;
      }
      if (now - swMatchSince >= 50) swOK = true;
    } else {
      swInMatch = false;
      swOK = false;
    }
  }

  // ----- POTS -----
  {
    int v1 = analogRead(POT1);
    int v2 = analogRead(POT2);
    int sum = v1 + v2;
    int diff = abs(sum - potTarget);

    if (diff < POT_TIGHT) {
      if (!potInRange) {
        potInRange = true;
        potGoodSince = now;
      }
      if (now - potGoodSince >= 50) {
        potsOK = true;
        indicators.setPixelColor(2, 0, 255, 0);
      }
    } else if (diff < POT_NEAR) {
      potInRange = false;  // proche mais pas encore validable
      potsOK = false;
      indicators.setPixelColor(2, 255, 60, 0);
    } else {
      potInRange = false;
      potsOK = false;
      indicators.setPixelColor(2, 255, 0, 0);
    }
  }

  // ----- Agrégation -----
  bool ok = (crocoOK && swOK && potsOK);
  ReglageOK = ok;                         // vérité unique
  // digitalWrite(LED_OK, ok ? HIGH : LOW);  // feedback matériel simple
}


// void mgPickNewTarget() {
//   if (MG_N <= 0) return;
//   int newIdx;
//   do { newIdx = random(0, MG_N); } while (MG_N > 1 && newIdx == mgTargetIdx);
//   mgTargetIdx = newIdx;
//   mgHasTarget = true;
//   Serial.printf("[MG] Nouvelle cible: GPIO %d\n", MG_PORTS[mgTargetIdx]);
//   JustOnce2 = false;
// }

// void mgUpdateLED() {
//   if (!mgHasTarget) {
//     digitalWrite(LED_OK, LOW);
//     // NE PAS forcer ReglageOK ici
//     return;
//   }

//   const int pin = MG_PORTS[mgTargetIdx];

//   // (1) Réaffirme l’état du GPIO à chaque passage (blindage pull-up)
//   pinMode(pin, INPUT_PULLUP);

//   // (2) Anti-glitch temporel: LOW doit tenir ≥ 50 ms pour être "OK"
//   static int lastPin = -1;
//   static bool latchedLow = false;
//   static unsigned long lowSince = 0;

//   if (pin != lastPin) {
//     // reset si on change de cible
//     latchedLow = false;
//     lowSince = 0;
//     lastPin = pin;
//   }

//   bool sampleLow = (digitalRead(pin) == LOW);

//   // (2b) Option: triple sample ultracourt pour lisser les spikes (total ~1 ms)
//   if (sampleLow) {
//     for (int i = 0; i < 3; ++i) {
//       delayMicroseconds(300);
//       if (digitalRead(pin) != LOW) {
//         sampleLow = false;
//         break;
//       }
//     }
//   }

//   unsigned long now = millis();
//   if (sampleLow) {
//     if (!latchedLow) {
//       latchedLow = true;
//       lowSince = now;
//     }
//   } else {
//     latchedLow = false;
//     lowSince = 0;
//   }

//   bool okStable = (latchedLow && (now - lowSince >= 50));  // seuil 50 ms (ajuste 30–100 ms si besoin)

//   ReglageOK = okStable;
//   digitalWrite(LED_OK, ReglageOK ? HIGH : LOW);
// }


// Hystérésis d'affichage pour ReglageOK (évite les flips 1–2 frames lors des tours)
static bool ReglageOK_vis = true;
static unsigned long reglage_last_flip = 0;
static const unsigned long REGLAGE_VIS_HOLDOFF_MS = 300;  // ajuste 200–400ms selon ton goût



// ---------- COUNTING FX STATE ----------
struct CountFX {
  float pctSmooth = 0.0f;
  int head = 0;
  unsigned long tComet = 0;
  unsigned long tSpark = 0;
} fx1, fx2;

inline uint32_t HSV(uint16_t h, uint8_t s, uint8_t v) {
  // ColorHSV = 0..65535 hue
  return ring.gamma32(ring.ColorHSV(h, s, v));
}

static inline float mixf(float a, float b, float t) {
  return a + (b - a) * t;
}

inline float clamp01(float x) {
  if (x < 0) return 0;
  if (x > 1) return 1;
  return x;
}

inline uint32_t IC_RED() {
  return indicators.gamma32(indicators.Color(255, 0, 0));
}
inline uint32_t IC_GREEN() {
  return indicators.gamma32(indicators.Color(0, 255, 0));
}

void drawMiniGameIndicators() {
  indicators.setPixelColor(0, crocoOK ? IC_GREEN() : IC_RED());
  indicators.setPixelColor(1, swOK ? IC_GREEN() : IC_RED());
  // indicators.setPixelColor(2, potsOK ? IC_GREEN() : IC_RED());
  indicators.show();
}



//===================================================SETUP INITIALISATION========================================================


// class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
//   void onResult(BLEAdvertisedDevice advertisedDevice) override {
//     if (advertisedDevice.haveName()) {
//       String devName = String(advertisedDevice.getName().c_str());
//       if (devName.startsWith("Player")) {
//         int rssi = advertisedDevice.getRSSI();

//         // ManufacturerData -> String propre (évite l'erreur de conversion)
//         String manufacturerData;
//         if (advertisedDevice.haveManufacturerData()) {
//           manufacturerData = String(advertisedDevice.getManufacturerData().c_str());
//         }
//         // Parse "P:..." et "E:..."
//         String key = devName;  // ex: "Player3" (si tu préfères, lis P: depuis manufacturerData)
//         int spotted = 0;
//         if (manufacturerData.length()) {
//           int pIdx = manufacturerData.indexOf("P:");
//           int eIdx = manufacturerData.indexOf("E:");
//           if (eIdx != -1) {
//             String eStr = manufacturerData.substring(eIdx + 2);
//             spotted = eStr.toInt();  // 0/1
//           }
//           // Si tu veux forcer la clé par "P:", dé-commente :
//           // if (pIdx != -1 && eIdx != -1) key = manufacturerData.substring(pIdx + 2, eIdx - 1);
//         }

//         // Upsert dans le cache
//         int slot = findSlotByKey(key);
//         if (slot < 0) slot = findFreeOrOldestSlot();
//         players[slot].key = key;
//         players[slot].rssi = rssi;
//         players[slot].spotted = spotted;
//         players[slot].lastSeen = millis();

//         // Debug utile
//         Serial.printf("[BLE] %s  RSSI=%d  E:%d  -> slot %d\n",
//                       key.c_str(), rssi, spotted, slot);
//       }
//     }
//   }
// };

class MyScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    // Serial.printf("Advertised Device Result: %s \n", advertisedDevice->toString().c_str());
    if (advertisedDevice->haveName()) {
      String devName = String(advertisedDevice->getName().c_str());
      if (devName.startsWith("Player")) {
        int rssi = advertisedDevice->getRSSI();

        String manufacturerData;
        if (advertisedDevice->haveManufacturerData()) {
          std::string md = advertisedDevice->getManufacturerData();
          manufacturerData = String(md.c_str());
        }

        String key = devName;  // ex: "Player3"
        int spotted = 0;
        if (manufacturerData.length()) {
          int eIdx = manufacturerData.indexOf("E:");
          if (eIdx != -1) {
            String eStr = manufacturerData.substring(eIdx + 2);
            spotted = eStr.toInt();  // 0/1
          }
        }

        int slot = findSlotByKey(key);
        if (slot < 0) slot = findFreeOrOldestSlot();
        players[slot].key = key;
        players[slot].rssi = rssi;
        players[slot].spotted = spotted;
        players[slot].lastSeen = millis();

        Serial.printf("[BLE] %s  RSSI=%d  E:%d  -> slot %d\n",
                      key.c_str(), rssi, spotted, slot);

        if (spotted == 1 && rssi > -100) {
          ShockTime = millis();
        }
      }
    }
  }
  void onScanEnd(NimBLEScanResults results) {
    // rien (certaines versions requièrent l'implémentation)
  }
} scanCallbacks;



// --- SETUP ---
void setup() {
  delay(2000);
  Serial.begin(115200);
  delay(2000);
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(2000);

  Serial.println("Initialisation du DFPlayer Mini...");
  if (!myDFPlayer.begin(Serial2)) {
    Serial.println("❌ Erreur : Impossible de communiquer avec le DFPlayer Mini !");
    // while (true)
    //   ;  // Bloquer en cas d'échec
  }
  Serial.println("✅ DFPlayer Mini initialisé avec succès.");

  // Vérification de la carte SD
  delay(1000);
  myDFPlayer.volume(30);
  // int sdStatus = myDFPlayer.readType();
  // if (sdStatus != DFPlayerCardInserted) {
  //   Serial.println("⚠️ Aucune carte SD détectée ! Vérifiez la carte SD.");
  // }

  // Définition des capteurs en entrée avec pull-up
  pinMode(MAG_SENSOR_1, INPUT_PULLUP);
  pinMode(MAG_SENSOR_2, INPUT_PULLUP);

  // Attacher les interruptions
  // attachInterrupt(digitalPinToInterrupt(MAG_SENSOR_1), onMagnet1Detected, RISING); // Détection de 1 → 0
  // attachInterrupt(digitalPinToInterrupt(MAG_SENSOR_2), onMagnet2Detected, RISING);

  pinMode(GENERATOR_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_BUTTON_PIN, OUTPUT);
  pinMode(LED_DECORATIVE_PIN, OUTPUT);
  // 2  pinMode(LED_DECORATIVE_PIN2, OUTPUT);
  // pinMode(LED_DECORATIVE_PIN3, OUTPUT);
  // pinMode(LED_DECORATIVE_PIN4, OUTPUT);
  digitalWrite(LED_BUTTON_PIN, HIGH);
  analogWrite(LED_DECORATIVE_PIN, 0);
  // analogWrite(LED_DECORATIVE_PIN2, 0);
  // analogWrite(LED_DECORATIVE_PIN3, 0);
  // analogWrite(LED_DECORATIVE_PIN4, 0);

  // --- MINI-JEUX I/O ---
  // pinMode(LED_OK, OUTPUT);
  // digitalWrite(LED_OK, LOW);
  for (int i = 0; i < CROCO_N; ++i) pinMode(CROCO_PINS[i], INPUT_PULLUP);
  for (int i = 0; i < SW_N; ++i) pinMode(SW_PINS[i], INPUT_PULLUP);
  pinMode(POT1, INPUT);
  pinMode(POT2, INPUT);

  // cibles initiales (une par jeu)
  initAllTargets();  // (code ci-dessous)

  indicators.begin();
  indicators.show();


  ring.begin();
  ring.show();  // Initialisation (tout éteint)

  // Allume toutes les LEDs en rouge au départ
  for (int i = 0; i < NUM_LEDS; i++) {
    ring.setPixelColor(i, ring.Color(255, 0, 0));
  }
  ring.show();

  // BLEDevice::init("");
  // pBLEScan = BLEDevice::getScan();
  // pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  // pBLEScan->setActiveScan(true);  // Scan actif pour plus de données
  // pBLEScan->setInterval(100);      // Réduit le temps entre scans (en ms)
  // pBLEScan->setWindow(30);        // Augmente le temps d'écoute par canal (en ms)


  // --- NimBLE init ---
  NimBLEDevice::init("");
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(&scanCallbacks);
  pBLEScan->setActiveScan(true);  // Scan actif pour plus de données
  pBLEScan->setInterval(100);     // ms
  pBLEScan->setWindow(25);        // ms (<= interval)
  // pBLEScan->setDuplicateFilter(true);


  // Ajout d'une pause pour éviter les bugs d'initialisation
  delay(2000);

  // // Test de lecture audio
  // Serial.println("🎵 Test de  lecture audio (track 1)");
  // playLoopedTrack(1, 20);
  audioQ = xQueueCreate(16, sizeof(AudioCmd));

  xTaskCreatePinnedToCore(mqttTask, "MQTT Task", 8192, NULL, 2, &mqttTaskHandle, 0);
  xTaskCreatePinnedToCore(generatorTask, "Generator Task", 8192, NULL, 4, &gameTaskHandle, 1);
  xTaskCreatePinnedToCore(bleScanTask, "BLEScanTask", 4096, NULL, 1, &bleScanTaskHandle, 0);
  xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 3, &audioTaskHandle, 1);
}

//========================================GAME LOGIC=====================================================

// --- FONCTIONS AUDIO ---
// void playTrackOnce(int track, int volume) {
//   Serial.printf("Lecture de la piste %d avec volume %d\n", track, volume);
//   // vTaskDelay(300 / portTICK_PERIOD_MS);
//   // myDFPlayer.volume(volume);
//   // vTaskDelay(300 / portTICK_PERIOD_MS);
//   myDFPlayer.play(track);
//   vTaskDelay(200 / portTICK_PERIOD_MS);
// }

// void playLoopedTrack(int track, int volume) {
//   if (!isLooped) {
//     Serial.printf("Lecture en boucle de la piste %d avec volume %d\n", track, volume);
//     // vTaskDelay(300 / portTICK_PERIOD_MS);
//     // myDFPlayer.volume(volume);
//     // vTaskDelay(300 / portTICK_PERIOD_MS);
//     myDFPlayer.loop(track);
//     vTaskDelay(200 / portTICK_PERIOD_MS);
//     isLooped = true;
//   }
// }

// void stopLoopTrack() {
//   Serial.println("Arrêt de la boucle.");
//   // vTaskDelay(300 / portTICK_PERIOD_MS);
//   myDFPlayer.stop();
//   vTaskDelay(200 / portTICK_PERIOD_MS);
//   isLooped = false;
// }

// void audioTask(void*) {
//   // Init matérielle déjà faite dans setup() -> myDFPlayer.begin(Serial2), volume()
//   const TickType_t interCmdDelay = pdMS_TO_TICKS(250);  // gap entre trames
//   AudioCmd cmd;

//   // pinMode(DF_BUSY_PIN, INPUT); // si utilisé

//   for (;;) {
//     if (xQueueReceive(audioQ, &cmd, portMAX_DELAY) == pdTRUE) {
//       switch (cmd.type) {
//         case AUD_PLAY:
//           myDFPlayer.play(cmd.track);
//           vTaskDelay(interCmdDelay);
//           break;

//         case AUD_LOOP:
//           if (!isLooped) {
//             myDFPlayer.loop(cmd.track);
//             isLooped = true;
//             vTaskDelay(interCmdDelay);
//           }
//           break;

//         case AUD_STOP:
//           myDFPlayer.stop();
//           isLooped = false;
//           vTaskDelay(interCmdDelay);
//           break;

//         case AUD_VOL:
//           myDFPlayer.volume(constrain(cmd.volume, 0, 30));
//           vTaskDelay(interCmdDelay);
//           break;
//       }
//     }
//   }
// }

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





// --- TÂCHE GENERATOR (CORE 1) ---
void generatorTask(void* parameter) {
  Serial.println("Generator Task démarré.");
  stateStartTime = millis();

  while (true) {
    // Serial.println(String(genState));
    // Serial.print("Magnet 1 :");
    // Serial.println(digitalRead(27));
    // Serial.print("Magnet 2 :");
    // Serial.println(digitalRead(26));

    if (anyThreat(-80) && !Shocked) {  // Vérifie que le joueur est détecté ET à moins de 5m
      previousGenState = genState;     // Sauvegarde l’état actuel avant le changement
      genState = SHOCK;
      Killerholding = true;
      ShockTime = millis();
      Serial.println("⚡ Choc déclenché ! Joueur trop proche !");
      Shocked = true;
      // notifyMQTT ("Player" + playerNameSpotted + ":SABOTAGE");
      // stopLoopTrack();
      audioStop();
      audioLoop(8);
    }

    // Après 3 secondes, retour à l’état précédent
    if (genState == SHOCK && millis() - ShockTime >= 10000 && Shocked) {
      audioStop();
      JustOnce = false;
      genState = previousGenState;
      Serial.println("↩ Retour à l’état précédent !");
      Shocked = false;
      Killerholding = false;
      // stopLoopTrack();
    }

    // if (digitalRead(GENERATOR_BUTTON_PIN) == LOW && !Killerholding) {
    //   // stopLoopTrack();
    //   audioStop();
    //   previousGenState = genState;
    //   genState = KILLERHOLDING;
    //   Shocked = true;
    //   Killerholding = true;
    //   KillerTime = millis();
    //   KillerTurn = 0;
    //   Serial.println("KillerHolding Activated");
    //   // playLoopedTrack(7, 30);
    //   audioLoop(7);
    // }

    //Serial.println(digitalRead(27));
    switch (genState) {
      case COUNTING:

        // if (turnCount == 0 && !mgHasTarget) {
        //   pickNewTarget();  // choisit une broche
        //   JustOnce = false;   // déverrouille les cues audio liés au réglage
        // }
        NbTrue = (crocoOK ? 1 : 0) + (swOK ? 1 : 0) + (potsOK ? 1 : 0);
        RegFalse = 3 - NbTrue;
        drawMiniGameIndicators();
        updateAllMiniGames();
        if (onFall_ReglageOK(ReglageOK)) {
          audioStop();
          audioPlay(11);
          audioLoop(14);
        }
        if (turnCount == 0 && ReglageOK && !JustOnce) {
          audioLoop(1);
          JustOnce = true;
        }
        // if (!ReglageOK && !JustOnce) {
        //   audioStop();
        //   audioPlay(11);
        //   // audioLoop(); piste réparation nécessaire
        //   JustOnce = true;
        // }
        if (onRise_ReglageOK(ReglageOK) && turnCount > 0) {
          audioStop();
          audioLoop(10);
          // JustOnce = true;
        }

        if (!ReglageOK && millis() - Decharge > TimeDecharge) {
          Decharge = millis();
          if (turnCount > 0) turnCount -= 1;
        }

        colorSecondRed();
        digitalWrite(LED_BUTTON_PIN, LOW);
        analogWrite(LED_DECORATIVE_PIN, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
        // analogWrite(LED_DECORATIVE_PIN2, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
        // analogWrite(LED_DECORATIVE_PIN3, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
        // analogWrite(LED_DECORATIVE_PIN4, map(turnCount, 0, NbrTour + diffLvl, 0, 255));

        updateProgressRing(turnCount, NbrTour + diffLvl, 0);  // 🎯 Affichage LED progressif

        // 🔹 Détection du capteur secondaire (doit être activé avant le principal)
        if (digitalRead(MAG_SENSOR_1) == 0 && !capteur2Passe) {  // Anti-rebond
          capteur2Passe = true;
          Serial.println("🔸 Capteur secondaire activé !");
        }

        if (capteur2Passe && digitalRead(MAG_SENSOR_2) == 0) {  // Vérifie si capteur 2 a été activé avant
          if (ReglageOK) {
            turnCount++;
            audioStop();
            audioPlay(2);  // piste tour
            audioLoop(10);
          }
          if (!ReglageOK) {
            int randomTurn = random(0, 5);
            if (randomTurn > RegFalse) {
              turnCount++;
              audioStop();
              audioPlay(2);
              audioLoop(14);
            } else {
              audioStop();
              audioPlay(13);
              audioLoop(14);
            }
          }
          Serial.printf("✅ Tour complet détecté ! Total : %d\n", turnCount);
          capteur2Passe = false;  // Réinitialisation
        }

        if (turnCount >= lastAnnouncedTurn + 1 && turnCount < NbrTour + diffLvl) {
          lastAnnouncedTurn = turnCount;
          if (turnCount < 2) {
            // audioStop();
            pickNewTarget();
            updateAllMiniGames();
            JustOnce = false;
          }
          // stopLoopTrack();
          int randomPick = random(0, 4);
          if (randomPick <= RegFalse) {
            pickNewTarget();
            updateAllMiniGames();
            JustOnce = false;
          }
        }

        if (turnCount >= NbrTour + diffLvl) {
          audioStop();
          JustOnce = false;
          genState = WAITING;
          notifyMQTT("generateur lvl 2");
          stateStartTime = millis();
          // stopLoopTrack();
          // audioPlay();  piste phase 1 réussi
          updateProgressRing(turnCount, NbrTour + diffLvl, 0);  // 🎯 Affichage LED progressif
          Serial.println("✅ Niveau 1 validé, passage en WAITING.");
        }
        break;

      case WAITING:
        {
          colorFirstHalfGreen(0);
          Killerholding = true;
          Shocked = true;
          // playLoopedTrack(9, 30);
          if (!JustOnce) {
            // audioStop();
            audioLoop(9);
            JustOnce = true;
          }

          digitalWrite(LED_BUTTON_PIN, LOW);
          analogWrite(LED_DECORATIVE_PIN, 255);
          // analogWrite(LED_DECORATIVE_PIN2, 255);
          // analogWrite(LED_DECORATIVE_PIN3, 255);
          // analogWrite(LED_DECORATIVE_PIN4, 255);

          unsigned long durationMs = (unsigned long)(20000 * diffLvl);  // même durée que votre timer
          unsigned long elapsed = millis() - stateStartTime;
          animateWaitingSimple(stateStartTime, durationMs);

          // if (JustOnce == false) {
          //   playTrackOnce(3, 30);
          //   JustOnce = true;
          // }

          if (elapsed >= durationMs) {
            genState = COUNTING2;
            // stopLoopTrack();
            ReglageOK = true;
            // mgHasTarget = false;
            audioStop();
            JustOnce = false;
            lastTurnDetectedTime = millis();  // Mise à jour de l'heure du dernier tour détecté
            turnCount = 0;
            lastAnnouncedTurn = 0;
            notifyMQTT("generateur lvl 3");
            Killerholding = false;
            Shocked = false;
            Serial.println("⏳ 3 minutes écoulées, passage en BUTTON_PHASE");
          }
          break;
        }

      case BUTTON_PHASE:
        if (digitalRead(GENERATOR_BUTTON_PIN) == LOW) {
          if (buttonHoldStartTime == 0) {
            buttonHoldStartTime = millis();
          } else if (millis() - buttonHoldStartTime >= 5000 + (diffLvl * 1000)) {
            genState = COUNTING2;
            lastTurnDetectedTime = millis();  // Mise à jour de l'heure du dernier tour détecté
            turnCount = 0;
            lastAnnouncedTurn = 0;
            Serial.println("✅ Bouton maintenu 5 sec.");
            // stopLoopTrack();
            audioStop();
          }
          digitalWrite(LED_BUTTON_PIN, LOW);
        } else {
          buttonHoldStartTime = 0;
          digitalWrite(LED_BUTTON_PIN, (millis() / 500) % 2);
          // playLoopedTrack(4, 30);
          audioLoop(4);
        }
        analogWrite(LED_DECORATIVE_PIN, random(0, 255));
        // analogWrite(LED_DECORATIVE_PIN2, random(0, 255));
        // analogWrite(LED_DECORATIVE_PIN3, random(0, 255));
        // analogWrite(LED_DECORATIVE_PIN4, random(0, 255));
        break;

      case COUNTING2:
        {
          colorFirstHalfGreen(0);
          NbTrue = (crocoOK ? 1 : 0) + (swOK ? 1 : 0) + (potsOK ? 1 : 0);
          RegFalse = 3 - NbTrue;
          // if (turnCount == 0 && !mgHasTarget) {
          //   pickNewTarget();  // choisit une broche
          //   JustOnce = false;   // déverrouille les cues audio liés au réglage
          // }
          drawMiniGameIndicators();
          updateAllMiniGames();
          if (onFall_ReglageOK(ReglageOK)) {
            audioStop();
            audioPlay(11);
            audioLoop(14);
          }
          if (ReglageOK && !JustOnce) {
            audioLoop(10);
            JustOnce = true;
          }
          // if (!ReglageOK && !JustOnce) {
          //   audioStop();
          //   audioPlay(11);
          //   // audioLoop(); piste réparation nécessaire
          //   JustOnce = true;
          // }
          if (onRise_ReglageOK(ReglageOK) && turnCount > 0) {
            audioStop();
            audioLoop(10);
            // JustOnce = true;
          }


          if (!ReglageOK && millis() - Decharge > TimeDecharge) {
            Decharge = millis();
            if (turnCount > 0) turnCount -= 1;
          }

          digitalWrite(LED_BUTTON_PIN, LOW);
          analogWrite(LED_DECORATIVE_PIN, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
          // analogWrite(LED_DECORATIVE_PIN2, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
          // analogWrite(LED_DECORATIVE_PIN3, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
          // analogWrite(LED_DECORATIVE_PIN4, map(turnCount, 0, NbrTour + diffLvl, 0, 255));

          updateProgressRing(turnCount, (NbrTour) + diffLvl, 1);   // 🎯 Affichage LED progressif
                                                                   // 🔹 Détection du capteur secondaire (doit être activé avant le principal)
          if (digitalRead(MAG_SENSOR_1) == 0 && !capteur2Passe) {  // Anti-rebond
            capteur2Passe = true;
            Serial.println("🔸 Capteur secondaire activé !");
          }

          if (capteur2Passe && digitalRead(MAG_SENSOR_2) == 0) {  // Vérifie si capteur 2 a été activé avant
            if (ReglageOK) {
              turnCount++;
              audioStop();
              audioPlay(2);  // piste tour
              audioLoop(10);
            }
            if (!ReglageOK) {
              int randomTurn = random(0, 5);
              if (randomTurn > RegFalse) {
                turnCount++;
                audioStop();
                audioPlay(2);
                audioLoop(14);
              } else {
                audioStop();
                audioPlay(13);
                audioLoop(14);
              }
            }
            Serial.printf("✅ Tour complet détecté ! Total : %d\n", turnCount);
            capteur2Passe = false;  // Réinitialisation
          }

          if (turnCount >= lastAnnouncedTurn + 1 && turnCount < NbrTour + diffLvl) {
            lastAnnouncedTurn = turnCount;
            if (turnCount < 2) {
              // audioStop();
              pickNewTarget();
              updateAllMiniGames();
              JustOnce = false;
            }
            // stopLoopTrack();
            int randomPick = random(0, 4);
            if (randomPick <= RegFalse) {
              pickNewTarget();
              updateAllMiniGames();
              JustOnce = false;
            }
          }

          if (turnCount >= (NbrTour) + diffLvl) {
            genState = WAITP3;
            notifyMQTT("generateur reparer");
            stateStartTime = millis();
            updateProgressRing(turnCount, (NbrTour) + diffLvl, 1);
            // stopLoopTrack();
            audioStop();
            JustOnce = false;
            Serial.println("✅ Niveau 1 validé, passage en WAITING.");
          }

          // if (millis() - lastTurnDetectedTime >= 300000 - (diffLvl * 20000)) {  // 300000 ms = 5 minutes
          //   Serial.println("⚠️ Aucune activité détectée pendant 5 minutes. Retour en BUTTON_PHASE.");
          //   genState = BUTTON_PHASE;
          //   notifyMQTT("generateur lvl 3");  // Mise à jour MQTT
          // }
          break;
        }

      case WAITP3:
        {
          Killerholding = true;
          Shocked = true;
          // playLoopedTrack(9, 30);
          if (!JustOnce) {
            audioLoop(9);
            JustOnce = true;
          }
          colorFirstHalfGreen(1);
          digitalWrite(LED_BUTTON_PIN, LOW);
          analogWrite(LED_DECORATIVE_PIN, 255);
          // analogWrite(LED_DECORATIVE_PIN2, 255);
          // analogWrite(LED_DECORATIVE_PIN3, 255);
          // analogWrite(LED_DECORATIVE_PIN4, 255);

          unsigned long durationMs = (unsigned long)(30000 * diffLvl);  // même durée que votre timer
          unsigned long elapsed = millis() - stateStartTime;
          // animateWaitingSimple(stateStartTime, durationMs);

          // if (JustOnce == false) {
          //   playTrackOnce(3, 30);
          //   JustOnce = true;
          // }

          if (elapsed >= durationMs) {
            genState = PHASE3;
            // stopLoopTrack();
            audioStop();
            lastTurnDetectedTime = millis();  // Mise à jour de l'heure du dernier tour détecté
            turnCount = 0;
            lastAnnouncedTurn = 0;
            notifyMQTT("generateur lvl 3");
            Killerholding = false;
            Shocked = false;
            JustOnce = false;
            Serial.println("⏳ 3 minutes écoulées, passage en BUTTON_PHASE");
          }
          break;
        }

      case PHASE3:
        {

          bool buttonPressed = (digitalRead(GENERATOR_BUTTON_PIN) == LOW);  // LOW = appuyé

          // Détection du début d'appui
          if (buttonPressed && !buttonWasPressed) {
            buttonPressStart = millis();
            buttonWasPressed = true;
            analogWrite(LED_BUTTON_PIN, 255);
            audioStop();
            audioLoop(12);
          }

          // Si le bouton reste appuyé
          if (buttonPressed) {
            if (millis() - buttonPressStart >= LONG_PRESS_TIME) {
              audioStop();
              JustOnce = false;
              genState = FINISHED;
            }
          }

          // Quand on relâche le bouton
          if (!buttonPressed && buttonWasPressed) {
            buttonWasPressed = false;
            JustOnce = false;
            audioStop();
          }

          if (!buttonPressed) {
            if (!JustOnce) {
              audioLoop(4);
              JustOnce = true;
            }
            analogWrite(LED_DECORATIVE_PIN, 255);
            // analogWrite(LED_DECORATIVE_PIN2, 255);
            // analogWrite(LED_DECORATIVE_PIN3, 255);
            // analogWrite(LED_DECORATIVE_PIN4, 255);

            unsigned long currentMillis = millis();

            // avance la phase sans bloquer
            tLED += 0.002 * (currentMillis - previousMillis);
            previousMillis = currentMillis;

            // courbe de respiration en sinus (0 à 1)
            float brightness = (sin(tLED) + 1.0) / 2.0;
            int duty = (int)(brightness * 255);  // convertit en 0–255

            // applique la luminosité
            analogWrite(LED_BUTTON_PIN, duty);

            // si on dépasse un cycle complet, on remet t à 0
            if (tLED > 2 * PI) tLED -= 2 * PI;
          }
        }
        break;


      case FINISHED:
        Killerholding = true;
        Shocked = true;
        digitalWrite(LED_BUTTON_PIN, HIGH);
        analogWrite(LED_DECORATIVE_PIN, 255);
        // analogWrite(LED_DECORATIVE_PIN2, 255);
        // analogWrite(LED_DECORATIVE_PIN3, 255);
        // analogWrite(LED_DECORATIVE_PIN4, 255);
        // playLoopedTrack(5, 30);
        if (!JustOnce) {
          audioStop();
          audioLoop(5);
          JustOnce = true;
        }
        break;

      case SHOCK:
        // playLoopedTrack(8, 30);
        // audioLoop(8);
        animateShockRing();
        digitalWrite((LED_BUTTON_PIN), random(0, 2));
        analogWrite(LED_DECORATIVE_PIN, random(0, 255));
        // analogWrite(LED_DECORATIVE_PIN2, random(0, 255));
        // analogWrite(LED_DECORATIVE_PIN3, random(0, 255));
        // analogWrite(LED_DECORATIVE_PIN4, random(0, 255));
        break;

      case OUT:
        Killerholding = true;
        Shocked = true;
        audioStop();
        digitalWrite((LED_BUTTON_PIN), 0);
        analogWrite(LED_DECORATIVE_PIN, 0);
        // analogWrite(LED_DECORATIVE_PIN2, 0);
        // analogWrite(LED_DECORATIVE_PIN3, 0);
        // analogWrite(LED_DECORATIVE_PIN4, 0);
        break;

      case KILLERHOLDING:

        animateKillerHoldingRing(KillerTurn, diffLvl);
        // 🔹 Détection du capteur secondaire (doit être activé avant le principal)
        if (digitalRead(MAG_SENSOR_1) == 0 && !capteur2Passe) {  // Anti-rebond
          capteur2Passe = true;
          Serial.println("🔸 Capteur secondaire activé !");
        }

        if (capteur2Passe && digitalRead(MAG_SENSOR_2) == 0) {  // Vérifie si capteur 2 a été activé avant
          KillerTurn++;
          Serial.printf("✅ Tour complet détecté ! Total : %d\n", turnCount);
          capteur2Passe = false;  // Réinitialisation
        }

        if (KillerTurn >= diffLvl || millis() - KillerTime > 30000) {
          genState = previousGenState;
          KillerTurn = 0;
          Killerholding = false;
          Shocked = false;
          // stopLoopTrack();
          audioStop();
          updateProgressRing(0, (NbrTour) + diffLvl, 0);
          updateProgressRing(0, (NbrTour) + diffLvl, 1);
        }

        break;

      case INITIATE:
        colorAllRed();
        pickNewTarget();
        digitalWrite((LED_BUTTON_PIN), 0);
        analogWrite(LED_DECORATIVE_PIN, 0);
        // analogWrite(LED_DECORATIVE_PIN2, 0);
        // analogWrite(LED_DECORATIVE_PIN3, 0);
        // analogWrite(LED_DECORATIVE_PIN4, 0);
        Shocked = false;
        Killerholding = false;
        turnCount = 0;
        lastAnnouncedTurn = 0;
        JustOnce = false;
        isLooped = false;
        KillerTurn = 0;
        ReglageOK = true;
        // stopLoopTrack();
        audioStop();
        vTaskDelay(300 / portTICK_PERIOD_MS);
        // playLoopedTrack(1, 30);
        audioLoop(1);
        // vTaskDelay(2000 / portTICK_PERIOD_MS);
        notifyMQTT("generateur lvl 1");
        genState = COUNTING;
        break;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// void updateProgressRing(int turnCount, int totalTours) {
//   int ledsToLight = map(turnCount, 0, totalTours, 0, 12);
//   ledsToLight = constrain(ledsToLight, 0, 12);
//   int blinkingIndex = ledsToLight;  // La LED en cours de progression

//   // Mise à jour clignotement
//   if (millis() - lastBlinkTime >= blinkInterval) {
//     blinkState = !blinkState;
//     lastBlinkTime = millis();
//   }

//   for (int i = 0; i < 12; i++) {
//     if (i < ledsToLight) {
//       ring.setPixelColor(i, ring.Color(0, 255, 0));  // Vert : complété
//     } else if (i == blinkingIndex && blinkState && ledsToLight < 12) {
//       ring.setPixelColor(i, ring.Color(255, 255, 0));  // Jaune clignotant : en cours
//     } else {
//       ring.setPixelColor(i, ring.Color(255, 0, 0));  // Rouge : restant
//     }
//   }

//   ring.show();
// }

// --- COUNTING: wrapper drop-in (garde TON appel existant) ---
void updateProgressRing(int turnCount, int totalTours, int Anneau) {
  unsigned long now = millis();
  bool target = ReglageOK;

  // Si l'état demandé (target) diffère de l'état affiché, on n'accepte le changement
  // que s'il reste stable au moins REGLAGE_VIS_HOLDOFF_MS.
  if (target != ReglageOK_vis) {
    if (now - reglage_last_flip >= REGLAGE_VIS_HOLDOFF_MS) {
      ReglageOK_vis = target;
      reglage_last_flip = now;
    }
    // Sinon: on garde l'ancien état visuel (latch)
  } else {
    // État stable: on met juste à jour le timestamp de référence
    reglage_last_flip = now;
  }

  if (ReglageOK_vis) updateProgressRing_OK(turnCount, totalTours, Anneau);
  else updateProgressRing_Fault(turnCount, totalTours, Anneau);
}


// --- Animation "OK" : fond rouge, progression verte, liseré jaune, comète ambre ---
void updateProgressRing_OK(int turnCount, int totalTours, int Anneau) {
  const int start = 12 * Anneau, span = 12, end = start + span;
  const unsigned long now = millis();
  totalTours = max(totalTours, 1);

  float targetPct = clamp01((float)turnCount / (float)totalTours);
  fx1.pctSmooth = mixf(fx1.pctSmooth, targetPct, 0.18f);

  if (now - fx1.tComet > 55) {
    fx1.tComet = now;
    fx1.head = (fx1.head + 1) % span;
  }

  float breathe = 0.60f + 0.40f * sinf(now * 0.006f);
  int filled = (int)floorf(fx1.pctSmooth * span);

  // Fond rouge franc
  for (int i = start; i < end; i++) {
    ring.setPixelColor(i, ring.gamma32(ring.Color(160, 0, 0)));
  }

  // Progression verte (teinte fixe, intensité “breathe”)
  for (int i = start; i < start + filled && i < end; i++) {
    uint8_t v = (uint8_t)min(255.0f, 90.0f + 120.0f * breathe);
    ring.setPixelColor(i, ring.gamma32(ring.Color(0, v, 0)));
  }

  // Liseré jaune
  if (filled < span) {
    int edge = start + filled;
    uint8_t vEdge = (uint8_t)(200 + 55 * sinf(now * 0.02f));
    ring.setPixelColor(edge, HSV(9000, 220, vEdge));  // ~jaune orangé
  }

  // Comète ambre (3 px) sur la zone restante
  for (int k = 0; k < 3; k++) {
    int idx = start + ((fx1.head - k + span) % span);
    if (idx >= start + filled) {
      uint8_t v = (k == 0) ? 180 : (k == 1 ? 120 : 70);
      ring.setPixelColor(idx, HSV(7000, 255, v));
    }
  }

  ring.show();
}

// --- Animation "FAULT" : signal réparation, progression toujours visible ---
// Chevrons ambre sur la zone restante, vert atténué côté rempli, balise blanche
void updateProgressRing_Fault(int turnCount, int totalTours, int Anneau) {
  const int start = 12 * Anneau, span = 12, end = start + span;
  const unsigned long now = millis();
  totalTours = max(totalTours, 1);

  float targetPct = clamp01((float)turnCount / (float)totalTours);
  fx1.pctSmooth = mixf(fx1.pctSmooth, targetPct, 0.18f);

  // on garde la tête qui tourne pour la balise
  if (now - fx1.tComet > 55) {
    fx1.tComet = now;
    fx1.head = (fx1.head + 1) % span;
  }

  float breathe = 0.60f + 0.40f * sinf(now * 0.006f);
  int filled = (int)floorf(fx1.pctSmooth * span);

  // Fond rouge franc
  for (int i = start; i < end; i++) {
    ring.setPixelColor(i, ring.gamma32(ring.Color(160, 0, 0)));
  }

  // Progression VERTE mais un peu atténuée pour signal d’alerte (toujours lisible)
  for (int i = start; i < start + filled && i < end; i++) {
    uint8_t v = (uint8_t)min(255.0f, 70.0f + 90.0f * breathe);  // plus doux que OK
    ring.setPixelColor(i, ring.gamma32(ring.Color(0, v, 0)));
  }

  // Zone restante = chevrons ambre clignotants
  bool phase = ((now / 120) % 2) == 0;  // strobe 120 ms
  for (int i = start + filled; i < end; i++) {
    bool stripe = ((i & 1) == (phase ? 1 : 0));
    if (stripe) ring.setPixelColor(i, ring.gamma32(ring.Color(255, 110, 0)));  // ambre
    else ring.setPixelColor(i, ring.gamma32(ring.Color(110, 0, 0)));           // rouge sombre
  }

  // Balise blanche tournante (attire l’attention même si rien ne reste)
  int beacon = start + ((fx1.head + 3) % span);
  ring.setPixelColor(beacon, ring.gamma32(ring.Color(255, 255, 255)));

  // Pas de liseré ni de comète ici: signal “panne” prioritaire et sans conflit
  ring.show();
}






// void updateProgressRing2(int turnCount, int totalTours) {
//   int ledsToLight = map(turnCount, 0, totalTours, 12, NUM_LEDS);
//   ledsToLight = constrain(ledsToLight, 12, NUM_LEDS);
//   int blinkingIndex = ledsToLight;  // La LED en cours de progression

//   // Mise à jour clignotement
//   if (millis() - lastBlinkTime >= blinkInterval) {
//     blinkState = !blinkState;
//     lastBlinkTime = millis();
//   }

//   for (int i = 12; i < NUM_LEDS; i++) {
//     if (i < ledsToLight) {
//       ring.setPixelColor(i, ring.Color(0, 255, 0));  // Vert : complété
//     } else if (i == blinkingIndex && blinkState && ledsToLight < NUM_LEDS) {
//       ring.setPixelColor(i, ring.Color(255, 255, 0));  // Jaune clignotant : en cours
//     } else {
//       ring.setPixelColor(i, ring.Color(255, 0, 0));  // Rouge : restant
//     }
//   }

//   ring.show();
// }

// --------- COUNTING PHASE 2 : même FX sur deuxième moitié (LEDs 12..23), teinte + froide ---------
void updateProgressRing2(int turnCount, int totalTours) {
  const int start = 12, span = 12, end = start + span;
  const unsigned long now = millis();
  totalTours = max(totalTours, 1);

  // cible en [0..1], lissée pour enlever les à-coups
  float targetPct = clamp01((float)turnCount / (float)totalTours);
  fx1.pctSmooth = mixf(fx1.pctSmooth, targetPct, 0.18f);

  // animation de la comète
  if (now - fx1.tComet > 55) {
    fx1.tComet = now;
    fx1.head = (fx1.head + 1) % span;
  }

  // (SUPPRIMER l’ancienne ligne hueBase… on n’en a plus besoin)

  // glow respirant (garde l'effet vivant)
  float breathe = 0.60f + 0.40f * sinf(now * 0.006f);

  // combien de LEDs “remplies”
  int filled = (int)floorf(fx1.pctSmooth * span);

  // --- FOND : ROUGE FRANC partout (pas de dégradé)
  for (int i = start; i < end; i++) {
    ring.setPixelColor(i, ring.gamma32(ring.Color(160, 0, 0)));  // ajuste 160 si tu veux plus/moins fort
  }

  // --- PROGRESSION : VERT UNI (on ne change que la luminosité via breathe, pas la teinte)
  for (int i = start; i < start + filled && i < end; i++) {
    uint8_t v = (uint8_t)min(255.0f, 90.0f + 120.0f * breathe);  // même dynamique que ton ancien "v"
    ring.setPixelColor(i, ring.gamma32(ring.Color(0, v, 0)));
  }

  // bord de progression: liseré jauni qui “crépite” (inchangé)
  if (filled < span) {
    int edge = start + filled;
    uint8_t vEdge = (uint8_t)(200 + 55 * sinf(now * 0.02f));
    ring.setPixelColor(edge, HSV(9000, 220, vEdge));  // ~jaune orangé
  }

  // comète ambrée (inchangée) sur la zone non remplie
  for (int k = 0; k < 3; k++) {
    int idx = start + ((fx1.head - k + span) % span);
    if (idx >= start + filled) {
      uint8_t v = (k == 0) ? 180 : (k == 1 ? 120 : 70);
      ring.setPixelColor(idx, HSV(7000, 255, v));
    }
  }

  ring.show();
}


// Anim LED pour SHOCK : strobe rouge + étincelles blanches + flash global très bref
void animateShockRing() {
  const unsigned long blinkInt = 80;        // strobe de base
  const unsigned long glitchPeriod = 1000;  // flash global toutes les ~1s
  const unsigned long glitchWidth = 30;     // durée du flash global

  unsigned long now = millis();
  if (now - shockAnim.t >= blinkInt) {
    shockAnim.t = now;
    shockAnim.alt = !shockAnim.alt;
  }
  if (shockAnim.burstT == 0 || now - shockAnim.burstT >= glitchPeriod) shockAnim.burstT = now;

  // motif alterné rouge / off
  for (int i = 0; i < NUM_LEDS; i++) {
    bool on = ((i & 1) == (shockAnim.alt ? 1 : 0));
    ring.setPixelColor(i, on ? RGB(255, 0, 0) : 0);
  }

  // 2-3 étincelles blanches aléatoires
  for (int s = 0; s < 3; s++) {
    int idx = random(NUM_LEDS);
    ring.setPixelColor(idx, RGB(180, 180, 180));
  }

  // flash global ultra court (sensation "glitch")
  if (now - shockAnim.burstT < glitchWidth) {
    for (int i = 0; i < NUM_LEDS; i++) ring.setPixelColor(i, RGB(255, 255, 255));
  }

  ring.show();
}

// Anim LED pour KILLERHOLDING : progression verte + comète ambrée + flash vert sur validation
void animateKillerHoldingRing(int turns, int required) {
  unsigned long now = millis();

  // vitesse de rotation de la comète
  if (now - holdAnim.t >= 45) {
    holdAnim.t = now;
    holdAnim.pos = (holdAnim.pos + 1) % NUM_LEDS;
  }

  // progression "réparation" (verte) selon le nombre de tours requis
  int req = max(required, 1);
  int progPixels = constrain(map(turns, 0, req, 0, NUM_LEDS), 0, NUM_LEDS);

  // fond : progression en vert
  for (int i = 0; i < NUM_LEDS; i++) {
    if (i < progPixels) ring.setPixelColor(i, RGB(0, 200, 60));  // réparé
    else ring.setPixelColor(i, 0);                               // restant
  }

  // comète ambrée (3 pixels) sur la partie non réparée
  for (int k = 0; k < 3; k++) {
    int idx = (holdAnim.pos - k + NUM_LEDS) % NUM_LEDS;
    if (idx >= progPixels) {
      uint8_t r = (k == 0) ? 255 : (k == 1) ? 180
                                            : 110;
      uint8_t g = (k == 0) ? 120 : (k == 1) ? 85
                                            : 50;
      ring.setPixelColor(idx, RGB(r, g, 0));
    }
  }

  // flash vert bref quand un nouveau tour est validé
  if (turns != holdAnim.lastTurn) {
    holdAnim.lastTurn = turns;
    holdAnim.flashStart = now;
  }
  if (holdAnim.flashStart && now - holdAnim.flashStart < 150) {
    for (int i = progPixels; i < NUM_LEDS; i++) ring.setPixelColor(i, RGB(0, 255, 100));
  }

  ring.show();
}

// WAITING simple : décharge orange + spinner de luminosité
void animateWaitingSimple(unsigned long startMs, unsigned long durationMs) {
  const int start = 12;
  const int span = 12;  // LEDs 12..23
  const int end = start + span;

  unsigned long now = millis();
  if (durationMs == 0) durationMs = 1;  // éviter division par 0

  // Progression du temps: 0.0 -> 1.0
  float t = (float)(now - startMs) / (float)durationMs;
  if (t < 0) t = 0;
  if (t > 1) t = 1;

  // Combien restent allumées (orange) ?
  int remaining = span - (int)floorf(t * span);
  if (remaining < 0) remaining = 0;
  if (remaining > span) remaining = span;

  // Spinner: LED plus lumineuse qui tourne parmi les LEDs restantes
  static unsigned long lastSpin = 0;
  static int spin = 0;
  if (now - lastSpin >= 90) {  // vitesse de rotation
    lastSpin = now;
    spin = (spin + 1) % span;
  }

  for (int i = 0; i < span; i++) {
    int idx = start + i;
    if (i < remaining) {
      // Orange normal
      uint8_t r = 255, g = 20, b = 0;
      // LED du spinner plus lumineuse
      if (i == spin) { g = 100; }  // rend l’orange plus vif
      ring.setPixelColor(idx, ring.Color(r, g, b));
    } else {
      // Déchargée (éteinte) — ou très sombre si tu préfères un fond minimum:
      // ring.setPixelColor(idx, ring.Color(3, 2, 0)); // fond ultra-faible
      ring.setPixelColor(idx, 0);
    }
  }

  ring.show();
}


void colorFirstHalfGreen(int value) {
  for (int i = 12 * value; i < 12; i++) {
    ring.setPixelColor(i, ring.Color(0, 255, 0));  // vert
  }
  ring.show();
}

void colorAllRed() {
  for (int i = 0; i < 24; i++) {
    ring.setPixelColor(i, ring.Color(255, 0, 0));  // vert
  }
  ring.show();
}

void colorSecondRed() {
  for (int i = 12; i < 24; i++) {
    ring.setPixelColor(i, ring.Color(255, 0, 0));  // vert
  }
  ring.show();
}

// Applique le changement de difficulté SANS casser le % courant
void applyDifficultyKeepPercent(float newLvl) {
  float oldLvl = diffLvl;
  if (newLvl <= 0) newLvl = 1;  // garde un min

  // --- COUNTING / COUNTING2: garder le % de tours faits ---
  if (genState == COUNTING || genState == COUNTING2) {
    int oldTotal = NbrTour + (int)oldLvl;
    int newTotal = NbrTour + (int)newLvl;
    float pct = (oldTotal > 0) ? (float)turnCount / (float)oldTotal : 0.0f;
    pct = constrain(pct, 0.0f, 1.0f);
    turnCount = (int)floorf(pct * newTotal);
    lastAnnouncedTurn = min(lastAnnouncedTurn, turnCount);
  }

  // --- WAITING / WAITP3: garder le % de temps écoulé ---
  if (genState == WAITING) {
    unsigned long now = millis();
    unsigned long oldDur = (unsigned long)(20000.0f * oldLvl);
    unsigned long newDur = (unsigned long)(20000.0f * newLvl);
    if (oldDur == 0) oldDur = 1;
    float pct = (float)(now - stateStartTime) / (float)oldDur;
    pct = constrain(pct, 0.0f, 1.0f);
    stateStartTime = now - (unsigned long)(pct * (float)newDur);
  }

  if (genState == WAITP3) {
    unsigned long now = millis();
    unsigned long oldDur = (unsigned long)(30000.0f * oldLvl);
    unsigned long newDur = (unsigned long)(30000.0f * newLvl);
    if (oldDur == 0) oldDur = 1;
    float pct = (float)(now - stateStartTime) / (float)oldDur;
    pct = constrain(pct, 0.0f, 1.0f);
    stateStartTime = now - (unsigned long)(pct * (float)newDur);
  }

  // --- KILLERHOLDING: garder le % du seuil atteint ---
  if (genState == KILLERHOLDING) {
    float pct = (oldLvl > 0.0f) ? (float)KillerTurn / oldLvl : 0.0f;
    pct = constrain(pct, 0.0f, 1.0f);
    KillerTurn = (int)floorf(pct * newLvl);
  }

  // Applique la nouvelle difficulté
  diffLvl = newLvl;
}



void loop() {}

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
  vTaskDelay(10000 / portTICK_PERIOD_MS);
  while (true) {
    NimBLEScanResults foundDevices = pBLEScan->getResults(SCAN_TIME, false);
    // Serial.print("Devices found: ");
    // Serial.println(foundDevices.getCount());
    // Serial.println("Scan done!");
    pBLEScan->clearResults();  // delete results scan buffer to release memory
    vTaskDelay((100) / portTICK_PERIOD_MS);
  }
}

// Gestionnaire d'événements Wi-Fi
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case WIFI_EVENT_STA_DISCONNECTED:  // Ancien SYSTEM_EVENT_STA_DISCONNECTED
      Serial.println("Wi-Fi déconnecté. Tentative de reconnexion...");
      connectToWiFi();
      break;

    case IP_EVENT_STA_GOT_IP:  // Ancien SYSTEM_EVENT_STA_GOT_IP
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
      reconnectAttempts = 0;  // Réinitialise le compteur d'échecs
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
        reconnectAttempts = 0;  // Réinitialise le compteur après redémarrage
      }

      vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
  }
}

// Fonction pour redémarrer le Wi-Fi
void resetWiFi() {
  WiFi.disconnect(true);                  // Déconnecte du réseau et réinitialise les paramètres Wi-Fi
  vTaskDelay(2000 / portTICK_PERIOD_MS);  // Pause pour garantir une réinitialisation propre
  connectToWiFi();                        // Reconnecte au Wi-Fi
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
    genState = INITIATE;
  }

  if (message.startsWith(String("Generator:difficulte("))) {
    int startIdx = message.indexOf("(") + 1;
    int endIdx = message.indexOf(")");
    String difficultyStr = message.substring(startIdx, endIdx);
    float difficultyLevel = difficultyStr.toFloat();

    Serial.print("Niveau de difficulté reçu : ");
    Serial.println(difficultyLevel);


    applyDifficultyKeepPercent(difficultyLevel);  // <-- garde le %
    Serial.print("Niveau de difficulté appliqué : ");
    Serial.println(diffLvl);
  }

  if (message == String(esp32_id) + ":generateur reparer") {
    turnCount = 0;
    lastAnnouncedTurn = 0;
    JustOnce = false;
    isLooped = false;
    genState = FINISHED;
  }

  if (message == String(esp32_id) + ":generateur lvl 1") {
    turnCount = 0;
    lastAnnouncedTurn = 0;
    JustOnce = false;
    isLooped = false;
    genState = COUNTING;
    notifyMQTT("generateur lvl 1");
  }

  if (message == String(esp32_id) + ":generateur lvl 2") {
    turnCount = 0;
    lastAnnouncedTurn = 0;
    JustOnce = false;
    isLooped = false;
    genState = WAITING;
  }

  if (message == String(esp32_id) + ":generateur lvl 3") {
    turnCount = 0;
    lastAnnouncedTurn = 0;
    JustOnce = false;
    isLooped = false;
    genState = COUNTING2;
  }

  if (message == String(esp32_id) + ":inactif") {
    Serial.println("🚨 Mode INACTIF activé via MQTT.");
    genState = OUT;
    notifyMQTT("off");
  }

  if (message == String(esp32_id) + ":actif") {
    Serial.println("✅ Mode ACTIF activé via MQTT.");
    genState = INITIATE;
    notifyMQTT("on");
  }
}

// Notifier le serveur MQTT
void notifyMQTT(const char* message) {
  String fullMessage = String(esp32_id) + ":" + message;  // Préfixer le message par l'identifiant
  client.publish("esp32/donnees", fullMessage.c_str());
}
