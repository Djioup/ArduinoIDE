#include <Arduino.h>

// === Config LED IR ===
#define IR_LED_PIN 13
#define PWM_CHANNEL 0
#define PWM_FREQ 38000   // 38 kHz
#define PWM_RES 8        // 8 bits
#define DUTY 85          // ~33 % de 255

// === Code à envoyer ===
uint32_t code = 0xA90; // Exemple ID du tueur
unsigned long pulseOn = 560;  // µs "marqueur" bit
unsigned long pulseOff = 560; // µs '0'
unsigned long pulseOne = 1690; // µs '1'
unsigned long leadOn = 9000;  // µs préambule ON
unsigned long leadOff = 4500; // µs préambule OFF

void sendBurst(unsigned long timeMicros) {
  ledcWrite(PWM_CHANNEL, DUTY);  // PWM 38kHz ON
  delayMicroseconds(timeMicros);
  ledcWrite(PWM_CHANNEL, 0);     // PWM OFF
}

void sendCode(uint32_t data) {
  // Préambule NEC
  sendBurst(leadOn);
  delayMicroseconds(leadOff);

  // 32 bits
  for (int i = 31; i >= 0; i--) {
    sendBurst(pulseOn); // start bit
    if (data & (1UL << i))
      delayMicroseconds(pulseOne); // '1'
    else
      delayMicroseconds(pulseOff); // '0'
  }

  // Stop bit
  sendBurst(pulseOn);
}

void setup() {
  Serial.begin(115200);
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(IR_LED_PIN, PWM_CHANNEL);
  Serial.println("Émetteur IR optimisé prêt");
}

void loop() {
  sendCode(code); // Envoi du code
  delay(200);     // Intervalle avant le prochain tir
}
