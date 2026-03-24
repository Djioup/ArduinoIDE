#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_NeoPixel.h>
#include <IRremote.hpp>

// ------------------------------------------------------------ INIT ------------------------------------------------------------------------------
// WIFI
const char* ssid = "PelucheGang";
const char* password = "CACHE-CACHEKILLER";
const char* mqtt_server = "192.168.0.139";
const char* mqtt_user = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";
const char* esp32_id = "Generator0";

WiFiClient espClient;
PubSubClient client(espClient);

// Variables pour gérer les reconnexions
const int maxReconnectAttempts = 5;  // Seuil avant de redémarrer le Wi-Fi
int reconnectAttempts = 0;           // Compteur d'échecs de reconnexion

// DFPlayer Mini Configuration
#define RX_PIN 16
#define TX_PIN 17
DFRobotDFPlayerMini myDFPlayer;

// variables Contrôles
int waitingTime = 30000;
int ShockTime = 30000;
int DechargeCharging = 10000;
int DechargeGame = 2000;
int NbrTour = 12;

// variables
int turnCount = 0;
int lastAnnouncedTurn = 0;
int winningCombinationSwitch[3] = { 0, 0, 0 };
int currentStateSwitch[3] = { 0, 0, 0 };
int CountVictory = 0;
int targetValue1;
int targetValue2;
int potValue1;
int potValue2;
int winningSequenceBouton[3] = { 0, 0, 0 };
int currentPositionBouton = 0;

const int tolerance = 600;

unsigned long blinkDebounce = 0;
unsigned long buttonDebounce = 0;
unsigned long blinkTimer = 0;
unsigned long LastturnCount = 0;
unsigned long StartGameState = 0;
unsigned long startWaitingState = 0;
unsigned long ShockTimer = 0;

bool capteur1Passe = false;
bool capteur2Passe = false;
bool isWinningSwitch = false;
bool PotarGameWin = false;
bool SeqBoutonGameWin = false;
bool blinkLedsOn = false;
bool buttonPressed[3] = { false, false, false };
bool Shock = false;

//GPIO
const int MAG_SENSOR_1 = 27;
const int MAG_SENSOR_2 = 14;
const int switchPins[] = { 18, 19, 21 };
const int buttonPins[] = { 12, 13, 14 };
const int ledbutton[] = { 25, 26, 27 };
static const uint8_t RECV_PIN = 4;
#define POT1 34
#define POT2 35

// ADAFRUIT
#define ANNEAULED 4
#define NUM_LEDS 12
Adafruit_NeoPixel ring(NUM_LEDS, ANNEAULED, NEO_GRB + NEO_KHZ800);

// FREERTOS
enum GeneratorState {
  INITIATE,
  CHARGING,
  GAME,
  WAITING,
  FINISH,
  SHOCK,
  KILLERHOLD
};

GeneratorState genState = INITIATE;
GeneratorState previousState;

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

// Struct
// Etat SHOCK
struct ShockAnim {
  unsigned long t = 0;
  bool alt = false;
  unsigned long burstT = 0;
} shockAnim;

// ---------------------------------------------------------- SETUP -------------------------------------------------

void setup() {
  delay(1000);
  Serial.begin(115200);
  delay(1000);
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(1000);

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

  delay(1000);

  audioQ = xQueueCreate(16, sizeof(AudioCmd));

  xTaskCreatePinnedToCore(mqttTask, "MQTTTask", 8192, NULL, 3, &mqttTaskHandle, 0);
  xTaskCreatePinnedToCore(generatorTask, "GeneratorTask", 8192, NULL, 2, &gameTaskHandle, 1);
  xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 1, &audioTaskHandle, 1);
}

void loop() {
}

// -------------------------------------------------------------------- GAME ------------------------------------------------------
void generatorTask(void* parameter) {
  while (true) {

    if (IrReceiver.decode()) {
      auto& d = IrReceiver.decodedIRData;
      // IrReceiver.printIRResultShort(&Serial);  // résumé propre (protocole, adresse, commande)
      if (d.address == 0x0000 && d.command == 0xA9) {
        if (!Shock && genState != FINISH) {
          previousState = genState;
          genState = SHOCK;
          ShockTimer = millis();
        }
      }
    }

    switch (genState) {
      case INITIATE:
        {

          isWinningSwitch = false;
          PotarGameWin = false;
          SeqBoutonGameWin = false;
          Shock = false;

          do {
            for (int i = 0; i < 3; i++) {
              winningCombinationSwitch[i] = esp_random() % 2;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
          } while (isSame(winningCombinationSwitch, currentStateSwitch));


          do {
            targetValue1 = esp_random() % (4000 - 300) + 300;
            vTaskDelay(10 / portTICK_PERIOD_MS);
          } while (abs(potValue1 - targetValue1) >= tolerance);
          do {
            targetValue2 = esp_random() % (4000 - 300) + 300;
            vTaskDelay(10 / portTICK_PERIOD_MS);
          } while (abs(potValue2 - targetValue2) >= tolerance);

          int sequence[3] = { 0, 1, 2 };  // Tableau initial avec les indices des boutons

          // Mélanger le tableau pour obtenir une permutation aléatoire
          for (int i = 0; i < 3; i++) {
            int j = esp_random() % (3 - i) + i;  // Choisir un indice aléatoire entre i et 2
            int temp = sequence[i];
            sequence[i] = sequence[j];
            sequence[j] = temp;
          }

          // Copier la séquence mélangée dans la variable globale
          for (int i = 0; i < 3; i++) {
            winningSequenceBouton[i] = sequence[i];
          }

          genState = CHARGING;

          break;
        }
      case CHARGING:
        if (turnCount < 0) turnCount = 0;

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
          StartGameState = millis();
          genState = GAME;
        }

        break;

      case GAME:

        if (turnCount <= 0) genState = CHARGING;

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

          if (millis() - blinkTimer < 3000) blinkLEDs();  //Blink en cas d'échec de SeqBoutonGame
        }

        break;

      case WAITING:

        turnCount = 0;

        if (CountVictory >= 3) genState = FINISH;

        else {
          if (millis() - startWaitingState > waitingTime) genState = CHARGING;

          WaitingLED(startWaitingState, waitingTime);

          // feedback audio
        }

        break;

      case FINISH:

        Shock = true;
        notifyMQTT("generateur reparer");
        FinishLED();
        // feedback audio

        break;

      case SHOCK:

        Shock = true;

        if (millis() - ShockTimer > ShockTime) {
          Shock = false;
          genState = previousState;
          turnCount = 0;
        }

        ShockLED();
        // feedback audio

        break;

      case KILLERHOLD:

        KillerHoldLED();

        // feedback audio

        break;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}



// ----------------------------------------- UTILITAIRES  -------------------------------------------------------------
// ----------------------------------------------Gestion Jeux---------------------------------------------------------
void SwitchGame() {
  if (!isWinningSwitch) {
    // Lecture de l'état actuel des interrupteurs
    for (int i = 0; i < 3; i++) {
      currentStateSwitch[i] = digitalRead(switchPins[i]);
    }

    // Vérification si la combinaison actuelle correspond à la combinaison gagnante
    isWinningSwitch = true;
    for (int i = 0; i < 3; i++) {
      if (currentStateSwitch[i] != winningCombinationSwitch[i]) {
        isWinningSwitch = false;
        break;
      }
    }
    // feedback audio
    // feedback led


    if (isWinningSwitch) {
      CountVictory += 1;
      genState = WAITING;
      startWaitingState = millis();
    }
  }
}

bool isSame(int combo1[], int combo2[]) {
  for (int i = 0; i < 3; i++) {
    if (combo1[i] != combo2[i]) {
      return false;
    }
  }
  return true;
}


void PotarGame() {
  if (!PotarGameWin) {
    // Lecture des valeurs des potentiomètres
    potValue1 = analogRead(POT1);
    potValue2 = analogRead(POT2);

    // Vérification de la proximité avec les valeurs cibles
    bool isPot1Correct = abs(potValue1 - targetValue1) <= tolerance;
    bool isPot2Correct = abs(potValue2 - targetValue2) <= tolerance;

    // feedback audio
    // feedback led

    if (isPot1Correct && isPot2Correct) {
      CountVictory += 1;
      PotarGameWin = true;
      genState = WAITING;
      startWaitingState = millis();
    } else if (isPot1Correct || isPot2Correct) {

    } else {
    }
  }
}

void SeqBoutonGame() {
  int buttonIndex = -1;
  if (millis() - blinkTimer > 3000 && !SeqBoutonGameWin) {
    for (int i = 0; i < 3; i++) {
      if (digitalRead(buttonPins[i]) == LOW) {
        buttonDebounce = millis();
        if (millis() - buttonDebounce > 100 && digitalRead(buttonPins[i]) == LOW) {
          buttonPressed[i] = true;
        }
      }
      if (digitalRead(buttonPins[i]) == HIGH && buttonPressed[i]) {
        buttonPressed[i] = false;
        buttonIndex = i;
      } else buttonPressed[i] = false;
    }

    // feedback audio
    // feedback led


    if (buttonIndex == winningSequenceBouton[currentPositionBouton]) {
      digitalWrite(ledbutton[buttonIndex], HIGH);
      currentPositionBouton++;

      if (currentPositionBouton == 3) {
        currentPositionBouton = 0;
        SeqBoutonGameWin = true;
        CountVictory += 1;
        genState = WAITING;
        startWaitingState = millis();
      }
    } else {
      blinkTimer = millis();
      currentPositionBouton = 0;
    }
  }
}

// ------------------------------------------------------- Animation LED-------------------------------------------
void ChargingLED(int TurnCount) {

  float pourcent = TurnCount / NbrTour;
  int numLedsPourcent = pourcent * NUM_LEDS;

  if (TurnCount == 0) {
    for (int i = 0; i < NUM_LEDS; i++) {
      ring.setPixelColor(i, ring.Color(120, 0, 0));
    }
  } else {
    for (int i = 0; i < numLedsPourcent; i++) {
      ring.setPixelColor(i, ring.Color(120, 60, 0));
    }
    for (int i = numLedsPourcent + 1; i < NUM_LEDS; i++) {
      ring.setPixelColor(i, ring.Color(0, 0, 0));
    }
  }
  ring.show();  
}

void ChargedLED(int TurnCount) {

  float pourcent = TurnCount / NbrTour;
  int numLedsPourcent = pourcent * NUM_LEDS;

  for (int i = 0; i < numLedsPourcent; i++) {
    ring.setPixelColor(i, ring.Color(0, 120, 0));
  }
  for (int i = numLedsPourcent + 1; i < NUM_LEDS; i++) {
    ring.setPixelColor(i, ring.Color(0, 0, 0));
  }
  ring.show();
}

void WaitingLED(unsigned long startMs, int durationMs) {
  unsigned long now = millis();
  if (durationMs == 0) durationMs = 1;  // éviter division par 0

  // Progression du temps: 0.0 -> 1.0
  float t = (float)(now - startMs) / (float)durationMs;
  if (t < 0) t = 0;
  if (t > 1) t = 1;

  // Combien restent allumées (orange) ?
  int remaining = NUM_LEDS - (int)floorf(t * NUM_LEDS);
  if (remaining < 0) remaining = 0;
  if (remaining > NUM_LEDS) remaining = NUM_LEDS;

  // Spinner: LED plus lumineuse qui tourne parmi les LEDs restantes
  static unsigned long lastSpin = 0;
  static int spin = 0;
  if (now - lastSpin >= 90) {  // vitesse de rotation
    lastSpin = now;
    spin = (spin + 1) % NUM_LEDS;
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    if (i < remaining) {
      // Orange normal
      uint8_t r = 255, g = 20, b = 0;
      // LED du spinner plus lumineuse
      if (i == spin) { g = 100; }  // rend l’orange plus vif
      ring.setPixelColor(i, ring.Color(r, g, b));
    } else {
      // Déchargée (éteinte) — ou très sombre si tu préfères un fond minimum:
      // ring.setPixelColor(idx, ring.Color(3, 2, 0)); // fond ultra-faible
      ring.setPixelColor(i, 0);
    }
  }

  ring.show();
}

void FinishLED() {
}

void ShockLED() {
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
    ring.setPixelColor(i, on ? ring.Color(255, 0, 0) : 0);
  }

  // 2-3 étincelles blanches aléatoires
  for (int s = 0; s < 3; s++) {
    int idx = esp_random() % NUM_LEDS;
    ring.setPixelColor(idx, ring.Color(180, 180, 180));
  }

  // flash global ultra court (sensation "glitch")
  if (now - shockAnim.burstT < glitchWidth) {
    for (int i = 0; i < NUM_LEDS; i++) ring.setPixelColor(i, ring.Color(255, 255, 255));
  }

  ring.show();
}

void KillerHoldLED() {
}

// Led du jeu SeqBoutonSwitch
void blinkLEDs() {
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
  if (message.endsWith("RESET")) {
    Serial.println("Commande de reset reçue de Unity.");
    genState = INITIATE;
  }

  if (message.endsWith("SABOTAGE")) {
    Serial.println("Sabotage recu");
  }

  if (message.endsWith("PHASE3_ON")) {
    Serial.println("PHASE3_ON");
  }

  if (message.endsWith("Door")) {
    audioStop();
    audioLoop(15);
  }



  if (message.startsWith(String("Generator:difficulte("))) {
    int startIdx = message.indexOf("(") + 1;
    int endIdx = message.indexOf(")");
    String difficultyStr = message.substring(startIdx, endIdx);
    float difficultyLevel = difficultyStr.toFloat();

    Serial.print("Niveau de difficulté reçu : ");
    Serial.println(difficultyLevel);


    // applyDifficultyKeepPercent(difficultyLevel);  // <-- garde le %
    Serial.print("Niveau de difficulté appliqué : ");
    // Serial.println(diffLvl);
  }

  if (message == String(esp32_id) + ":generateur reparer") {
  }

  if (message == String(esp32_id) + ":generateur lvl 1") {
  }

  if (message == String(esp32_id) + ":generateur lvl 2") {
  }

  if (message == String(esp32_id) + ":generateur lvl 3") {
  }

  if (message == String(esp32_id) + ":inactif") {
  }

  if (message == String(esp32_id) + ":actif") {
  }

  if (message.startsWith("GENERATORREG=")) {
    String s = message.substring(strlen("GENERATORREG="));  // ex: "4"
    int t = s.toInt();

    // garde-fou
    if (t < 0) t = 0;

    // RegDiff = t;
  }
}


// Notifier le serveur MQTT
void notifyMQTT(const char* message) {
  String fullMessage = String(esp32_id) + ":" + message;  // Préfixer le message par l'identifiant
  client.publish("esp32/donnees", fullMessage.c_str());
}
