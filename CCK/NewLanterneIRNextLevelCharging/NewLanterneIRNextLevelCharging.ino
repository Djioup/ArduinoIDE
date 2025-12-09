#include <WiFi.h>
#include <PubSubClient.h>
#include <IRremote.hpp>
#include <Adafruit_NeoPixel.h>
#include <NimBLEDevice.h>
#include <DFRobotDFPlayerMini.h>

// ---------- Wi-Fi ----------
const char* ssid = "PelucheGang";
const char* password = "CACHE-CACHEKILLER";
const char* mqtt_server = "192.168.0.139";
const char* mqtt_user = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";

// ---------- Identité ----------
const int lanternID = 1;  // <-- change pour chaque lanterne
const char* esp32_id = "Lanterne1";
#define MOTOR_PIN 13
#define BUTTON_PIN 23
#define BUTTON_LED 32

#define RUBLED 25
#define RUB_LEDS 6
Adafruit_NeoPixel RubLed(RUB_LEDS, RUBLED, NEO_GRB + NEO_KHZ800);

// --- Animation de respiration pour les munitions en cours de recharge ---
static uint8_t breath = 80;    // luminosité actuelle (0-255)
static int8_t dir = 4;         // direction (+/-)
static uint32_t lastStep = 0;  // chrono

// ---------- IR ----------
#define IR_SEND_PIN 14
const uint16_t irIddle = 0x0000;  // code que le gilet reconnaît
uint16_t irShoot = 0x0001;
const uint8_t irCmd = 0xA9;
int Cooldown = 10000;

// --- Flash blanc x3 (non bloquant) ---
bool flashing = false;
uint8_t flashCount = 0;            // nb de flashes terminés
uint8_t flashState = 0;            // 0 = BLANC allumé, 1 = éteint (retour rouge)
unsigned long flashTs = 0;         // chrono
const uint16_t FLASH_ON_MS = 60;   // durée du blanc
const uint16_t FLASH_OFF_MS = 60;  // durée entre deux blancs (rouge)
unsigned long lastIR = 0;
bool Led;
int Recharge = 30000;
int TotalMunition = 3;
int munitions = 3;
unsigned long LastRecharge = 0;
int Ame = 0;
bool borneMunition = false;
unsigned long borneTime = 0;
bool Find = false;
bool lvlup1 = false;
bool lvlup2 = false;
bool lvlup3 = false;

// --- Charge (appui maintenu) ---
bool isCharging = false;
unsigned long chargeStart = 0;
const uint16_t CHARGE_MS = 1000;  // temps à maintenir avant tir (ajuste)
bool prevPressed = false;         // mémorise l'état précédent du bouton


// ---------- LED Ring ----------
#define RING_PIN 27
#define NUM_LEDS 12
Adafruit_NeoPixel ring(NUM_LEDS, RING_PIN, NEO_GRB + NEO_KHZ800);

// ---------- DFPlayer (réglages uniquement) ----------
#define RX_PIN 16
#define TX_PIN 17
DFRobotDFPlayerMini myDFPlayer;
// (exemples de pistes si tu en as besoin plus tard)
const uint8_t TRACK_FIND = 1;
const uint8_t TRACK_TICK = 2;
const uint8_t TRACK_HIT = 3;

// ---------- MQTT ----------
WiFiClient espClient;
PubSubClient client(espClient);

NimBLEScan* pBLEScan;
#define SCAN_TIME 3000

// ---------- FreeRTOS ----------
TaskHandle_t networkTaskHandle = nullptr;
TaskHandle_t irTaskHandle = nullptr;
TaskHandle_t audioTaskHandle;
TaskHandle_t bleScanTaskHandle;

bool startVib = false;
unsigned long NowS = 0;
unsigned long LastShoot = 0;
bool Shooting = false;

// ---------- Protos ----------
void connectToWiFi();
void reconnectMQTT();
void callback(char* topic, byte* payload, unsigned int length);
void networkTask(void* parameter);
void irTask(void* parameter);

// inline void dfpPlay(uint8_t track) {
//   // Appels DFPlayer rapides + petites pauses pour stabilité
//   // myDFPlayer.volume(30);
//   // vTaskDelay(30 / portTICK_PERIOD_MS);
//   myDFPlayer.play(track);
// }

// --- Proximité Borne Munition (hystérésis + timeout) ---
volatile bool ammoNear = false;      // état courant (dans zone)
volatile int ammoRSSI = -127;        // dernier RSSI consolidé
volatile uint32_t ammoLastSeen = 0;  // timestamp dernière vue

// Seuils
constexpr int RSSI_ENTER = -80;  // entrer dans la zone
constexpr int RSSI_EXIT = -100;  // sortir de la zone (un peu plus bas que ENTER)
constexpr uint32_t AMMO_TIMEOUT_MS = 5000;

// Meilleur RSSI observé durant le scan en cours (réinitialisé à chaque passe)
volatile int scanBestRSSI = -127;


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

class MyScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    if (!dev->haveName()) return;
    const char* name = dev->getName().c_str();
    if (strncmp(name, "Munition", 8) == 0) {
      int rssi = dev->getRSSI();
      // Garde le meilleur RSSI du scan en cours
      if (rssi > scanBestRSSI) scanBestRSSI = rssi;
      // On peut aussi mettre à jour "en live"
      ammoRSSI = rssi;
      ammoLastSeen = millis();
      Serial.printf("[BLE] %s RSSI=%d\n", name, rssi);
    }
  }
  void onScanEnd(NimBLEScanResults) {
    // rien
  }
} scanCallbacks;




// ============================== SETUP/LOOP ==============================
void setup() {
  Serial.begin(115200);


  // IR
  IrSender.begin(IR_SEND_PIN);

  pinMode(MOTOR_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUTTON_LED, OUTPUT);

  // LED -> ROUGE FIXE (identique à ton intention)
  ring.begin();
  ring.setBrightness(255);
  ring.fill(ring.Color(255, 0, 0), 0, NUM_LEDS);
  ring.show();

  RubLed.begin();

  // DFPlayer (réglages uniquement)
  // ---------- DFPlayer ----------
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  vTaskDelay(1500 / portTICK_PERIOD_MS);
  if (!myDFPlayer.begin(Serial2)) {
    Serial.println("DFPlayer Mini non détecté !");
    // while (true) { vTaskDelay(100 / portTICK_PERIOD_MS); }
  }
  Serial.println("DFPlayer Mini OK.");
  myDFPlayer.volume(30);
  delay(2000);
  // myDFPlayer.play(4);
  // delay(2000);

  // WiFi/MQTT config (structure inchangée)
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // --- NimBLE init ---
  NimBLEDevice::init("");
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(&scanCallbacks);
  pBLEScan->setActiveScan(true);  // Scan actif pour plus de données
  pBLEScan->setInterval(100);     // ms
  pBLEScan->setWindow(25);        // ms (<= interval)
  // pBLEScan->setDuplicateFilter(true);

  audioQ = xQueueCreate(16, sizeof(AudioCmd));

  // Tasks FreeRTOS
  xTaskCreatePinnedToCore(networkTask, "NetworkTask", 8192, NULL, 1, &networkTaskHandle, 0);
  xTaskCreatePinnedToCore(irTask, "IrTask", 8192, NULL, 3, &irTaskHandle, 1);
  xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 2, &audioTaskHandle, 1);
  xTaskCreatePinnedToCore(bleScanTask, "BLEScanTask", 4096, NULL, 1, &bleScanTaskHandle, 0);
}

void loop() {
  // Tout tourne en tasks; on ne fait rien ici.
}

// ============================== TASKS ==============================

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

void bleScanTask(void* parameter) {
  // Scan actif court et fréquent = meilleure réactivité, peu de latence
  // const uint32_t scanMs = 1200;  // ~1.2 s
  const TickType_t idleDelay = pdMS_TO_TICKS(50);

  for (;;) {
    scanBestRSSI = -127;                // reset pour ce cycle
    pBLEScan->start(SCAN_TIME, false);  // bloquant, mais dans SA tâche

    // Consolidation: si on a vu au moins une "Munition" durant ce cycle
    // ammoRSSI et ammoLastSeen ont déjà été mis à jour dans onResult()

    // Évalue l'état “near” avec hystérésis + timeout
    const uint32_t now = millis();
    const bool stillFresh = (now - ammoLastSeen) < AMMO_TIMEOUT_MS;

    // Si on est déjà "near", on est plus tolérant (RSSI_EXIT)
    bool wantNear;
    if (ammoNear) wantNear = stillFresh && (ammoRSSI >= RSSI_EXIT);
    else wantNear = stillFresh && (ammoRSSI >= RSSI_ENTER);

    if (wantNear != ammoNear) {
      ammoNear = wantNear;
      if (ammoNear) onAmmoProximityStart();
      else onAmmoProximityStop();
    }

    vTaskDelay(idleDelay);
  }
}


void networkTask(void* parameter) {
  vTaskDelay(3000 / portTICK_PERIOD_MS);
  connectToWiFi();
  reconnectMQTT();
  for (;;) {
    if (!client.connected()) reconnectMQTT();
    client.loop();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void irTask(void* parameter) {
  // ÉMISSION IR CONTINUE (comme dans ton loop() d’origine)
  const TickType_t period = 100 / portTICK_PERIOD_MS;  // même timing que ton delay(120)
  for (;;) {

    renderMunitionBase();

    if (munitions > TotalMunition) munitions = TotalMunition;
    if (munitions < 0) munitions = 0;
    // tout en haut de la loop de irTask()
    static uint32_t dbTs = 0;
    static bool btn = HIGH, btnRaw = HIGH;

    btnRaw = (digitalRead(BUTTON_PIN) == LOW);  // actif bas
    if (btnRaw != btn) {                        // changement ?
      if (millis() - dbTs > 50) btn = btnRaw;   // anti-rebond ~3ms
    } else {
      dbTs = millis();
    }
    bool pressed = btn;

    // if (!dfp_playing)
    // {
    //   audioLoop(4);
    // }

    // if (myDFPlayer.available()) {
    //   uint8_t type = myDFPlayer.readType();
    //   int value = myDFPlayer.read();
    //   if (type == DFPlayerPlayFinished) {
    //     audioPlay(4);
    //     // vTaskDelay(400 / portTICK_PERIOD_MS);
    //     // la piste est terminée
    //   }
    // }


    if (millis() - lastIR > 100 && !Shooting && !isCharging && millis() - LastShoot > Cooldown) {
      IrSender.sendNEC(irIddle, irCmd, 1);  // 1 trame + 2 repeats NEC (identique à ton code)
      lastIR = millis();
    }
    // vTaskDelay(period);
    // if(digitalRead(BUTTON_PIN) == LOW)  Serial.println("BUTTON ok");
    if (millis() - LastShoot > Cooldown && munitions != 0) {
      digitalWrite(BUTTON_LED, HIGH);
      ring.fill(ring.Color(255, 0, 0), 0, NUM_LEDS);
      ring.show();
    } else {
      digitalWrite(BUTTON_LED, LOW);
      ring.fill(ring.Color(0, 0, 0), 0, NUM_LEDS);
      ring.show();
    }

    if (millis() - NowS > 3000 && startVib && digitalRead(BUTTON_PIN) == HIGH && !Find) {
      StopVibration();
      startVib = false;
    }

    // if (digitalRead(BUTTON_PIN) == LOW && millis() - LastShoot > Cooldown) {
    //   LastShoot = millis();
    //   Shooting = true;

    //   // --- démarrer la rafale de 3 flashes ---
    //   flashing = true;
    //   flashCount = 0;
    //   flashState = 0;  // on commence par allumer en blanc
    //   flashTs = millis();
    //   // ring.fill(ring.Color(255, 255, 255), 0, NUM_LEDS);
    //   // ring.show();

    //   // envoi IR tir
    //   IrSender.sendNEC(irShoot, irCmd, 3);
    //   // vTaskDelay(period);
    // }

    // lecture bouton (LOW = appuyé car INPUT_PULLUP)
    // bool pressed = (digitalRead(BUTTON_PIN) == LOW);

    if (munitions != TotalMunition && millis() - LastRecharge > Recharge) {
      munitions += 1;
      LastRecharge = millis();
    }

    if (borneMunition && millis() - borneTime > 20000) {
      munitions = TotalMunition;
      borneTime = millis();
    }

    // démarrage de charge si possible (pas en cooldown, pas déjà en charge)
    if (!isCharging && (millis() - LastShoot > Cooldown) && pressed && munitions != 0 && (millis() - chargeStart > 600)) {
      isCharging = true;
      chargeStart = millis();
      // audioStop();
      audioPlay(1);
    }

    // progression de charge (UI) ou annulation
    if (isCharging) {
      unsigned long elapsed = millis() - chargeStart;

      // si on relâche AVANT la fin -> annule
      if (!pressed) {
        isCharging = false;
        audioStop();
        ring.fill(ring.Color(255, 0, 0), 0, NUM_LEDS);
        ring.show();  // retour rouge
        StopVibration();
      } else {
        // MAJ UI
        float t = (float)elapsed / (float)CHARGE_MS;
        if (t > 1.0f) t = 1.0f;
        drawChargeProgress(t);
        // vibration en fonction de la progression
        if (pressed) chargeVibration(t);

        startVib = true;
        NowS = millis();
        digitalWrite(BUTTON_LED, LOW);



        // charge complète -> TIR auto (tant que le bouton reste appuyé)
        if (elapsed >= CHARGE_MS) {
          isCharging = false;
          Shooting = true;
          LastShoot = millis();
          StopVibration();
          Shooting = false;
          if (munitions == TotalMunition) {
            LastRecharge = millis();
          }
          munitions -= 1;
          doShoot();
          // ring.fill(ring.Color(255, 0, 0), 0, NUM_LEDS);
          // ring.show();  // retour rouge
        }
      }
    }

    // mémorise l'état (utile si tu veux déclencher au relâchement)
    prevPressed = pressed;


    // if (Shooting && millis() - LastShoot > 100) {
    //   ring.fill(ring.Color(255, 0, 0), 0, NUM_LEDS);
    //   ring.show();
    //   Shooting = false;
    // }

    // --- gestion non bloquante des 3 flashes ---
    // if (flashing) {
    //   unsigned long now = millis();
    //   if (flashState == 0) {
    //     // état BLANC allumé
    //     if (now - flashTs >= FLASH_ON_MS) {
    //       // repasse au ROUGE (pause entre deux blancs)
    //       // ring.fill(ring.Color(255, 0, 0), 0, NUM_LEDS);
    //       // ring.show();
    //       flashTs = now;
    //       flashState = 1;
    //     }
    //   } else {
    //     // état ROUGE (pause)
    //     if (now - flashTs >= FLASH_OFF_MS) {
    //       flashCount++;
    //       if (flashCount >= 3) {
    //         // 3 flashes terminés : on reste en ROUGE
    //         flashing = false;
    //         Shooting = false;
    //       } else {
    //         // relance un nouveau BLANC
    //         // ring.fill(ring.Color(255, 255, 255), 0, NUM_LEDS);
    //         // ring.show();
    //         flashTs = now;
    //         flashState = 0;
    //       }
    //     }
    //   }
    // }

    if (Ame >= 50) {
      Cooldown = 4000;
      Recharge = 15000;
      TotalMunition = 6;
      irShoot = 0x1003;
      if (!lvlup3) {
        audioPlay(6);
        lvlup3 = true;
      }
    }

    else if (Ame >= 25) {
      Cooldown = 6000;
      Recharge = 20000;
      TotalMunition = 5;
      irShoot = 0x1002;
      if (!lvlup2) {
        audioPlay(6);
        lvlup2 = true;
      }
    }

    else if (Ame >= 10) {
      Cooldown = 8000;
      Recharge = 30000;
      TotalMunition = 4;
      irShoot = 0x1001;
      if (!lvlup1) {
        audioPlay(6);
        lvlup1 = true;
      }
    }

    else if (Ame < 10) {
      Cooldown = 10000;
      Recharge = 40000;
      TotalMunition = 3;
      irShoot = 0x1001;
    }
  }
}

// ============================== HELPERS D'ORIGINE ==============================
void connectToWiFi() {
  Serial.print("Connexion WiFi à ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    Serial.print(".");
  }
  Serial.println("\nConnecté au WiFi");
  Serial.print("IP : ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connexion MQTT...");
    if (client.connect(esp32_id, mqtt_user, mqtt_password)) {
      Serial.println("Connecté");
      // souscrire à SON topic (structure inchangée)
      String topic = String("esp32/lanterne1");
      client.subscribe("esp32/lanterne1");
      client.subscribe("unity/commandes");
      Serial.println("Sub sur " + topic);
    } else {
      Serial.print("Échec MQTT, rc=");
      Serial.print(client.state());
      Serial.println(" retry dans 5s");
      vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
  }
}

void Vibration(int Vit) {
  analogWrite(MOTOR_PIN, Vit);
}

void StopVibration() {
  analogWrite(MOTOR_PIN, 0);
}

void doShoot() {
  // envoi IR tir
  audioPlay(3);
  // petit feedback visuel (3 flashes blancs rapides, bloquant ~360 ms)
  for (int i = 0; i < 3; i++) {
    ring.fill(ring.Color(255, 255, 255), 0, NUM_LEDS);
    ring.show();
    vTaskDelay(60 / portTICK_PERIOD_MS);
    ring.fill(ring.Color(255, 0, 0), 0, NUM_LEDS);
    ring.show();
    vTaskDelay(60 / portTICK_PERIOD_MS);
  }
  IrSender.sendNEC(irShoot, irCmd, 2);
}

void drawChargeProgress(float t) {  // t: 0.0 -> 1.0
  int lit = (int)(t * NUM_LEDS);
  if (lit < 0) lit = 0;
  if (lit > NUM_LEDS) lit = NUM_LEDS;
  for (int i = 0; i < NUM_LEDS; i++) {
    if (i < lit) ring.setPixelColor(i, ring.Color(255, 255, 255));  // blanc = charge
    else ring.setPixelColor(i, ring.Color(255, 0, 0));              // rouge = fond
  }
  ring.show();
}

void chargeVibration(float t) {
  // t = 0.0 (début) -> vibration faible, t = 1.0 (plein) -> forte
  int pwm = (int)(t * 255) + 80;
  if (pwm > 255) pwm = 255;
  if (digitalRead(BUTTON_PIN) == LOW) analogWrite(MOTOR_PIN, pwm);
}

void onAmmoProximityStart() {
  Serial.println("[Ammo] PROXIMITY START (≤ -80 dBm)");
  borneMunition = true;
  borneTime = millis();

  // EXEMPLES — à adapter :
  // audioPlay(TRACK_FIND);
  // Vibration(180);
  // ring.fill(ring.Color(0, 80, 255), 0, NUM_LEDS); ring.show(); // bleu discret
}

void onAmmoProximityStop() {
  Serial.println("[Ammo] PROXIMITY STOP (> -83 dBm ou timeout)");
  borneMunition = false;
  // EXEMPLES — à adapter :
  // audioStop();
  // StopVibration();
  // ring.fill(ring.Color(255, 0, 0), 0, NUM_LEDS); ring.show();   // retour rouge
}

void renderMunitionBase() {


  uint32_t now = millis();
  if (now - lastStep > 30) {
    lastStep = now;
    breath += dir;
    if (breath > 150) {  // plafond
      breath = 150;
      dir = -dir;
    } else if (breath < 40) {  // plancher
      breath = 40;
      dir = -dir;
    }
  }
  RubLed.clear();
  int n = constrain(munitions, 0, RUB_LEDS);
  for (int i = 0; i < n; i++) {
    RubLed.setPixelColor(i, RubLed.Color(255, 0, 0));
  }
  RubLed.show();

  if (borneMunition) {
    int f = constrain(TotalMunition, munitions, RUB_LEDS);
    {
      for (int y = munitions; y < f; y++) {
        RubLed.setPixelColor(y, RubLed.Color(breath, breath/3, 0));
      }
      RubLed.show();
    }
  }
}


// ============================== CALLBACK (structure inchangée) ==============================
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("Reçu sur " + String(topic) + ": " + msg);

  if (msg.endsWith("RESET")) {
    Serial.println("[RESET] remise à zéro");
    Ame = 0;
    Cooldown = 10000;
    Recharge = 30000;
    TotalMunition = 3;
    irShoot = 0x0001;
    lvlup1 = false;
    lvlup2 = false;
    lvlup3 = false;
  }

  if (msg.endsWith("FIND")) {
    if (!isCharging) Vibration(200);
    NowS = millis();
    startVib = true;
    Find = true;
  }

  if (msg.endsWith("LOST")) {
    StopVibration();
    Find = false;
  }

  if (msg.endsWith("TICK")) {
    audioPlay(2);
    Vibration(255);
    Ame += 1;
  }

  if (msg.endsWith("HIT")) {
    audioPlay(5);
    Vibration(255);
    Ame += 3;
  }
}
