#include <Arduino.h>
#include <IRremote.hpp>

#define IR_SEND_PIN 14

void setup() {
  Serial.begin(115200);
  IrSender.begin(IR_SEND_PIN);   // <- 1 seul argument
}

void loop() {
  IrSender.sendNEC(0x2222, 0xA9, 1); // 1 trame + repeats
  delay(500);
}
