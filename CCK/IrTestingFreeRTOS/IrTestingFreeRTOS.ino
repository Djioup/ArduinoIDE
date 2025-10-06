#include <WiFi.h>
#include <PubSubClient.h>
#include <IRremote.hpp>
#include <Adafruit_NeoPixel.h>
#include <DFRobotDFPlayerMini.h>

// ---------- Wi-Fi ----------
const char* ssid          = "PelucheGang";
const char* password      = "CACHE-CACHEKILLER";
const char* mqtt_server   = "192.168.0.139";
const char* mqtt_user     = "DjiooDanTae";
const char* mqtt_password = "DjioopPod";

// ---------- Identité ----------
const int lanternID = 1;   // <-- change pour chaque lanterne
const char* esp32_id = "Lanterne1";

// ---------- IR ----------
#define IR_SEND_PIN 14
const uint16_t irAddr = 0x0000;  // code que le gilet reconnaît
const uint8_t  irCmd  = 0xA9;

// ---------- LED Ring ----------
#define RING_PIN 27
#define NUM_LEDS 36
Adafruit_NeoPixel ring(NUM_LEDS, RING_PIN, NEO_GRB + NEO_KHZ800);

// ---------- DFPlayer ----------
#define RX_PIN 16
#define TX_PIN 17
DFRobotDFPlayerMini myDFPlayer;
const uint8_t TRACK_FIND = 1;
const uint8_t TRACK_TICK = 2;
const uint8_t TRACK_HIT  = 3;

// ---------- MQTT ----------
WiFiClient espClient;
PubSubClient client(espClient);

// ---------- Protos ----------
void connectToWiFi();
void reconnectMQTT();
void callback(char* topic, byte* payload, unsigned int length);

inline void dfpPlay(uint8_t track){
  myDFPlayer.volume(30);
  delay(30);
  myDFPlayer.play(track);
}

// ============================== SETUP ==============================
void setup() {
  Serial.begin(115200);

  // IR
  IrSender.begin(IR_SEND_PIN);

  // LED -> ROUGE FIXE
  ring.begin();
  ring.setBrightness(255);
  ring.fill(ring.Color(255, 0, 0), 0, NUM_LEDS);
  ring.show();

  // DFPlayer
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(1500);
  if (!myDFPlayer.begin(Serial2)) {
    Serial.println("DFPlayer Mini non détecté !");
    while (true) { delay(100); }
  }
  Serial.println("DFPlayer Mini OK.");
  myDFPlayer.volume(30);

  // WiFi/MQTT config
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // connectToWiFi();
  // reconnectMQTT();
}

// ============================== LOOP ==============================
void loop() {
  // Réseau
  // if (!client.connected()) {
  //   reconnectMQTT();
  // }
  // client.loop();

  // Émission IR continue
  IrSender.sendNEC(irAddr, irCmd, 2);
  delay(120);  // tempo identique à ton sketch simple
}

// ============================== HELPERS ==============================
void connectToWiFi() {
  Serial.print("Connexion WiFi à "); Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnecté au WiFi");
  Serial.print("IP : "); Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connexion MQTT...");
    if (client.connect(esp32_id, mqtt_user, mqtt_password)) {
      Serial.println("Connecté");
      String topic = String("esp32/lanterne/") + String(lanternID) + "/event";
      client.subscribe(topic.c_str());
      Serial.println("Sub sur " + topic);
    } else {
      Serial.print("Échec MQTT, rc=");
      Serial.print(client.state());
      Serial.println(" retry dans 5s");
      delay(5000);
    }
  }
}

// ============================== CALLBACK ==============================
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("Reçu sur " + String(topic) + ": " + msg);

  if (msg.indexOf("FIND") >= 0) {
    dfpPlay(TRACK_FIND);
  }
  if (msg.indexOf("TICK") >= 0) {
    dfpPlay(TRACK_TICK);
  }
  else if (msg.indexOf("HIT") >= 0) {
    dfpPlay(TRACK_HIT);
  }
}
