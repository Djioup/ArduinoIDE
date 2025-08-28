#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

// Informations Wi-Fi
const char* ssid = "PelucheGang";
const char* password = "CACHE-CACHEKILLER";
const char* esp32_id = "Peluche1"; // Identifiant unique pour cet ESP32

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("WiFi connecté");

  if (!MDNS.begin(esp32_id)) {
    Serial.println("Erreur mDNS"); return;
  }

  ArduinoOTA.setHostname(esp32_id);

  ArduinoOTA.begin();
  Serial.println("OTA prêt");
}

void loop() {
  ArduinoOTA.handle();
  delay(1);
}
