#include <ESP32Servo.h>
#include <DFRobotDFPlayerMini.h>
#include <Arduino.h>

Servo myServo;

// Pin de signal pour le servo
const int servoPin = 13;  // Sur Arduino Nano, utiliser le pin D9 pour le servo
float pos;
// Délais pour ralentir la vitesse du servo (en millisecondes)
const int delayTime = 15;


// Définir les broches pour le DFPlayer Mini
#define RX_PIN 16
#define TX_PIN 17

DFRobotDFPlayerMini myDFPlayer;

void setup() {
  delay (3000);
  myServo.attach(servoPin); // Attache le servo au pin D9
    delay(500);


  delay(1000);
  Serial.begin(115200);
  delay(2000);
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(2000);

  // Initialisation du DFPlayer Mini
  Serial.println("Initialisation du DFPlayer Mini...");
  if (!myDFPlayer.begin(Serial2)) {
    Serial.println("Impossible de communiquer avec le DFPlayer Mini !");
    while (true);
  }
  Serial.println("DFPlayer Mini initialisé.");
}

void loop() {
  // Rotation de 0° à 90°
  if (pos < 10)
  {
    pos = 10;
  }

  if (pos > 180)
  {
    pos = 180;
  }

  for (pos <= 10; pos <= 180; pos+= 0.5) {
    myServo.write(pos);
    delay(delayTime); // Contrôle la vitesse du mouvement
  }

  // Rotation de 90° à 0°
  for (pos >= 180; pos >= 10; pos-= 0.5) {
    myServo.write(pos);
    delay(delayTime);
  }

  if (myDFPlayer.readState() == 0) {  // 0 signifie que le lecteur est arrêté
      delay(500);
      myDFPlayer.volume(30);  // Régle le volume (0~30)
      delay(500);
      myDFPlayer.loop(1);     // Lecture en boucle du premier fichier MP3
      delay(500);             // Relance la première piste
  }

}
