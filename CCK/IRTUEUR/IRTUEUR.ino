#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <Adafruit_NeoPixel.h>

// ---------- IR ----------
#define IR_LED_PIN 14           // GPIO relié à la gate du MOSFET via ~220 Ω
IRsend irsend(IR_LED_PIN);

// ---------- NeoPixel ----------
#define RING_PIN   27          // GPIO pour l’anneau LED (DIN)
#define NUM_LEDS   36          // Nombre de LED sur l’anneau
Adafruit_NeoPixel ring(NUM_LEDS, RING_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);

  // IR
  irsend.begin();               // Active la porteuse 38 kHz sur IR_LED_PIN
  Serial.println("Émetteur IR prêt");

  // NeoPixel
  ring.begin();
  ring.setBrightness(255);       // 0..255 (à ajuster selon votre alim)
  ring.fill(ring.Color(255, 255, 255), 0, NUM_LEDS);  // Tout en rouge
  ring.show();
}

void loop() {
  uint32_t code = 0x00000A90;   // Exemple de code NEC (32 bits)

  irsend.sendNEC(code, 32);     // (code, nombre de bits)
  Serial.println("Code IR envoyé !");
  delay(10);                    // Pause entre tirs
}
