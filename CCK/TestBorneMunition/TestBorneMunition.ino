#include <Arduino.h>
#include "ESP32BleAdvertise.h"
#include <Adafruit_NeoPixel.h>

#define LED_PIN    5
#define LED_COUNT  30

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

SimpleBLE ble;
String BLENAME = "Munition1";

// ---- Timing ----
static const uint32_t LED_PERIOD_MS = 20;     // respiration fluide
static const uint32_t BLE_PERIOD_MS = 1000;   // pub BLE toutes les 1s

uint32_t lastLedMs = 0;
uint32_t lastBleMs = 0;

// ---- Animation ----
int brightness = 0;
int fadeAmount = 3;

void setup() {
  Serial.begin(115200);

  ble.begin(BLENAME);
  Serial.println("Borne Munition BLE active !");

  strip.begin();
  strip.show(); // éteint
}

static void updateLedsBreathingBlue() {
  brightness += fadeAmount;

  if (brightness <= 0 || brightness >= 255) {
    fadeAmount = -fadeAmount;
    if (brightness < 0) brightness = 0;
    if (brightness > 255) brightness = 255;
  }

  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, strip.Color(brightness, 0,0 ));
  }
  strip.show();
}

static void advertiseBle() {
  // payload simple, à adapter si besoin
  ble.advertise("Munition");
}

void loop() {
  const uint32_t now = millis();

  // LED tick
  if (now - lastLedMs >= LED_PERIOD_MS) {
    lastLedMs = now;
    updateLedsBreathingBlue();
  }

  // BLE tick
  if (now - lastBleMs >= BLE_PERIOD_MS) {
    lastBleMs = now;
    advertiseBle();
  }

  // Pas de delay : on laisse le CPU respirer sans bloquer
  // (optionnel) yield();
}
