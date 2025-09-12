#include <IRremote.hpp>
#include <Adafruit_NeoPixel.h>

#define IR_SEND_PIN 14

#define RING_PIN 27
#define NUM_LEDS 36
Adafruit_NeoPixel ring(NUM_LEDS, RING_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);

  // IR TX
  IrSender.begin(IR_SEND_PIN);  // ou: IrSender.begin(IR_SEND_PIN, ENABLE_LED_FEEDBACK, LED_BUILTIN);

  // NeoPixel
  ring.begin();
  ring.setBrightness(255);
  ring.fill(ring.Color(255, 0, 0), 0, NUM_LEDS); // rouge (ton code mettait blanc: 255,255,255)
  ring.show();
}

void loop() {
  const uint16_t addr = 0x0000;
  const uint8_t  cmd  = 0xA9;
  const uint8_t  reps = 2;   // 1 trame + 2 repeats NEC

  IrSender.sendNEC(addr, cmd, reps);
  delay(120); // laisse respirer le canal; pas besoin de spammer
}
