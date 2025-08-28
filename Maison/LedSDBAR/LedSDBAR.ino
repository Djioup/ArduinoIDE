#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>

// Définir les broches pour le capteur ultrason
#define TRIG_PIN 14  // GPIO pour Trig
#define ECHO_PIN 27  // GPIO pour Echo

//CapteurPIR
#define LED 4  // GPIO pour Echo

// Définir les broches pour le DFPlayer Mini
#define RX_PIN 16  // ESP32 TX connecté au RX du DFPlayer Mini
#define TX_PIN 17  // ESP32 RX connecté au TX du DFPlayer Mini

int led;
bool loose;
 bool isplaying = false;
unsigned long LastSpot = 0;
unsigned long CurrentTime = millis();
unsigned long End = millis();
unsigned long LoopTime = millis ();
unsigned long LedTime = millis ();
// Initialiser l'instance DFPlayer Mini
//HardwareSerial Serial2(2); // Utilise Serial2 pour l'ESP32
DFRobotDFPlayerMini myDFPlayer;

void setup() {
  // Configuration des broches du capteur ultrason
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED,OUTPUT);
  // Initialisation des communications série
  Serial.begin(115200);     
  delay (2000)      ;                  // Moniteur série pour débogage
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);  // Initialisation de Serial2 avec les broches personnalisées
  delay (2000);

  // Initialisation du DFPlayer Mini
  Serial.println("Initialisation du DFPlayer Mini...");
  if (!myDFPlayer.begin(Serial2)) {  // Utilisation de Serial2 pour le DFPlayer
    Serial.println("Impossible de communiquer avec le DFPlayer Mini !");
    while (true)
      ;  // Arrêt si erreur
  }
   // Régler le volume (0 à 30)
  Serial.println("DFPlayer Mini initialisé.");
  digitalWrite (LED, HIGH);
  //myDFPlayer.play(1);
}

void loop() {
  long duration;
  float distance;

  if (led > 3)
  {
    led = 3;
  }
 

  if (millis() - LoopTime > 100)
  {
     Serial.println(led);
  // Envoi d'un signal Trig (10 µs)
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Lecture du signal Echo
  duration = pulseIn(ECHO_PIN, HIGH);

  // Calcul de la distance en cm
  distance = (duration * 0.034) / 2;

  // Affichage de la distance et lecture du fichier MP3 si condition remplie
  if (millis () - LastSpot > 1500 && distance > 2 && distance < 200) {
    LastSpot = millis();
    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.println(" cm"); 
    led += 1;
    CurrentTime = millis();
    End = millis ();
  }

  if (distance >= 200)
  {
  Serial.println("Hors de portée !");
  }

  if (led == 0)
  {
  digitalWrite (LED, HIGH);
  }

  else if (led <= 2)
  {
    if (millis () - End >= 10000)
    {
      led = 0;
    }

    if (millis () - End < 10000)
    {
    if (millis() - LedTime > 300)
    {
      if (digitalRead(LED) == 1)
      {
        digitalWrite(LED, LOW);
        LedTime = millis ();
      }
      else if (digitalRead(LED) == 0)
      {
       digitalWrite(LED, HIGH);
       LedTime = millis ();
      }
    }
    }
  }
  
  // if (led == 2)
  // {
  //   if (millis () - End >= 10000)
  //   {
  //     led = 0;
  //   }

  //   if (millis () - End < 10000)
  //   {
  //   if (millis() - CurrentTime > 200)
  //   {
  //     if (digitalRead(LED) == 1)
  //     {
  //       digitalWrite(LED, LOW);
  //       CurrentTime = millis ();
  //     }
  //     else if (digitalRead(LED) == 0)
  //     {
  //      digitalWrite(LED, HIGH);
  //      CurrentTime = millis ();
  //     }
  //   }
  //   }
  // }

  else if (led >= 3)
  {
   
    if (!isplaying){
    myDFPlayer.volume(30); 
    delay (100);
    myDFPlayer.play(1); // Joue la piste 01.mp3 (index 1)    
    isplaying = true;
    }
    LastSpot = millis();
  
    if (millis () - End >= 15000)
    {
      led = 0;
      isplaying = false;
    }

    if (millis () - End < 15000)
    {
    if (millis() - LedTime > 100)
    {
      if (digitalRead(LED) == 1)
      {
        digitalWrite(LED, LOW);
        LedTime = millis ();
      }
      else if (digitalRead(LED) == 0)
      {
       digitalWrite(LED, HIGH);
       LedTime = millis ();
      }
    }
    }
  }
  
      
  

  // else
  // {
  //   Serial.println("Hors de portée !");
  //   //digitalWrite(LED, HIGH);
  // }
  //delay(500);  // Pause avant la prochaine lecture
  LoopTime = millis ();
}
}
