#include <Servo.h>
#include <DFRobotDFPlayerMini.h>
#include <SoftwareSerial.h>

Servo myServo;

// Pin de signal pour le servo
const int servoPin = 9;  // Sur Arduino Nano, utiliser le pin D9 pour le servo
float pos;
// Délais pour ralentir la vitesse du servo (en millisecondes)
const int delayTime = 15;

// Pins pour le DFPlayer Mini (adaptés pour l'Arduino Nano)
const int rxPin = 10; // RX du DFPlayer connecté au pin D10 (TX de l'Arduino Nano)
const int txPin = 11; // TX du DFPlayer connecté au pin D11 (RX de l'Arduino Nano)

SoftwareSerial mySoftwareSerial(rxPin, txPin); // Crée un port série logiciel sur les pins D10 et D11
DFRobotDFPlayerMini myDFPlayer;

void setup() {
  delay (3000);
  myServo.attach(servoPin); // Attache le servo au pin D9
    delay(50);

  mySoftwareSerial.begin(9600); // Initialisation de la communication série avec le DFPlayer
    delay(50);
  Serial.begin(115200); // Pour debug
  delay(50);
  if (!myDFPlayer.begin(mySoftwareSerial)) {  
    Serial.println("Impossible de démarrer le DFPlayer Mini. Vérifiez les connexions.");
    while(true);
  }
  Serial.println("DFPlayer Mini démarré avec succès.");

  delay(50);
  myDFPlayer.volume(30);  // Régle le volume (0~30)
  delay(50);
  myDFPlayer.loop(1);     // Lecture en boucle du premier fichier MP3
  delay(50);
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

  for (pos = 10; pos <= 180; pos+= 0.5) {
    myServo.write(pos);
    delay(delayTime); // Contrôle la vitesse du mouvement
  }

  // Rotation de 90° à 0°
  for (pos = 180; pos >= 10; pos-= 0.5) {
    myServo.write(pos);
    delay(delayTime);
  }

  if (myDFPlayer.readState() == 0) {  // 0 signifie que le lecteur est arrêté
    myDFPlayer.play(1);               // Relance la première piste
  }

}
