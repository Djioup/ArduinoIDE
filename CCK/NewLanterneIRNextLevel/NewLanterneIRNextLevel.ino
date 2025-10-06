#include <WiFi.h>
#include <PubSubClient.h>
#include <IRremote.hpp>
#include <Adafruit_NeoPixel.h>
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

// ---------- IR ----------
#define IR_SEND_PIN 14
const uint16_t irIddle = 0x0000;  // code que le gilet reconnaît
const uint16_t irShoot = 0x0001;
const uint8_t irCmd = 0xA9;
int Cooldown = 5000;

// --- Flash blanc x3 (non bloquant) ---
bool flashing = false;
uint8_t flashCount = 0;            // nb de flashes terminés
uint8_t flashState = 0;            // 0 = BLANC allumé, 1 = éteint (retour rouge)
unsigned long flashTs = 0;         // chrono
const uint16_t FLASH_ON_MS = 60;   // durée du blanc
const uint16_t FLASH_OFF_MS = 60;  // durée entre deux blancs (rouge)
unsigned long lastIR = 0;
bool Led;

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

// ---------- FreeRTOS ----------
TaskHandle_t networkTaskHandle = nullptr;
TaskHandle_t irTaskHandle = nullptr;

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

inline void dfpPlay(uint8_t track) {
  // Appels DFPlayer rapides + petites pauses pour stabilité
  // myDFPlayer.volume(30);
  // vTaskDelay(30 / portTICK_PERIOD_MS);
  myDFPlayer.play(track);
}

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

  // DFPlayer (réglages uniquement)
  // ---------- DFPlayer ----------
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  vTaskDelay(1500 / portTICK_PERIOD_MS);
  if (!myDFPlayer.begin(Serial2)) {
    Serial.println("DFPlayer Mini non détecté !");
    while (true) { vTaskDelay(100 / portTICK_PERIOD_MS); }
  }
  Serial.println("DFPlayer Mini OK.");
  myDFPlayer.volume(30);
  delay(2000);
  myDFPlayer.play(4);
  delay(2000);

  // WiFi/MQTT config (structure inchangée)
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // Tasks FreeRTOS
  xTaskCreatePinnedToCore(networkTask, "NetworkTask", 4096, NULL, 1, &networkTaskHandle, 0);
  xTaskCreatePinnedToCore(irTask, "IrTask", 4096, NULL, 3, &irTaskHandle, 1);
}

void loop() {
  // Tout tourne en tasks; on ne fait rien ici.
}

// ============================== TASKS ==============================
void networkTask(void* parameter) {
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

    // tout en haut de la loop de irTask()
    static uint32_t dbTs = 0;
    static bool btn = HIGH, btnRaw = HIGH;

    btnRaw = (digitalRead(BUTTON_PIN) == LOW);  // actif bas
    if (btnRaw != btn) {                        // changement ?
      if (millis() - dbTs > 3) btn = btnRaw;    // anti-rebond ~3ms
    } else {
      dbTs = millis();
    }
    bool pressed = btn;


    // if (myDFPlayer.available()) {
    //   uint8_t type = myDFPlayer.readType();
    //   int value = myDFPlayer.read();
    //   if (type == DFPlayerPlayFinished) {
    //     myDFPlayer.play(4);
    //     vTaskDelay(400 / portTICK_PERIOD_MS);
    //     // la piste est terminée
    //   }
    // }


    if (millis() - lastIR > 100 && !Shooting && !isCharging) {
      IrSender.sendNEC(irIddle, irCmd, 1);  // 1 trame + 2 repeats NEC (identique à ton code)
      lastIR = millis();
    }
    // vTaskDelay(period);
    // if(digitalRead(BUTTON_PIN) == LOW)  Serial.println("BUTTON ok");
    if (millis() - LastShoot > Cooldown) {
      digitalWrite(BUTTON_LED, HIGH);
    } else {
      digitalWrite(BUTTON_LED, LOW);
    }

    if (millis() - NowS > 2000 && startVib && digitalRead(BUTTON_PIN) == HIGH) {
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

    // démarrage de charge si possible (pas en cooldown, pas déjà en charge)
   #include <WiFi.h>
#include <PubSubClient.h>
#include <IRremote.hpp>
#include <Adafruit_NeoPixel.h>
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

// ---------- IR ----------
#define IR_SEND_PIN 14
const uint16_t irIddle = 0x0000;  // code que le gilet reconnaît
const uint16_t irShoot = 0x0001;
const uint8_t irCmd = 0xA9;
int Cooldown = 5000;

// --- Flash blanc x3 (non bloquant) ---
bool flashing = false;
uint8_t flashCount = 0;            // nb de flashes terminés
uint8_t flashState = 0;            // 0 = BLANC allumé, 1 = éteint (retour rouge)
unsigned long flashTs = 0;         // chrono
const uint16_t FLASH_ON_MS = 60;   // durée du blanc
const uint16_t FLASH_OFF_MS = 60;  // durée entre deux blancs (rouge)
unsigned long lastIR = 0;
bool Led;

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

// ---------- FreeRTOS ----------
TaskHandle_t networkTaskHandle = nullptr;
TaskHandle_t irTaskHandle = nullptr;

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

inline void dfpPlay(uint8_t track) {
  // Appels DFPlayer rapides + petites pauses pour stabilité
  // myDFPlayer.volume(30);
  // vTaskDelay(30 / portTICK_PERIOD_MS);
  myDFPlayer.play(track);
}

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

  // DFPlayer (réglages uniquement)
  // ---------- DFPlayer ----------
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  vTaskDelay(1500 / portTICK_PERIOD_MS);
  if (!myDFPlayer.begin(Serial2)) {
    Serial.println("DFPlayer Mini non détecté !");
    while (true) { vTaskDelay(100 / portTICK_PERIOD_MS); }
  }
  Serial.println("DFPlayer Mini OK.");
  myDFPlayer.volume(30);
  delay(2000);
  myDFPlayer.play(4);
  delay(2000);

  // WiFi/MQTT config (structure inchangée)
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // Tasks FreeRTOS
  xTaskCreatePinnedToCore(networkTask, "NetworkTask", 4096, NULL, 1, &networkTaskHandle, 0);
  xTaskCreatePinnedToCore(irTask, "IrTask", 4096, NULL, 3, &irTaskHandle, 1);
}

void loop() {
  // Tout tourne en tasks; on ne fait rien ici.
}

// ============================== TASKS ==============================
void networkTask(void* parameter) {
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

    // tout en haut de la loop de irTask()
    static uint32_t dbTs = 0;
    static bool btn = HIGH, btnRaw = HIGH;

    btnRaw = (digitalRead(BUTTON_PIN) == LOW);  // actif bas
    if (btnRaw != btn) {                        // changement ?
      if (millis() - dbTs > 3) btn = btnRaw;    // anti-rebond ~3ms
    } else {
      dbTs = millis();
    }
    bool pressed = btn;


    // if (myDFPlayer.available()) {
    //   uint8_t type = myDFPlayer.readType();
    //   int value = myDFPlayer.read();
    //   if (type == DFPlayerPlayFinished) {
    //     myDFPlayer.play(4);
    //     vTaskDelay(400 / portTICK_PERIOD_MS);
    //     // la piste est terminée
    //   }
    // }


    if (millis() - lastIR > 100 && !Shooting && !isCharging) {
      IrSender.sendNEC(irIddle, irCmd, 1);  // 1 trame + 2 repeats NEC (identique à ton code)
      lastIR = millis();
    }
    // vTaskDelay(period);
    // if(digitalRead(BUTTON_PIN) == LOW)  Serial.println("BUTTON ok");
    if (millis() - LastShoot > Cooldown) {
      digitalWrite(BUTTON_LED, HIGH);
    } else {
      digitalWrite(BUTTON_LED, LOW);
    }

    if (millis() - NowS > 2000 && startVib && digitalRead(BUTTON_PIN) == HIGH) {
      StopVibration();
      startVib = false;
    }

    if (pressed && millis() - LastShoot > Cooldown) {
      LastShoot = millis();
      Shooting = true;
      digitalWrite(BUTTON_LED, LOW);
      Vibration (255);
      doShoot();
    }

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

    // démarrage de charge si possible (pas en cooldown, pas déjà en charge)
    // if (!isCharging && (millis() - LastShoot > Cooldown) && pressed) {
    //   isCharging = true;
    //   chargeStart = millis();
    // }

    // // progression de charge (UI) ou annulation
    // if (isCharging) {
    //   unsigned long elapsed = millis() - chargeStart;

    //   // si on relâche AVANT la fin -> annule
    //   if (!pressed) {
    //     isCharging = false;
    //     ring.fill(ring.Color(255, 0, 0), 0, NUM_LEDS);
    //     ring.show();  // retour rouge
    //     StopVibration();
    //   } else {
    //     // MAJ UI
    //     float t = (float)elapsed / (float)CHARGE_MS;
    //     if (t > 1.0f) t = 1.0f;
    //     drawChargeProgress(t);
    //     // vibration en fonction de la progression
    //     if (pressed) chargeVibration(t);

    //     startVib = true;
    //     NowS = millis();
    //     digitalWrite(BUTTON_LED, LOW);



    //     // charge complète -> TIR auto (tant que le bouton reste appuyé)
    //     if (elapsed >= CHARGE_MS) {
    //       isCharging = false;
    //       Shooting = true;
    //       LastShoot = millis();
    //       doShoot();
    //       StopVibration();
    //       Shooting = false;
    //       // ring.fill(ring.Color(255, 0, 0), 0, NUM_LEDS);
    //       // ring.show();  // retour rouge
    //     }
    //   }
    // }

    // // mémorise l'état (utile si tu veux déclencher au relâchement)
    // prevPressed = pressed;


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
      String topic = String("esp32/lanterne");
      client.subscribe(topic.c_str());
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
    myDFPlayer.play(3);
  // petit feedback visuel (3 flashes blancs rapides, bloquant ~360 ms)
  for (int i = 0; i < 3; i++) {
    ring.fill(ring.Color(255, 255, 255), 0, NUM_LEDS);
    ring.show();
    digitalWrite(BUTTON_LED, HIGH);
    vTaskDelay(60 / portTICK_PERIOD_MS);
    ring.fill(ring.Color(255, 0, 0), 0, NUM_LEDS);
    ring.show();
    digitalWrite(BUTTON_LED, LOW);
    vTaskDelay(60 / portTICK_PERIOD_MS);    
  }
  
  // envoi IR tir
  IrSender.sendNEC(irShoot, irCmd, 3);
  Shooting = false;
  StopVibration();
}

// void drawChargeProgress(float t) {  // t: 0.0 -> 1.0
//   int lit = (int)(t * NUM_LEDS);
//   if (lit < 0) lit = 0;
//   if (lit > NUM_LEDS) lit = NUM_LEDS;
//   for (int i = 0; i < NUM_LEDS; i++) {
//     if (i < lit) ring.setPixelColor(i, ring.Color(255, 255, 255));  // blanc = charge
//     else ring.setPixelColor(i, ring.Color(255, 0, 0));              // rouge = fond
//   }
//   ring.show();
// }

// void chargeVibration(float t) {
//   // t = 0.0 (début) -> vibration faible, t = 1.0 (plein) -> forte
//   int pwm = (int)(t * 255) + 80;
//   if (pwm > 255) pwm = 255;
//   if (digitalRead(BUTTON_PIN) == LOW) analogWrite(MOTOR_PIN, pwm);
// }



// ============================== CALLBACK (structure inchangée) ==============================
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("Reçu sur " + String(topic) + ": " + msg);

  if (msg.endsWith("FIND")) {
    if (!isCharging) Vibration(200);
    NowS = millis();
    startVib = true;
  }

  if (msg.endsWith("LOST")) {
    StopVibration();
  }

  if (msg.endsWith("TICK")) {
    myDFPlayer.play(2);
    Vibration(255);
  }

  if (msg.endsWith("HIT")) {
    myDFPlayer.play(3);
    Vibration(255);
  }

  // Tu feras ce que tu veux ici plus tard (FIND/TICK/HIT, etc.)
  // Pour l’instant : aucun changement de fonctionnalité.
}



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
      String topic = String("esp32/lanterne");
      client.subscribe(topic.c_str());
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
  IrSender.sendNEC(irShoot, irCmd, 3);
  myDFPlayer.play(3);
  // petit feedback visuel (3 flashes blancs rapides, bloquant ~360 ms)
  for (int i = 0; i < 3; i++) {
    ring.fill(ring.Color(255, 255, 255), 0, NUM_LEDS);
    ring.show();
    vTaskDelay(60 / portTICK_PERIOD_MS);
    ring.fill(ring.Color(255, 0, 0), 0, NUM_LEDS);
    ring.show();
    vTaskDelay(60 / portTICK_PERIOD_MS);
  }
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



// ============================== CALLBACK (structure inchangée) ==============================
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("Reçu sur " + String(topic) + ": " + msg);

  if (msg.endsWith("FIND")) {
    if (!isCharging) Vibration(200);
    NowS = millis();
    startVib = true;
  }

  if (msg.endsWith("LOST")) {
    StopVibration();
  }

  if (msg.endsWith("TICK")) {
    myDFPlayer.play(2);
    Vibration(255);
  }

  if (msg.endsWith("HIT")) {
    myDFPlayer.play(3);
    Vibration(255);
  }

  // Tu feras ce que tu veux ici plus tard (FIND/TICK/HIT, etc.)
  // Pour l’instant : aucun changement de fonctionnalité.
}
