#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include <NimBLEDevice.h>

// -------------------- Réseau --------------------
const char* ssid      = "PelucheGang";
const char* password  = "CACHE-CACHEKILLER";
const char* mqtt_server = "192.168.0.139";
const char* mqtt_user = "DjiooDanTae";
const char* mqtt_pass = "DjioopPod";
const char* esp32_id  = "HealBorne1";

// -------------------- MQTT ----------------------
WiFiClient espClient;
PubSubClient client(espClient);
static const char* TOPIC = "unity/commandes";   // topic unique de sortie

// -------------------- NimBLE --------------------
,nfrrrèyu vb|

// -------------------- Détection présence --------
struct Track {
  String key;         // "PlayerX"
  bool inE3 = false;  // armé (= déjà envoyé Healing) tant que présent
  int rssi = -127;
  unsigned long lastSeen = 0;
};

static Track tracks[16];
static const unsigned long STALE_MS   = 4000;
static const int           RSSI_ENTER = -80;
static const int           RSSI_EXIT  = -100;

// -------------------- Protos --------------------
void connectToWiFi();
void reconnectMQTT();
void mqttTask(void*);
void bleScanTask(void*);
void publishHealing(const String& name);
void publishStopHealing(const String& name);

// -------------------- Utils tracking ------------
static int findByKey(const String& k) {
  for (int i = 0; i < (int)(sizeof(tracks)/sizeof(tracks[0])); ++i)
    if (tracks[i].key == k) return i;
  return -1;
}
static int allocSlot() {
  int freeIdx = -1;
  unsigned long oldest = ULONG_MAX;
  int oldestIdx = -1;
  for (int i = 0; i < (int)(sizeof(tracks)/sizeof(tracks[0])); ++i) {
    if (tracks[i].key.length() == 0) { freeIdx = i; break; }
    if (tracks[i].lastSeen < oldest) { oldest = tracks[i].lastSeen; oldestIdx = i; }
  }
  return (freeIdx != -1) ? freeIdx : oldestIdx;
}

// -------------------- Callbacks scan ------------
class MyScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* adv) override {
    if (!adv->haveName()) return;

    String name = String(adv->getName().c_str());
    if (!name.startsWith("Player")) return;

    // "Player:2" -> "Player2"
    name.replace(":", "");

    // Lire E:3 depuis ManufacturerData
    int eVal = -1;
    if (adv->haveManufacturerData()) {
      std::string md = adv->getManufacturerData();
      String s(md.c_str());
      int eIdx = s.indexOf("E:");
      if (eIdx != -1) eVal = s.substring(eIdx + 2).toInt();
    }
    if (eVal != 3) return;

    int rssi = adv->getRSSI();
    unsigned long now = millis();

    int idx = findByKey(name);
    if (idx < 0) {
      idx = allocSlot();
      tracks[idx] = Track{};
      tracks[idx].key = name;
    }

    bool wasIn = tracks[idx].inE3;
    tracks[idx].rssi = rssi;
    tracks[idx].lastSeen = now;

    // Front d'entrée -> envoyer "PlayerX:Healing"
    if (!wasIn && rssi > RSSI_ENTER) {
      tracks[idx].inE3 = true;
      publishHealing(name);
    }

    // Sortie (hystérésis) -> réarmer + envoyer StopHealing si tu le veux
    if (wasIn && rssi < RSSI_EXIT) {
      tracks[idx].inE3 = false;
      publishStopHealing(name);
    }
  }

  // ⚠ SUPPRIMÉ: onScanEnd(...) override — inutile et source d'erreur selon versions de lib
} gScanCb;

// -------------------- Setup ---------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  // Wi-Fi + MQTT
  connectToWiFi();
  client.setServer(mqtt_server, 1883);
  client.setCallback([](char*, byte*, unsigned int){ /* rien à écouter */ });

  // NimBLE
  NimBLEDevice::init("");
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(&gScanCb);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(SCAN_INT_MS);
  pBLEScan->setWindow(SCAN_WIN_MS);
  // pBLEScan->setDuplicateFilter(true); // optionnel

  // Tâches
  xTaskCreatePinnedToCore(mqttTask, "MQTT", 4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(bleScanTask, "BLE",  4096, NULL, 1, NULL, 0);

  Serial.println("[PresenceBorne] ready");
}

void loop() {
  // tout tourne dans les tâches
}

// -------------------- Tâches --------------------
void mqttTask(void*) {
  for (;;) {
    if (!client.connected()) reconnectMQTT();
    client.loop();

    // Expiration -> réarmement + StopHealing (cohérent côté gameplay)
    unsigned long now = millis();
    for (int i = 0; i < (int)(sizeof(tracks)/sizeof(tracks[0])); ++i) {
      if (tracks[i].key.length() == 0) continue;
      if (tracks[i].inE3 && (now - tracks[i].lastSeen) > STALE_MS) {
        tracks[i].inE3 = false;
        publishStopHealing(tracks[i].key);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void bleScanTask(void*) {
  vTaskDelay(pdMS_TO_TICKS(1000));
  for (;;) {
    // Scan "blocking" court, puis clear
    NimBLEScanResults res = pBLEScan->getResults(SCAN_TIME_MS, false);
    (void)res;
    pBLEScan->clearResults();
    vTaskDelay(pdMS_TO_TICKS(80));
  }
}

// -------------------- Réseau/MQTT ---------------
void connectToWiFi() {
  Serial.print("Wi-Fi... ");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(300));
    Serial.print(".");
  }
  Serial.print(" OK @ ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("MQTT...");
    String willTopic   = String("esp32/") + esp32_id + "/status";
    String willPayload = String(esp32_id) + ":offline";
    if (client.connect(esp32_id, mqtt_user, mqtt_pass, willTopic.c_str(), 1, true, willPayload.c_str())) {
      Serial.println(" connected");
      client.publish(willTopic.c_str(), (String(esp32_id)+":online").c_str(), true);
    } else {
      Serial.print(" rc="); Serial.println(client.state());
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }
}

// -------------------- Publication payload -------
void publishHealing(const String& name) {
  String msg = name + ":Healing";
  client.publish(TOPIC, msg.c_str(), false);
  Serial.println("Send: " + msg);
}

void publishStopHealing(const String& name) {
  String msg = name + ":StopHealing";
  client.publish(TOPIC, msg.c_str(), false);
  Serial.println("Send: " + msg);
}
