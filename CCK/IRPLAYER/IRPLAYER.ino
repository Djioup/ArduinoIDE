#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

#define RECV_PIN 15        // Pin du TSOP
#define LED_PIN 13          // LED intégrée ESP32 (souvent GPIO 2)

IRrecv irrecv(RECV_PIN);
decode_results results;

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);  
  digitalWrite(LED_PIN, LOW);

  // 🔽 Ajoute cette ligne pour activer le pull-up interne sur RECV_PIN
  pinMode(RECV_PIN, INPUT_PULLUP);

  irrecv.enableIRIn();       // Active la réception
  Serial.println("Récepteur IR prêt");
}

void loop() {
  if (irrecv.decode(&results)) {
    Serial.print("Code reçu : 0x");
    Serial.println(results.value, HEX);

    // Filtre : n'allume la LED que si code == 0xA90
    if (results.value == 0xA90) {
      digitalWrite(LED_PIN, HIGH);
      delay(100); // LED allumée brièvement
      digitalWrite(LED_PIN, LOW);
    }

    irrecv.resume();  // Prêt pour le signal suivant
  }
}
