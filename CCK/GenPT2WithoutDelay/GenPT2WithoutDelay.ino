#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include <NimBLEDevice.h>              // <-- NEW: NimBLE
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_NeoPixel.h>

// Informations Wi-Fi
const char* ssid = "PelucheGang";
const char* password = "CACHE-CACHEKILLER";
const char* mqtt_server = "192.168.0.139";
const char* mqtt_user = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";
const char* esp32_id = "Generator0";

// WiFi et MQTT
WiFiClient espClient;
PubSubClient client(espClient);

// BLE (NimBLE)
NimBLEScan* pBLEScan;

// DFPlayer Mini Configuration
#define RX_PIN 16
#define TX_PIN 17
DFRobotDFPlayerMini myDFPlayer;

// Capteur et LEDs
const int MAG_SENSOR_1 = 27;
const int MAG_SENSOR_2 = 26;
const int GENERATOR_BUTTON_PIN = 23;
const int LED_BUTTON_PIN = 22;
const int LED_DECORATIVE_PIN = 21;
const int LED_DECORATIVE_PIN2 = 19;
const int LED_DECORATIVE_PIN3 = 18;
const int LED_DECORATIVE_PIN4 = 33;
#define ANNEAULED 13
#define NUM_LEDS 24
Adafruit_NeoPixel ring(NUM_LEDS, ANNEAULED, NEO_GRB + NEO_KHZ800);

unsigned long lastBlinkTime = 0;
bool blinkState = false;
const int blinkInterval = 300;  // millisecondes
int KillerTurn = 0;

bool isLooped = false;
bool JustOnce = false;
const int NbrTour = 2;
int SpottedPlayer = 0;
float diffLvl = 1;
unsigned long ShockTime;
static int lastAnnouncedTurn = 0;
unsigned long lastTurnDetectedTime = 0;  // Timestamp du dernier tour détecté
unsigned long KillerTime = 0;

int playerRSSI;

bool capteur2Passe = false;      // Variable pour savoir si le capteur secondaire a été traversé
int turnCount = 0;               // Nombre de tours validés
unsigned long lastTurnTime = 0;  // Anti-rebond global
bool Shocked = false;
bool Killerholding = false;

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

// Tâche FreeRTOS
TaskHandle_t bleScanTaskHandle;
TaskHandle_t mqttTaskHandle;
TaskHandle_t gameTaskHandle;

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
    if (fresh && players[i].spotted == 1 && players[i].rssi > rssiThreshold) return true;
  }
  return false;
}

//==================== NimBLE callbacks ====================
class MyScanCallbacks : public NimBLEScanCallbacks {
public:
  void onResult(NimBLEAdvertisedDevice* adv) {
    if (!adv || !adv->haveName()) return;
    String devName = String(adv->getName().c_str());
    if (!devName.startsWith("Player")) return;

    int rssi = adv->getRSSI();

    String manufacturerData;
    if (adv->haveManufacturerData()) {
      std::string md = adv->getManufacturerData();
      manufacturerData = String(md.c_str(), md.length());
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
    // Serial.printf("[BLE] %s  RSSI=%d  E:%d  -> slot %d\n", key.c_str(), rssi, spotted, slot);
  }
};

//===================================================SETUP========================================================

// Anti-blocage DFPlayer : rate-limit simple (≥120 ms entre 2 cmd)
static unsigned long dfpLastCmdMs = 0;
static const unsigned long DFP_MIN_GAP_MS = 200;
static inline bool dfp_canSend() { return (millis() - dfpLastCmdMs) >= DFP_MIN_GAP_MS; }
static inline void dfp_markSend() { dfpLastCmdMs = millis(); }

void safeVolume(int v){ if(!dfp_canSend()) return; myDFPlayer.volume(constrain(v,0,30)); dfp_markSend(); }
void safePlay(int t){ if(!dfp_canSend()) return; myDFPlayer.play(t); dfp_markSend(); }
void safeLoop(int t){ if(!dfp_canSend()) return; myDFPlayer.loop(t); dfp_markSend(); }
void safeStop(){ if(!dfp_canSend()) return; myDFPlayer.stop(); dfp_markSend(); }

// --- SETUP ---
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);

  // IO
  pinMode(MAG_SENSOR_1, INPUT_PULLUP);
  pinMode(MAG_SENSOR_2, INPUT_PULLUP);
  pinMode(GENERATOR_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_BUTTON_PIN, OUTPUT);
  pinMode(LED_DECORATIVE_PIN, OUTPUT);
  pinMode(LED_DECORATIVE_PIN2, OUTPUT);
  pinMode(LED_DECORATIVE_PIN3, OUTPUT);
  pinMode(LED_DECORATIVE_PIN4, OUTPUT);
  digitalWrite(LED_BUTTON_PIN, HIGH);
  analogWrite(LED_DECORATIVE_PIN, 0);
  analogWrite(LED_DECORATIVE_PIN2, 0);
  analogWrite(LED_DECORATIVE_PIN3, 0);
  analogWrite(LED_DECORATIVE_PIN4, 0);

  ring.begin();
  ring.show();
  for (int i = 0; i < NUM_LEDS; i++) ring.setPixelColor(i, ring.Color(255, 0, 0));
  ring.show();

  // DFPlayer init non-bloquante (on n'entre PAS dans une boucle infinie en cas d’échec)
  Serial.println("Initialisation du DFPlayer Mini...");
  if (!myDFPlayer.begin(Serial2)) {
    Serial.println("❌ DFPlayer non détecté (on continue, audio désactivé jusqu’à prochain envoi).");
  } else {
    Serial.println("✅ DFPlayer Mini initialisé.");
    // Vérification SD non bloquante: pas de delay
    int sdStatus = myDFPlayer.readType();
    if (sdStatus != DFPlayerCardInserted) Serial.println("⚠️ Aucune carte SD détectée !");
  }

  // NimBLE
  NimBLEDevice::init("");
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(new MyScanCallbacks(), true /* wantDuplicates */);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(30);
  // Scan continu non-bloquant
  pBLEScan->start(0, /*isContinue*/ true, /*restart*/ true);

  // Tâches
  xTaskCreatePinnedToCore(mqttTask,      "MQTT Task",     4096, NULL, 1, &mqttTaskHandle, 0);
  xTaskCreatePinnedToCore(generatorTask, "Generator Task",8192, NULL, 1, &gameTaskHandle, 1);
  // NOTE: plus besoin de bleScanTask avec NimBLE en continu
}

//========================================GAME LOGIC=====================================================

// --- FONCTIONS AUDIO (remplacent les delays par rate-limit simple) ---
void playTrackOnce(int track, int volume) {
  Serial.printf("Lecture piste %d vol %d\n", track, volume);
  safeVolume(volume);
  safePlay(track);
}

void playLoopedTrack(int track, int volume) {
  if (!isLooped) {
    Serial.printf("Lecture boucle piste %d vol %d\n", track, volume);
    safeVolume(volume);
    safeLoop(track);
    isLooped = true;
  }
}

void stopLoopTrack() {
  if (!isLooped) return;
  Serial.println("Arrêt boucle.");
  safeStop();
  isLooped = false;
}

// --- TÂCHE GENERATOR (CORE 1) ---
void generatorTask(void* parameter) {
  Serial.println("Generator Task démarré.");
  stateStartTime = millis();

  while (true) {

    if (anyThreat(-100) && !Shocked) {
      previousGenState = genState;
      genState = SHOCK;
      Killerholding = true;
      ShockTime = millis();
      Serial.println("⚡ Choc déclenché ! Joueur trop proche !");
      Shocked = true;
      stopLoopTrack();
    }

    if (genState == SHOCK && millis() - ShockTime >= 10000 && Shocked) {
      genState = previousGenState;
      Serial.println("↩ Retour à l’état précédent !");
      Shocked = false;
      Killerholding = false;
      stopLoopTrack();
    }

    if (digitalRead(GENERATOR_BUTTON_PIN) == LOW && !Killerholding) {
      stopLoopTrack();
      previousGenState = genState;
      genState = KILLERHOLDING;
      Shocked = true;
      Killerholding = true;
      KillerTime = millis();
      KillerTurn = 0;
      Serial.println("KillerHolding Activated");
      playLoopedTrack(7, 30);
    }

    switch (genState) {
      case COUNTING:
        colorSecondRed();
        digitalWrite(LED_BUTTON_PIN, HIGH);
        analogWrite(LED_DECORATIVE_PIN,  map(turnCount, 0, NbrTour + diffLvl, 0, 255));
        analogWrite(LED_DECORATIVE_PIN2, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
        analogWrite(LED_DECORATIVE_PIN3, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
        analogWrite(LED_DECORATIVE_PIN4, map(turnCount, 0, NbrTour + diffLvl, 0, 255));

        updateProgressRing(turnCount, NbrTour + diffLvl);

        if (digitalRead(MAG_SENSOR_1) == LOW && !capteur2Passe) {
          capteur2Passe = true;
          Serial.println("🔸 Capteur secondaire activé !");
        }
        if (capteur2Passe && digitalRead(MAG_SENSOR_2) == LOW) {
          turnCount++;
          Serial.printf("✅ Tour complet détecté ! Total : %d\n", turnCount);
          capteur2Passe = false;
        }

        if (turnCount >= lastAnnouncedTurn + 1 && turnCount < NbrTour + diffLvl) {
          lastAnnouncedTurn = turnCount;
          stopLoopTrack();
          playLoopedTrack(10, (10 + (turnCount * 3)));
        }

        if (turnCount >= NbrTour + diffLvl) {
          genState = WAITING;
          notifyMQTT("generateur lvl 2");
          stateStartTime = millis();
          stopLoopTrack();
          updateProgressRing(turnCount, NbrTour + diffLvl);
          Serial.println("✅ Niveau 1 validé, passage en WAITING.");
        }
        break;

      case WAITING: {
        Killerholding = true;
        Shocked = true;
        playLoopedTrack(9, 30);
        digitalWrite(LED_BUTTON_PIN, LOW);
        analogWrite(LED_DECORATIVE_PIN, 255);
        analogWrite(LED_DECORATIVE_PIN2, 255);
        analogWrite(LED_DECORATIVE_PIN3, 255);
        analogWrite(LED_DECORATIVE_PIN4, 255);

        unsigned long durationMs = (unsigned long)(30000 * diffLvl);
        unsigned long elapsed = millis() - stateStartTime;
        animateWaitingSimple(stateStartTime, durationMs);

        if (elapsed >= durationMs) {
          genState = COUNTING2;
          stopLoopTrack();
          lastTurnDetectedTime = millis();
          turnCount = 0;
          lastAnnouncedTurn = 0;
          notifyMQTT("generateur lvl 3");
          Killerholding = false;
          Shocked = false;
          Serial.println("⏳ Timer écoulé, passage en COUNTING2");
        }
        break;
      }

      case BUTTON_PHASE:
        if (digitalRead(GENERATOR_BUTTON_PIN) == LOW) {
          if (buttonHoldStartTime == 0) buttonHoldStartTime = millis();
          else if (millis() - buttonHoldStartTime >= (unsigned long)(5000 + diffLvl * 1000)) {
            genState = COUNTING2;
            lastTurnDetectedTime = millis();
            turnCount = 0;
            lastAnnouncedTurn = 0;
            Serial.println("✅ Bouton maintenu OK");
            stopLoopTrack();
          }
          digitalWrite(LED_BUTTON_PIN, HIGH);
        } else {
          buttonHoldStartTime = 0;
          digitalWrite(LED_BUTTON_PIN, (millis() / 500) % 2);
          playLoopedTrack(4, 30);
        }
        analogWrite(LED_DECORATIVE_PIN,  random(0, 255));
        analogWrite(LED_DECORATIVE_PIN2, random(0, 255));
        analogWrite(LED_DECORATIVE_PIN3, random(0, 255));
        analogWrite(LED_DECORATIVE_PIN4, random(0, 255));
        break;

      case COUNTING2:
        colorFirstHalfGreen();
        playLoopedTrack(4, 30);
        digitalWrite(LED_BUTTON_PIN, HIGH);
        analogWrite(LED_DECORATIVE_PIN,  map(turnCount, 0, NbrTour + diffLvl, 0, 255));
        analogWrite(LED_DECORATIVE_PIN2, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
        analogWrite(LED_DECORATIVE_PIN3, map(turnCount, 0, NbrTour + diffLvl, 0, 255));
        analogWrite(LED_DECORATIVE_PIN4, map(turnCount, 0, NbrTour + diffLvl, 0, 255));

        updateProgressRing2(turnCount, (NbrTour) + diffLvl);
        if (digitalRead(MAG_SENSOR_1) == LOW && !capteur2Passe) {
          capteur2Passe = true;
          Serial.println("🔸 Capteur secondaire activé !");
        }
        if (capteur2Passe && digitalRead(MAG_SENSOR_2) == LOW) {
          turnCount++;
          lastTurnDetectedTime = millis();
          Serial.printf("✅ Tour complet détecté ! Total : %d\n", turnCount);
          capteur2Passe = false;
        }

        if (turnCount >= lastAnnouncedTurn + 1 && turnCount < (NbrTour) + diffLvl) {
          lastAnnouncedTurn = turnCount;
        }

        if (turnCount >= (NbrTour) + diffLvl) {
          genState = WAITP3;
          notifyMQTT("generateur reparer");
          stateStartTime = millis();
          updateProgressRing2(turnCount, (NbrTour) + diffLvl);
          stopLoopTrack();
          Serial.println("✅ COUNTING2 validé, passage en WAITP3.");
        }
        break;

      case WAITP3: {
        Killerholding = true;
        Shocked = true;
        playLoopedTrack(9, 30);
        digitalWrite(LED_BUTTON_PIN, LOW);
        analogWrite(LED_DECORATIVE_PIN, 255);
        analogWrite(LED_DECORATIVE_PIN2, 255);
        analogWrite(LED_DECORATIVE_PIN3, 255);
        analogWrite(LED_DECORATIVE_PIN4, 255);

        unsigned long durationMs = 10000;                 // <-- rétabli localement
        unsigned long elapsed = millis() - stateStartTime; // <-- rétabli localement

        if (elapsed >= durationMs) {
          genState = PHASE3;
          stopLoopTrack();
          lastTurnDetectedTime = millis();
          turnCount = 0;
          lastAnnouncedTurn = 0;
          notifyMQTT("generateur lvl 3");
          Killerholding = false;
          Shocked = false;
          Serial.println("⏳ WAITP3 fini, passage en PHASE3");
        }
        break;
      }

      case PHASE3:
        if (digitalRead(GENERATOR_BUTTON_PIN) == LOW) {  // <-- fix: BUTTON_PIN -> GENERATOR_BUTTON_PIN
          genState = FINISHED;
        }
        break;

      case FINISHED:
        Killerholding = true;
        Shocked = true;
        digitalWrite(LED_BUTTON_PIN, HIGH);
        analogWrite(LED_DECORATIVE_PIN, 255);
        analogWrite(LED_DECORATIVE_PIN2, 255);
        analogWrite(LED_DECORATIVE_PIN3, 255);
        analogWrite(LED_DECORATIVE_PIN4, 255);
        playLoopedTrack(5, 30);
        break;

      case SHOCK:
        playLoopedTrack(8, 30);
        animateShockRing();
        digitalWrite((LED_BUTTON_PIN), random(0, 2));
        analogWrite(LED_DECORATIVE_PIN, random(0, 255));
        analogWrite(LED_DECORATIVE_PIN2, random(0, 255));
        analogWrite(LED_DECORATIVE_PIN3, random(0, 255));
        analogWrite(LED_DECORATIVE_PIN4, random(0, 255));
        break;

      case OUT:
        Killerholding = true;
        Shocked = true;
        digitalWrite((LED_BUTTON_PIN), 0);
        analogWrite(LED_DECORATIVE_PIN, 0);
        analogWrite(LED_DECORATIVE_PIN2, 0);
        analogWrite(LED_DECORATIVE_PIN3, 0);
        analogWrite(LED_DECORATIVE_PIN4, 0);
        break;

      case KILLERHOLDING:
        animateKillerHoldingRing(KillerTurn, diffLvl);
        if (digitalRead(MAG_SENSOR_1) == LOW && !capteur2Passe) {
          capteur2Passe = true;
          Serial.println("🔸 Capteur secondaire activé !");
        }
        if (capteur2Passe && digitalRead(MAG_SENSOR_2) == LOW) {
          KillerTurn++;
          Serial.printf("✅ Tour complet détecté ! Total : %d\n", turnCount);
          capteur2Passe = false;
        }
        if (KillerTurn >= diffLvl || millis() - KillerTime > 30000) {
          genState = previousGenState;
          KillerTurn = 0;
          Killerholding = false;
          Shocked = false;
          stopLoopTrack();
          updateProgressRing2(0, (NbrTour) + diffLvl);
          updateProgressRing(0, (NbrTour) + diffLvl);
        }
        break;

      case INITIATE:
        colorAllRed();
        digitalWrite((LED_BUTTON_PIN), 0);
        analogWrite(LED_DECORATIVE_PIN, 0);
        analogWrite(LED_DECORATIVE_PIN2, 0);
        analogWrite(LED_DECORATIVE_PIN3, 0);
        analogWrite(LED_DECORATIVE_PIN4, 0);
        Shocked = false;
        Killerholding = false;
        turnCount = 0;
        lastAnnouncedTurn = 0;
        JustOnce = false;
        isLooped = false;
        KillerTurn = 0;
        stopLoopTrack();
        // Remplacement des vTaskDelay par un petit état temporel
        static unsigned long initT = 0;
        if (initT == 0) initT = millis();
        if (millis() - initT >= 2000) {     // au lieu de vTaskDelay(2000)
          playLoopedTrack(1, 30);
          initT = millis();
          notifyMQTT("generateur lvl 1");
          genState = COUNTING;
        }
        break;
    }
    // Yield court pour ne pas monopoliser le core (non bloquant pour le reste)
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

void updateProgressRing(int turnCount, int totalTours) {
  int ledsToLight = map(turnCount, 0, totalTours, 0, 12);
  ledsToLight = constrain(ledsToLight, 0, 12);
  int blinkingIndex = ledsToLight;
  if (millis() - lastBlinkTime >= (unsigned long)blinkInterval) {
    blinkState = !blinkState; lastBlinkTime = millis();
  }
  for (int i = 0; i < 12; i++) {
    if (i < ledsToLight) ring.setPixelColor(i, ring.Color(0, 255, 0));
    else if (i == blinkingIndex && blinkState && ledsToLight < 12) ring.setPixelColor(i, ring.Color(255, 255, 0));
    else ring.setPixelColor(i, ring.Color(255, 0, 0));
  }
  ring.show();
}

void updateProgressRing2(int turnCount, int totalTours) {
  int ledsToLight = map(turnCount, 0, totalTours, 12, NUM_LEDS);
  ledsToLight = constrain(ledsToLight, 12, NUM_LEDS);
  int blinkingIndex = ledsToLight;
  if (millis() - lastBlinkTime >= (unsigned long)blinkInterval) {
    blinkState = !blinkState; lastBlinkTime = millis();
  }
  for (int i = 12; i < NUM_LEDS; i++) {
    if (i < ledsToLight) ring.setPixelColor(i, ring.Color(0, 255, 0));
    else if (i == blinkingIndex && blinkState && ledsToLight < NUM_LEDS) ring.setPixelColor(i, ring.Color(255, 255, 0));
    else ring.setPixelColor(i, ring.Color(255, 0, 0));
  }
  ring.show();
}

// Anim LED pour SHOCK : strobe rouge + étincelles blanches + flash global très bref
void animateShockRing() {
  const unsigned long blinkInt = 80;
  const unsigned long glitchPeriod = 1000;
  const unsigned long glitchWidth = 30;

  unsigned long now = millis();
  if (now - shockAnim.t >= blinkInt) { shockAnim.t = now; shockAnim.alt = !shockAnim.alt; }
  if (shockAnim.burstT == 0 || now - shockAnim.burstT >= glitchPeriod) shockAnim.burstT = now;

  for (int i = 0; i < NUM_LEDS; i++) {
    bool on = ((i & 1) == (shockAnim.alt ? 1 : 0));
    ring.setPixelColor(i, on ? RGB(255, 0, 0) : 0);
  }
  for (int s = 0; s < 3; s++) ring.setPixelColor(random(NUM_LEDS), RGB(180, 180, 180));
  if (now - shockAnim.burstT < glitchWidth) for (int i = 0; i < NUM_LEDS; i++) ring.setPixelColor(i, RGB(255, 255, 255));
  ring.show();
}

// Anim LED pour KILLERHOLDING : progression verte + comète ambrée + flash vert sur validation
void animateKillerHoldingRing(int turns, int required) {
  unsigned long now = millis();
  if (now - holdAnim.t >= 45) { holdAnim.t = now; holdAnim.pos = (holdAnim.pos + 1) % NUM_LEDS; }
  int req = max(required, 1);
  int progPixels = constrain(map(turns, 0, req, 0, NUM_LEDS), 0, NUM_LEDS);

  for (int i = 0; i < NUM_LEDS; i++) {
    if (i < progPixels) ring.setPixelColor(i, RGB(0, 200, 60));
    else ring.setPixelColor(i, 0);
  }

  for (int k = 0; k < 3; k++) {
    int idx = (holdAnim.pos - k + NUM_LEDS) % NUM_LEDS;
    if (idx >= progPixels) {
      uint8_t r = (k == 0) ? 255 : (k == 1) ? 180 : 110;
      uint8_t g = (k == 0) ? 120 : (k == 1) ? 85 : 50;
      ring.setPixelColor(idx, RGB(r, g, 0));
    }
  }

  if (turns != holdAnim.lastTurn) { holdAnim.lastTurn = turns; holdAnim.flashStart = now; }
  if (holdAnim.flashStart && now - holdAnim.flashStart < 150)
    for (int i = progPixels; i < NUM_LEDS; i++) ring.setPixelColor(i, RGB(0, 255, 100));

  ring.show();
}

// WAITING simple : décharge orange + spinner de luminosité
void animateWaitingSimple(unsigned long startMs, unsigned long durationMs) {
  const int start = 12;
  const int span = 12;  // LEDs 12..23

  unsigned long now = millis();
  if (durationMs == 0) durationMs = 1;  // éviter division par 0

  float t = (float)(now - startMs) / (float)durationMs;
  if (t < 0) t = 0;
  if (t > 1) t = 1;

  int remaining = span - (int)floorf(t * span);
  if (remaining < 0) remaining = 0;
  if (remaining > span) remaining = span;

  static unsigned long lastSpin = 0;
  static int spin = 0;
  if (now - lastSpin >= 90) { lastSpin = now; spin = (spin + 1) % span; }

  for (int i = 0; i < span; i++) {
    int idx = start + i;
    if (i < remaining) {
      uint8_t r = 255, g = 20, b = 0;
      if (i == spin) { g = 100; }
      ring.setPixelColor(idx, ring.Color(r, g, b));
    } else {
      ring.setPixelColor(idx, 0);
    }
  }

  ring.show();
}

void colorFirstHalfGreen() { for (int i = 0; i < 12; i++) ring.setPixelColor(i, ring.Color(0, 255, 0)); ring.show(); }
void colorAllRed()         { for (int i = 0; i < 24; i++) ring.setPixelColor(i, ring.Color(255, 0, 0));  ring.show(); }
void colorSecondRed()      { for (int i = 12; i < 24; i++) ring.setPixelColor(i, ring.Color(255, 0, 0)); ring.show(); }

void loop() {}

//--------------------------------------------------------------------------------------------------------WIFI ----------------------------------------------------------------------------------------------------------------------------------------------
// Tâche MQTT
void mqttTask(void* parameter) {
  // Petit sleep coopératif mais non bloquant pour le système global
  vTaskDelay(500 / portTICK_PERIOD_MS);

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

// (obsolète avec NimBLE continu — laissée vide si jamais tu gardes la création)
void bleScanTask(void* parameter) {
  for(;;){ vTaskDelay(1000 / portTICK_PERIOD_MS); }
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

// Connexion Wi-Fi (bloque seulement la tâche MQTT, pas tout le MCU)
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

// Reconnexion MQTT (bloque seulement la tâche MQTT)
void reconnectMQTT() {
  String willTopic = String("esp32/") + String(esp32_id) + "/status";
  String willMessage = String(esp32_id) + ":offline";

  while (!client.connected()) {
    Serial.println("Connexion au broker MQTT...");
    if (client.connect(esp32_id, mqtt_user, mqtt_password, willTopic.c_str(), 1, true, willMessage.c_str())) {
      Serial.println("Connecté au broker MQTT");
      reconnectAttempts = 0;
      String fullMessage = String(esp32_id) + ":" + "online";
      client.publish(willTopic.c_str(), fullMessage.c_str(), true);
      client.subscribe("unity/commandes");
      notifyMQTT("Request");
    } else {
      Serial.print("Échec, rc=");
      Serial.print(client.state());
      Serial.println(". Nouvelle tentative dans 5 secondes.");
      reconnectAttempts++;
      if (reconnectAttempts >= maxReconnectAttempts) {
        Serial.println("Trop d'échecs de reconnexion. Redémarrage du Wi-Fi...");
        resetWiFi();
        reconnectAttempts = 0;
      }
      vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
  }
}

// Fonction pour redémarrer le Wi-Fi
void resetWiFi() {
  WiFi.disconnect(true);
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  connectToWiFi();
}

// Callback MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];

  Serial.print("Message reçu sur le topic ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

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
    diffLvl = difficultyLevel;
    Serial.print("Niveau de difficulté appliqué : ");
    Serial.println(diffLvl);
  }

  if (message == String(esp32_id) + ":generateur reparer") {
    turnCount = 0; lastAnnouncedTurn = 0; JustOnce = false; isLooped = false; genState = FINISHED;
  }
  if (message == String(esp32_id) + ":generateur lvl 1") {
    turnCount = 0; lastAnnouncedTurn = 0; JustOnce = false; isLooped = false; genState = COUNTING; notifyMQTT("generateur lvl 1");
  }
  if (message == String(esp32_id) + ":generateur lvl 2") {
    turnCount = 0; lastAnnouncedTurn = 0; JustOnce = false; isLooped = false; genState = WAITING;
  }
  if (message == String(esp32_id) + ":generateur lvl 3") {
    turnCount = 0; lastAnnouncedTurn = 0; JustOnce = false; isLooped = false; genState = COUNTING2;
  }
  if (message == String(esp32_id) + ":inactif") { Serial.println("🚨 INACTIF via MQTT."); genState = OUT; notifyMQTT("off"); }
  if (message == String(esp32_id) + ":actif")   { Serial.println("✅ ACTIF via MQTT.");  genState = INITIATE; notifyMQTT("on"); }
}

// Notifier le serveur MQTT
void notifyMQTT(const char* message) {
  String fullMessage = String(esp32_id) + ":" + message;
  client.publish("esp32/donnees", fullMessage.c_str());
}
