#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_NeoPixel.h>
#include <IRremote.hpp>
#include <ArduinoOTA.h>

// ------------------------------------------------------------ INIT ------------------------------------------------------------------------------
// WIFI
const char* ssid = "PelucheGang";
const char* password = "CACHE-CACHEKILLER";
const char* mqtt_server = "192.168.0.57";
const char* mqtt_user = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";
const char* esp32_id = "Generator0";

WiFiClient espClient;
PubSubClient client(espClient);

// DFPlayer Mini Configuration
#define RX_PIN 16
#define TX_PIN 17
DFRobotDFPlayerMini myDFPlayer;

//OTA
volatile bool otaInProgress = false;
bool otaReady = false;

// variables Contrôles
volatile int waitingTime = 30000;
volatile int32_t ShockTime = 30000;
volatile uint32_t DechargeCharging = 10000;
volatile uint32_t DechargeGame = 2000;
volatile uint8_t NbrTour = 12;
volatile uint32_t tolerance = 600;
volatile int WaitingMx = 10;
volatile uint32_t WaitSeqFail = 3000;
volatile uint8_t NbrVictory = 3;
volatile int TotalTimeParty = 1200000;


// variables
const uint8_t NbrCroco = 8;
const uint8_t SizeSeq = 3;
const uint8_t SizeSwitch = 3;

int turnCount = 0;
int winningCombinationSwitch[SizeSwitch] = { 0, 0, 0 };
int currentStateSwitch[SizeSwitch] = { 0, 0, 0 };
int CountVictory = 0;
int targetValue1;
int targetValue2;
int potValue1;
int potValue2;
int winningSequenceBouton[SizeSeq] = { 0, 0, 0 };

unsigned long blinkTimer = 0;
unsigned long StartGameState = 0;
unsigned long startWaitingState = 0;
unsigned long ShockTimer = 0;
unsigned long startPartyTimer = 0;

bool isWinningSwitch = false;
bool SwitchGameWin = false;
bool PotarGameWin = false;
bool SeqBoutonGameWin = false;
bool initCroco = false;
bool initSwitchGame = false;
bool initPotarGame = false;
bool initSeqBouton = false;
bool KillerLed = false;
volatile bool MessageOUT = false;
volatile bool MessageINIT = false;

//GPIO
constexpr uint8_t MAG_SENSOR_1 = 32;
constexpr uint8_t MAG_SENSOR_2 = 33;
constexpr uint8_t switchPins[] = { 19, 18, 5 };
constexpr uint8_t buttonPins[] = { 25, 26, 27 };
constexpr uint8_t ledbutton[] = { 14, 12, 13 };
constexpr uint8_t RECV_PIN = 23;
constexpr uint8_t POT1 = 34;
constexpr uint8_t POT2 = 35;
constexpr uint8_t KillerButon = 4;
constexpr uint8_t KillerButonLED = 2;
constexpr uint8_t croco[] = { 1, 2, 3, 4, 5, 6, 7, 8 };

// ADAFRUIT
constexpr uint8_t ANNEAULED = 15;
constexpr uint16_t NUM_LEDS = 84;
Adafruit_NeoPixel ring(NUM_LEDS, ANNEAULED, NEO_GRB + NEO_KHZ800);
constexpr uint16_t ANNEAU_LEDS_End = 12;
constexpr uint16_t SWITCH_LEDS_Start = 12;
constexpr uint16_t SWITCH_LEDS_End = 24;
constexpr uint16_t SEQ_LEDS_Start = 24;
constexpr uint16_t SEQ_LEDS_End = 36;
constexpr uint16_t POT_LEDS_Start = 36;
constexpr uint16_t POT_LEDS_End = 48;
constexpr uint16_t POT1_LEDS_Start = 48;
constexpr uint16_t POT1_LEDS_End = 60;
constexpr uint16_t POT2_LEDS_Start = 60;
constexpr uint16_t POT2_LEDS_End = 72;
constexpr uint16_t CROCO_LEDS_Start = 72;
constexpr uint16_t CROCO_LEDS_End = 84;


// FREERTOS
enum GeneratorState {
  INITIATE,
  CHARGING,
  GAME,
  WAITING,
  FINISH,
  SHOCK,
  KILLERHOLD,
  OUT
};

const char* stateToString(GeneratorState state) {
  switch (state) {
    case INITIATE: return "INITIATE";
    case CHARGING: return "CHARGING";
    case GAME: return "GAME";
    case WAITING: return "WAITING";
    case FINISH: return "FINISH";
    case SHOCK: return "SHOCK";
    case KILLERHOLD: return "KILLERHOLD";
    case OUT: return "OUT";
    default: return "UNKNOWN";
  }
}

GeneratorState genState = INITIATE;
GeneratorState previousState = INITIATE;

TaskHandle_t mqttTaskHandle;
TaskHandle_t gameTaskHandle;
TaskHandle_t audioTaskHandle;

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

const uint32_t DFP_GAP_MS = 500;          // délai entre 2 trames
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

// ---------------------------------------------------------- SETUP -------------------------------------------------

void setup() {
  delay(1000);
  Serial.begin(115200);
  delay(1000);
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


  // Configuration des broches des interrupteurs en entrée
  for (int i = 0; i < 3; i++) {
    pinMode(switchPins[i], INPUT_PULLUP);  // Utilisation de résistances de pull-up internes
  }
  pinMode(MAG_SENSOR_1, INPUT_PULLUP);
  pinMode(MAG_SENSOR_2, INPUT_PULLUP);
  pinMode(POT1, INPUT);
  pinMode(POT2, INPUT);
  for (int i = 0; i < 3; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }
  for (int i = 0; i < 3; i++) {
    pinMode(ledbutton[i], OUTPUT);
    digitalWrite(ledbutton[i], LOW);
  }
  pinMode(RECV_PIN, INPUT);
  IrReceiver.begin(RECV_PIN);
  pinMode(KillerButon, INPUT_PULLUP);
  pinMode(KillerButonLED, OUTPUT);

  digitalWrite(KillerButonLED, LOW);

  delay(1000);

  audioQ = xQueueCreate(16, sizeof(AudioCmd));

  xTaskCreatePinnedToCore(mqttTask, "MQTTTask", 8192, NULL, 3, &mqttTaskHandle, 0);
  xTaskCreatePinnedToCore(generatorTask, "GeneratorTask", 4096, NULL, 2, &gameTaskHandle, 1);
  xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 1, &audioTaskHandle, 1);
}

void loop() {
}

// -------------------------------------------------------------------- GAME ------------------------------------------------------
void generatorTask(void* parameter) {
  while (true) {

    if (otaInProgress) {
      ring.clear();
      ring.show();
      digitalWrite(KillerButonLED, LOW);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    KillerLed = false;
    if (MessageOUT) {
      genState = OUT;
      MessageOUT = false;
    }
    if (MessageINIT) {
      genState = INITIATE;
      MessageINIT = false;
    }

    switch (genState) {
      case INITIATE:

        notifyMQTT(stateToString(genState));

        isWinningSwitch = false;
        SwitchGameWin = false;
        PotarGameWin = false;
        SeqBoutonGameWin = false;
        turnCount = 0;
        CountVictory = 0;
        startPartyTimer = millis();

        audioStop();

        ring.clear();
        vTaskDelay(50 / portTICK_PERIOD_MS);
        ring.show();

        genState = CHARGING;

        notifyMQTT(stateToString(genState));

        break;

      case CHARGING:

        ShockFunction();
        KillerFunction();
        ClearGameLed();
        CrocoLed();

        static unsigned long LastturnCount = 0;
        static bool capteur1Passe = false;
        static bool capteur2Passe = false;

        if (turnCount <= 0) turnCount = 0;

        // 🔹 Détection du capteur secondaire (doit être activé avant le principal)
        if (digitalRead(MAG_SENSOR_1) == 0 && !capteur2Passe) {  // Anti-rebond
          capteur2Passe = true;
          capteur1Passe = false;
          turnCount += 1;
          LastturnCount = millis();
          Serial.println("🔸 Capteur secondaire activé !");
        }

        if (digitalRead(MAG_SENSOR_2) == 0 && !capteur1Passe) {
          capteur1Passe = true;
          capteur2Passe = false;
          turnCount += 1;
          LastturnCount = millis();
          Serial.println("🔸 Capteur premier activé !");
        }

        if (millis() - LastturnCount > DechargeCharging && turnCount > 0) {
          turnCount -= 1;
          LastturnCount = millis();
        }

        ChargingLED(turnCount);
        // feedback audio

        if (turnCount >= NbrTour) {
          initSwitchGame = false;
          initPotarGame = false;
          initSeqBouton = false;
          StartGameState = millis();

          genState = GAME;

          notifyMQTT(stateToString(genState));
        }

        ring.show();

        break;

      case GAME:

        ShockFunction();
        KillerFunction();
        CrocoLed();

        if (turnCount <= 0) {

          genState = CHARGING;

          notifyMQTT(stateToString(genState));

        }

        else {

          if (millis() - StartGameState > DechargeGame) {
            turnCount -= 1;
            StartGameState = millis();
          }

          ChargedLED(turnCount);
          // feedback audio
          SwitchGame();
          PotarGame();
          SeqBoutonGame();

          if (millis() - blinkTimer < WaitSeqFail) blinkLEDs();  //Blink en cas d'échec de SeqBoutonGame
        }

        ring.show();

        break;

      case WAITING:
        {
          unsigned long currentPartyTime = millis() - startPartyTimer;
          int restTime = TotalTimeParty - currentPartyTime;
          if (restTime < 0) restTime = 0;
          waitingTime = restTime * (WaitingMx * 0.01);

          ShockFunction();
          KillerFunction();
          ClearGameLed();
          CrocoLed();

          turnCount = 0;

          if (CountVictory >= NbrVictory) {

            genState = FINISH;

            notifyMQTT(stateToString(genState));

          }

          else {
            if (millis() - startWaitingState > waitingTime) {

              genState = CHARGING;

              notifyMQTT(stateToString(genState));
            }

            WaitingLED(startWaitingState, waitingTime);

            // feedback audio
          }

          ring.show();

          break;
        }

      case FINISH:

        FinishLED();
        CrocoLed();
        // feedback audio

        ring.show();

        break;

      case SHOCK:

        if (millis() - ShockTimer > ShockTime) {
          turnCount = 0;

          genState = previousState;

          notifyMQTT(stateToString(genState));
        }

        ShockLED();
        ClearGameLed();
        CrocoLed();
        // feedback audio

        ring.show();

        break;

      case KILLERHOLD:

        KillerHoldLED();
        CrocGame();
        ClearGameLed();
        // feedback audio

        ring.show();

        break;

      case OUT:

        ring.clear();
        vTaskDelay(100 / portTICK_PERIOD_MS);
        ring.show();

        break;
    }
    if (!KillerLed) digitalWrite(KillerButonLED, LOW);
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}



// ----------------------------------------- UTILITAIRES  -------------------------------------------------------------
// ----------------------------------------------Gestion Jeux---------------------------------------------------------
void ShockFunction() {
  if (IrReceiver.decode()) {
    auto& d = IrReceiver.decodedIRData;
    // IrReceiver.printIRResultShort(&Serial);  // résumé propre (protocole, adresse, commande)
    if (d.address == 0x0000 && d.command == 0xA9) {
      if (genState != SHOCK) {
        previousState = genState;
        genState = SHOCK;
        ShockTimer = millis();

        notifyMQTT(stateToString(genState));
      }
    }
    IrReceiver.resume();
  }
}

void KillerFunction() {
  digitalWrite(KillerButonLED, HIGH);
  KillerLed = true;
  if (digitalRead(KillerButon) == LOW) {
    digitalWrite(KillerButonLED, LOW);
    initCroco = false;
    KillerLed = false;
    previousState = genState;

    genState = KILLERHOLD;

    notifyMQTT(stateToString(genState));
  }
}

void SwitchGame() {

  if (!initSwitchGame && !SwitchGameWin) {
    for (int i = 0; i < SizeSwitch; i++) {
      winningCombinationSwitch[i] = esp_random() % 2;
    }
    if (!isSame(winningCombinationSwitch, currentStateSwitch)) initSwitchGame = true;
  }

  if (initSwitchGame && !SwitchGameWin) {
    // Lecture de l'état actuel des interrupteurs
    for (int i = 0; i < SizeSwitch; i++) {
      currentStateSwitch[i] = digitalRead(switchPins[i]);
    }

    // Vérification si la combinaison actuelle correspond à la combinaison gagnante
    isWinningSwitch = true;
    for (int i = 0; i < SizeSwitch; i++) {
      if (currentStateSwitch[i] != winningCombinationSwitch[i]) {
        isWinningSwitch = false;
        break;
      }
    }

    if (isWinningSwitch) {
      CountVictory += 1;
      SwitchGameWin = true;
      startWaitingState = millis();

      SwitchLED();

      genState = WAITING;

      notifyMQTT(stateToString(genState));
      vTaskDelay(20 / portTICK_PERIOD_MS);
      notifyMQTTValue(CountVictory);
    }
  }

  // feedback audio
  SwitchLED();
}

bool isSame(int combo1[], int combo2[]) {
  for (int i = 0; i < SizeSwitch; i++) {
    if (combo1[i] != combo2[i]) {
      return false;
    }
  }
  return true;
}


void PotarGame() {

  if (!initPotarGame && !PotarGameWin) {
    targetValue1 = esp_random() % (4000 - 300) + 300;
    targetValue2 = esp_random() % (4000 - 300) + 300;

    if (abs(potValue1 - targetValue1) > tolerance || abs(potValue2 - targetValue2) > tolerance) {
      initPotarGame = true;
    }
  }

  if (!PotarGameWin && initPotarGame) {
    // Lecture des valeurs des potentiomètres
    potValue1 = analogRead(POT1);
    potValue2 = analogRead(POT2);

    // Vérification de la proximité avec les valeurs cibles
    bool isPot1Correct = abs(potValue1 - targetValue1) <= tolerance;
    bool isPot2Correct = abs(potValue2 - targetValue2) <= tolerance;

    // feedback audio

    if (isPot1Correct && isPot2Correct) {
      CountVictory += 1;
      PotarGameWin = true;
      startWaitingState = millis();
      PotarLED();

      genState = WAITING;

      notifyMQTT(stateToString(genState));
      vTaskDelay(100 / portTICK_PERIOD_MS);
      notifyMQTTValue(CountVictory);
    }
  }

  PotarLED();
}

void SeqBoutonGame() {
  static int currentPositionBouton = 0;
  static bool buttonValidated[SizeSeq] = { false, false, false };
  static bool buttonReleased[SizeSeq] = { true, true, true };
  int buttonIndex = -1;

  static int sequence[SizeSeq] = { 0, 1, 2 };  // Tableau initial avec les indices des boutons

  if (!initSeqBouton && !SeqBoutonGameWin) {
    // Mélanger le tableau pour obtenir une permutation aléatoire
    for (int i = 0; i < SizeSeq; i++) {
      int j = esp_random() % (SizeSeq - i) + i;  // Choisir un indice aléatoire entre i et 2
      int temp = sequence[i];
      sequence[i] = sequence[j];
      sequence[j] = temp;
    }

    // Copier la séquence mélangée dans la variable globale
    for (int i = 0; i < SizeSeq; i++) {
      winningSequenceBouton[i] = sequence[i];
      buttonValidated[i] = false;
      buttonReleased[i] = true;
      digitalWrite(ledbutton[i], LOW);
    }
    currentPositionBouton = 0;
    initSeqBouton = true;
  }

  // Réarme les boutons qui ont été relâchés
  for (int i = 0; i < SizeSeq; i++) {
    if (digitalRead(buttonPins[i]) == HIGH) {
      buttonReleased[i] = true;
    }
  }

  if (millis() - blinkTimer > WaitSeqFail && !SeqBoutonGameWin && initSeqBouton) {

    for (int i = 0; i < SizeSeq; i++) {
      if (digitalRead(buttonPins[i]) == LOW && !buttonValidated[i] && buttonReleased[i]) {
        buttonIndex = i;
        buttonReleased[i] = false;
        break;
      }
    }
  }

  // feedback audio
  SeqBoutonLED();

  if (buttonIndex == -1) {
    return;
  }

  if (buttonIndex == winningSequenceBouton[currentPositionBouton]) {
    digitalWrite(ledbutton[buttonIndex], HIGH);
    currentPositionBouton++;
    buttonValidated[buttonIndex] = true;

    if (currentPositionBouton == SizeSeq) {
      currentPositionBouton = 0;
      SeqBoutonGameWin = true;
      CountVictory += 1;
      startWaitingState = millis();

      SeqBoutonLED();

      genState = WAITING;

      notifyMQTT(stateToString(genState));
      vTaskDelay(100 / portTICK_PERIOD_MS);
      notifyMQTTValue(CountVictory);
    }
  } else {
    blinkTimer = millis();
    currentPositionBouton = 0;
    for (int i = 0; i < SizeSeq; i++) {
      buttonValidated[i] = false;
      buttonReleased[i] = false;
      digitalWrite(ledbutton[i], LOW);
    }
  }
}


void CrocGame() {
  static int targetCroco1;
  static int targetCroco2;
  static bool stableState1 = HIGH;  // État stable du capteur 1
  static bool stableState2 = HIGH;  // État stable du capteur 2
  static unsigned long lastStableTime1 = 0;
  static unsigned long lastStableTime2 = 0;
  const unsigned long debounceDelay = 200;  // 50 ms d'anti-rebond

  // Initialisation des cibles
  if (!initCroco) {
    targetCroco1 = esp_random() % NbrCroco;
    targetCroco2 = esp_random() % NbrCroco;
    if (targetCroco1 != targetCroco2) {
      initCroco = true;
    }
  }

  // Lecture des capteurs avec anti-rebond
  bool reading1 = (digitalRead(croco[targetCroco1]) == LOW);
  bool reading2 = (digitalRead(croco[targetCroco2]) == LOW);

  // Anti-rebond pour le capteur 1
  if (reading1 != stableState1) {
    lastStableTime1 = millis();
    stableState1 = reading1;
  }
  if (millis() - lastStableTime1 > debounceDelay) {
    reading1 = stableState1;  // On ne considère que l'état stable
  }

  // Anti-rebond pour le capteur 2
  if (reading2 != stableState2) {
    lastStableTime2 = millis();
    stableState2 = reading2;
  }
  if (millis() - lastStableTime2 > debounceDelay) {
    reading2 = stableState2;  // On ne considère que l'état stable
  }

  // Vérification des cibles (états stables)
  if (reading1 && reading2) {

    genState = previousState;  // Sortie de KILLERHOLD

    notifyMQTT(stateToString(genState));
  }
}
// ------------------------------------------------------- Animation LED-------------------------------------------
void ChargingLED(int TurnCount) {

  float pourcent = (float)TurnCount / (float)NbrTour;
  int numLedsPourcent = (int)floorf(pourcent * ANNEAU_LEDS_End);
  static int bright = 10;
  static bool direction = false;

  if (!direction) {
    bright++;
    if (bright >= 120) {
      direction = true;
      bright = 120;
    }
  } else if (direction) {
    bright--;
    if (bright <= 10) {
      direction = false;
      bright = 10;
    }
  }

  if (TurnCount == 0) {
    for (int i = 0; i < ANNEAU_LEDS_End; i++) {
      ring.setPixelColor(i, ring.Color(120, 0, 0));
    }
  } else {
    for (int i = 0; i < numLedsPourcent; i++) {
      ring.setPixelColor(i, ring.Color(0, 120, 0));
    }
    ring.setPixelColor(numLedsPourcent, ring.Color(bright, bright, bright));
    for (int i = numLedsPourcent + 1; i < ANNEAU_LEDS_End; i++) {
      ring.setPixelColor(i, ring.Color(120, 0, 0));
    }
  }
}

void ChargedLED(int TurnCount) {
  unsigned long now = millis();
  float pourcent = (float)TurnCount / (float)NbrTour;
  int numLedsPourcent = (int)floorf(pourcent * ANNEAU_LEDS_End);

  static unsigned long lastSpin = 0;
  static int spin = 0;

  // gestion pause
  static bool inPause = false;
  static unsigned long pauseStart = 0;
  const unsigned long pauseDuration = 1000;  // 1 seconde

  if (numLedsPourcent <= 0) {
    for (int i = 0; i < ANNEAU_LEDS_End; i++) {
      ring.setPixelColor(i, ring.Color(120, 0, 0));
    }
    return;
  }

  // recadrage si le nombre de leds diminue
  if (spin >= numLedsPourcent) {
    spin = numLedsPourcent - 1;
  }

  // gestion pause
  if (inPause) {
    if (now - pauseStart >= pauseDuration) {
      inPause = false;
      lastSpin = now;
    }
  } else {
    if (now - lastSpin >= 90) {
      lastSpin = now;

      spin--;

      if (spin < 0) {
        spin = numLedsPourcent - 1;

        // déclenche pause à la fin d’un tour
        inPause = true;
        pauseStart = now;
      }
    }
  }

  // affichage
  for (int i = 0; i < numLedsPourcent; i++) {
    uint8_t r = 0, g = 120, b = 0;

    if (!inPause && i == spin) {
      r = 120;
      g = 120;
      b = 120;
    }

    ring.setPixelColor(i, ring.Color(r, g, b));
  }

  for (int i = numLedsPourcent; i < ANNEAU_LEDS_End; i++) {
    ring.setPixelColor(i, ring.Color(120, 0, 0));
  }
}

void WaitingLED(unsigned long startMs, int durationMs) {
  unsigned long now = millis();
  if (durationMs <= 0) durationMs = 1;  // éviter division par 0

  // Progression du temps: 0.0 -> 1.0
  float t = (float)(now - startMs) / (float)durationMs;
  if (t < 0) t = 0;
  if (t > 1) t = 1;

  // Combien restent allumées (orange) ?
  int remaining = ANNEAU_LEDS_End - (int)floorf(t * ANNEAU_LEDS_End);
  if (remaining < 0) remaining = 0;
  if (remaining > ANNEAU_LEDS_End) remaining = ANNEAU_LEDS_End;

  // Spinner: LED plus lumineuse qui tourne parmi les LEDs restantes
  static unsigned long lastSpin = 0;
  static int spin = 0;
  if (now - lastSpin >= 90) {  // vitesse de rotation
    lastSpin = now;
    spin = (spin + 1) % ANNEAU_LEDS_End;
  }

  for (int i = 0; i < ANNEAU_LEDS_End; i++) {
    if (i < remaining) {
      // Orange normal
      uint8_t r = 120, g = 20, b = 0;
      // LED du spinner plus lumineuse
      if (i == spin) { g = 100; }  // rend l’orange plus vif
      ring.setPixelColor(i, ring.Color(r, g, b));
    } else {
      // Déchargée (éteinte) — ou très sombre si tu préfères un fond minimum:
      // ring.setPixelColor(idx, ring.Color(3, 2, 0)); // fond ultra-faible
      ring.setPixelColor(i, 0);
    }
  }
}

void FinishLED() {
}

void ShockLED() {
  const unsigned long blinkInt = 80;        // strobe de base
  const unsigned long glitchPeriod = 1000;  // flash global toutes les ~1s
  const unsigned long glitchWidth = 30;     // durée du flash global
  static unsigned long t = 0;
  static bool alt = false;
  static unsigned long burstT = 0;
  unsigned long now = millis();

  if (now - t >= blinkInt) {
    t = now;
    alt = !alt;
  }
  if (burstT == 0 || now - burstT >= glitchPeriod) burstT = now;

  // motif alterné rouge / off
  for (int i = 0; i < ANNEAU_LEDS_End; i++) {
    bool on = ((i & 1) == (alt ? 1 : 0));
    ring.setPixelColor(i, on ? ring.Color(255, 0, 0) : 0);
  }

  // 2-3 étincelles blanches aléatoires
  for (int s = 0; s < 3; s++) {
    int idx = esp_random() % ANNEAU_LEDS_End;
    ring.setPixelColor(idx, ring.Color(180, 180, 180));
  }

  // flash global ultra court (sensation "glitch")
  if (now - burstT < glitchWidth) {
    for (int i = 0; i < ANNEAU_LEDS_End; i++) ring.setPixelColor(i, ring.Color(255, 255, 255));
  }
}

void KillerHoldLED() {
  static int bright = 10;
  static bool direction = false;

  if (!direction) {
    bright++;
    if (bright >= 120) {
      direction = true;
      bright = 120;
    }
  } else if (direction) {
    bright--;
    if (bright <= 10) {
      direction = false;
      bright = 10;
    }
  }
  for (int i = CROCO_LEDS_Start; i < CROCO_LEDS_End; i++) {
    ring.setPixelColor(i, ring.Color(bright, 0, 0));
  }
}

// Led du jeu SeqBoutonSwitch
void blinkLEDs() {
  static bool blinkLedsOn = false;
  static unsigned long blinkDebounce = 0;

  if (millis() - blinkDebounce > 300 && !blinkLedsOn) {
    for (int i = 0; i < 3; i++) {
      digitalWrite(ledbutton[i], HIGH);
    }
    blinkDebounce = millis();
    blinkLedsOn = true;
  } else if (millis() - blinkDebounce > 300 && blinkLedsOn) {
    for (int i = 0; i < 3; i++) {
      digitalWrite(ledbutton[i], LOW);
    }
    blinkDebounce = millis();
    blinkLedsOn = false;
  }
}

void ClearGameLed() {

  if (!SwitchGameWin) {
    for (int i = SWITCH_LEDS_Start; i < SWITCH_LEDS_End; i++) {
      ring.setPixelColor(i, ring.Color(0, 0, 0));
    }
  }

  if (!SeqBoutonGameWin) {
    for (int i = SEQ_LEDS_Start; i < SEQ_LEDS_End; i++) {
      ring.setPixelColor(i, ring.Color(0, 0, 0));
    }
  }

  if (!PotarGameWin) {
    for (int i = POT_LEDS_Start; i < POT2_LEDS_End; i++) {
      ring.setPixelColor(i, ring.Color(0, 0, 0));
    }
  }
}

void SeqBoutonLED() {
  static int bright = 10;
  static bool direction = false;

  if (!SeqBoutonGameWin) {
    if (!direction) {
      bright++;
      if (bright >= 120) {
        direction = true;
        bright = 120;
      }
    } else if (direction) {
      bright--;
      if (bright <= 10) {
        direction = false;
        bright = 10;
      }
    }
    for (int i = SEQ_LEDS_Start; i < SEQ_LEDS_End; i++) {
      ring.setPixelColor(i, ring.Color(bright, 0, 0));
    }
  }

  else {
    for (int i = SEQ_LEDS_Start; i < SEQ_LEDS_End; i++) {
      ring.setPixelColor(i, ring.Color(0, 120, 0));
    }
  }
}

void SwitchLED() {
  static int bright = 10;
  static bool direction = false;

  if (!SwitchGameWin) {
    if (!direction) {
      bright++;
      if (bright >= 120) {
        direction = true;
        bright = 120;
      }
    } else if (direction) {
      bright--;
      if (bright <= 10) {
        direction = false;
        bright = 10;
      }
    }
    for (int i = SWITCH_LEDS_Start; i < SWITCH_LEDS_End; i++) {
      ring.setPixelColor(i, ring.Color(bright, 0, 0));
    }
  }

  else {
    for (int i = SWITCH_LEDS_Start; i < SWITCH_LEDS_End; i++) {
      ring.setPixelColor(i, ring.Color(0, 120, 0));
    }
  }
}

void PotarLED() {
  static int bright = 10;
  static bool direction = false;

  if (!PotarGameWin) {
    if (!direction) {
      bright++;
      if (bright >= 120) {
        direction = true;
        bright = 120;
      }
    } else if (direction) {
      bright--;
      if (bright <= 10) {
        direction = false;
        bright = 10;
      }
    }
    for (int i = POT_LEDS_Start; i < POT_LEDS_End; i++) {
      ring.setPixelColor(i, ring.Color(bright, 0, 0));
    }
  }

  else {
    for (int i = POT_LEDS_Start; i < POT_LEDS_End; i++) {
      ring.setPixelColor(i, ring.Color(0, 120, 0));
    }
  }
}

void CrocoLed() {
  for (int i = CROCO_LEDS_Start; i < CROCO_LEDS_End; i++) {
    ring.setPixelColor(i, ring.Color(0, 120, 0));
  }
}

// ---------------------------------------------------------------------AUDIO---------------------------------------------------------
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

//--------------------------------------------------------------------------------------------------------WIFI et BLE ----------------------------------------------------------------------------------------------------------------------------------------------
// Tâche MQTT
void mqttTask(void* parameter) {
  vTaskDelay(pdMS_TO_TICKS(5000));
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      connectToWiFi();
    }

    if (WiFi.status() == WL_CONNECTED && !client.connected()) {
      reconnectMQTT();
    }

    if (WiFi.status() == WL_CONNECTED && client.connected()) {
      if (!otaReady) initOTA();
      ArduinoOTA.handle();
      client.loop();
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// Connexion Wi-Fi
void connectToWiFi() {
  Serial.println("Connexion au Wi-Fi...");
  WiFi.disconnect(true, true);
  vTaskDelay(pdMS_TO_TICKS(2000));
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int wifiReconnectAttempts = 0;
  const int maxWifiReconnectAttempts = 30;

  while (WiFi.status() != WL_CONNECTED && wifiReconnectAttempts < maxWifiReconnectAttempts) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    Serial.print(".");
    wifiReconnectAttempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnecté au Wi-Fi");
    Serial.print("Adresse IP : ");
    Serial.println(WiFi.localIP());
    wifiReconnectAttempts = 0;
  } else {
    Serial.println("\nÉchec connexion Wi-Fi, reset interface Wi-Fi");
    resetWiFiOnly();
  }
}

// Reconnexion MQTT
void reconnectMQTT() {
  const char* willTopic = "esp32";
  char willMessage[64];
  char onlineMessage[64];

  snprintf(willMessage, sizeof(willMessage), "%s:offline", esp32_id);
  snprintf(onlineMessage, sizeof(onlineMessage), "%s:online", esp32_id);

  int mqttAttempts = 0;
  const int maxMqttAttempts = 6;

  while (!client.connected() && WiFi.status() == WL_CONNECTED && mqttAttempts < maxMqttAttempts) {
    Serial.println("Connexion au broker MQTT...");

    if (otaInProgress) return;

    if (client.connect(esp32_id, mqtt_user, mqtt_password, willTopic, 1, true, willMessage)) {
      Serial.println("Connecté au broker MQTT");
      client.publish(willTopic, onlineMessage, true);
      client.subscribe("unity/generator");
      return;
    } else {
      Serial.print("Échec, rc=");
      Serial.print(client.state());
      Serial.println(". Nouvelle tentative dans 5 secondes.");
      mqttAttempts++;
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
  }

  if (!client.connected()) {
    Serial.println("MQTT non reconnecté, retour boucle principale.");
    resetWiFiOnly();
  }
}

// Fonction pour redémarrer le Wi-Fi
void resetWiFiOnly() {
  otaReady = false;
  WiFi.disconnect(true, true);
  vTaskDelay(pdMS_TO_TICKS(2000));
  WiFi.mode(WIFI_STA);
}

void callback(char* topic, byte* payload, unsigned int length) {
  char msg[128];
  if (length >= sizeof(msg)) length = sizeof(msg) - 1;
  memcpy(msg, payload, length);
  msg[length] = '\0';

  Serial.printf("Message reçu sur %s : %s\n", topic, msg);

  char* first = strtok(msg, ":");
  char* second = strtok(NULL, ":");

  if (!first || !second) {
    return;
  }

  // 1) Commandes ciblées
  if (strcmp(first, esp32_id) == 0) {
    if (strcmp(second, "START") == 0) {
      MessageINIT = true;
      return;
    }
    if (strcmp(second, "OUT") == 0) {
      MessageOUT = true;
      return;
    }
  }

  if (strcmp(first, "ALL") == 0) {
    if (strcmp(second, "START") == 0) {
      MessageINIT = true;
      return;
    }
    if (strcmp(second, "OUT") == 0) {
      MessageOUT = true;
      return;
    }
  }

  // 2) Paramètres globaux
  int value = atoi(second);

  if (strcmp(first, "WaitingMx") == 0) {
    WaitingMx = constrain(value, 1, 100);

  } else if (strcmp(first, "NbrTour") == 0) {
    NbrTour = constrain(value, 1, 100);

  } else if (strcmp(first, "tolerance") == 0) {
    tolerance = constrain(value, 0, 4095);

  } else if (strcmp(first, "ShockTime") == 0) {
    ShockTime = constrain(value, 1000, 1000000);

  } else if (strcmp(first, "DechargeCharging") == 0) {
    DechargeCharging = constrain(value, 1000, 1000000);

  } else if (strcmp(first, "DechargeGame") == 0) {
    DechargeGame = constrain(value, 500, 1000000);

  } else if (strcmp(first, "WaitSeqFail") == 0) {
    WaitSeqFail = constrain(value, 100, 1000000);

  } else if (strcmp(first, "NbrVictory") == 0) {
    NbrVictory = constrain(value, 1, 10);

  } else if (strcmp(first, "TotalTimeParty") == 0) {
    TotalTimeParty = constrain(value, 1000, 10000000);
  }
}

// Notifier le serveur MQTT
void notifyMQTT(const char* message) {
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "%s:%s", esp32_id, message);
  client.publish("esp32", buffer);
}

void notifyMQTTValue(int value) {
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "%s:%d", esp32_id, value);
  client.publish("esp32", buffer);
}

void initOTA() {
  ArduinoOTA.setHostname(esp32_id);
  // Optionnel mais conseillé
  // ArduinoOTA.setPassword("CCK_OTA_2026");

  ArduinoOTA
    .onStart([]() {
      otaInProgress = true;

      Serial.println("\n[OTA] Début mise à jour");

      // On calme le système
      audioStop();
      ring.clear();
      ring.show();

      // Optionnel : signaler à Unity / broker
      notifyMQTT("OTA_START");

      // Si tu veux geler le gameplay pendant l'OTA :
      vTaskSuspend(gameTaskHandle);
    })
    .onEnd([]() {
      Serial.println("\n[OTA] Fin mise à jour");
      notifyMQTT("OTA_END");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("[OTA] Progression : %u%%\r", (progress * 100) / total);
    })
    .onError([](ota_error_t error) {
      Serial.printf("\n[OTA] Erreur [%u]\n", error);

      notifyMQTT("OTA_ERROR");

      otaInProgress = false;

      // Reprend le jeu si l’OTA a échoué
      vTaskResume(gameTaskHandle);

      if (error == OTA_AUTH_ERROR) Serial.println("[OTA] Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("[OTA] Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("[OTA] Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("[OTA] Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("[OTA] End Failed");
    });

  ArduinoOTA.begin();
  otaReady = true;

  Serial.print("[OTA] Prêt sur : ");
  Serial.println(esp32_id);
}
