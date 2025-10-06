#define POT1 13        // premier potentiomètre (entrée analogique)
#define POT2 14        // deuxième potentiomètre (entrée analogique)
#define LED_BUILTIN 2  // LED feedback

// Variables pour les cibles
int target1 = -1;
int target2 = -1;

// Variables de contrôle
unsigned long TimeIn = 0;
bool inRangeBoth = false;
unsigned long lastBlink = 0;
bool ledState = false;

void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);

  randomSeed(analogRead(0));  // meilleure génération pseudo-aléatoire
  newTargets();
}

void loop() {
  int val1 = analogRead(POT1);
  int val2 = analogRead(POT2);

  bool pot1Ok = abs(val1 - target1) <= 100;
  bool pot2Ok = abs(val2 - target2) <= 100;

  // --- Cas 1 : les deux sont bons -> LED fixe ON ---
  if (pot1Ok && pot2Ok) {
    digitalWrite(LED_BUILTIN, HIGH);

    if (!inRangeBoth) {
      TimeIn = millis();
      inRangeBoth = true;
    }

    if (millis() - TimeIn > 3000) {
      newTargets();
      inRangeBoth = false;
    }
  }
  // --- Cas 2 : seulement un est bon -> LED clignote ---
  else if (pot1Ok || pot2Ok) {
    if (millis() - lastBlink > 300) {  // vitesse de clignotement
      ledState = !ledState;
      digitalWrite(LED_BUILTIN, ledState);
      lastBlink = millis();
    }
    inRangeBoth = false;
  }
  // --- Cas 3 : aucun n’est bon -> LED OFF ---
  else {
    digitalWrite(LED_BUILTIN, LOW);
    inRangeBoth = false;
  }

  // Debug
  Serial.print("POT1: ");
  Serial.print(val1);
  Serial.print(" (Target1: ");
  Serial.print(target1);
  Serial.print(") | POT2: ");
  Serial.print(val2);
  Serial.print(" (Target2: ");
  Serial.print(target2);
  Serial.println(")");
  delay(50);
}

void newTargets() {
  target1 = random(0, 4096);
  target2 = random(0, 4096);
  Serial.print("New targets -> POT1: ");
  Serial.print(target1);
  Serial.print(" | POT2: ");
  Serial.println(target2);
  digitalWrite(LED_BUILTIN, LOW);
}
