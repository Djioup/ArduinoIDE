#define POT1 34
#define POT2 35
#define LED_BUILTIN 2

int target = 0;
unsigned long goodSince = 0;
bool inRange = false;

void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  randomSeed(analogRead(0));
  newTarget();
}

void loop() {
  int val1 = analogRead(POT1);
  int val2 = analogRead(POT2);
  int sum = val1 + val2;
  int diff = abs(sum - target);

  // ----- LED feedback -----
  if (diff < 80) { // parfait
    digitalWrite(LED_BUILTIN, HIGH);
    if (!inRange) { goodSince = millis(); inRange = true; }
    if (millis() - goodSince > 3000) newTarget();  // nouvelle cible après 3s
  } 
  else if (diff < 500) { // on s'approche -> clignote
    digitalWrite(LED_BUILTIN, (millis() / 200) % 2);
    inRange = false;
  } 
  else { // trop loin
    digitalWrite(LED_BUILTIN, LOW);
    inRange = false;
  }

  Serial.print("P1: "); Serial.print(val1);
  Serial.print(" | P2: "); Serial.print(val2);
  Serial.print(" | SUM: "); Serial.print(sum);
  Serial.print(" | TARGET: "); Serial.println(target);

  delay(100);
}

void newTarget() {
  target = random(200, 8000); // évite extrêmes 0 et 8190
  Serial.print("Nouvelle cible = "); Serial.println(target);
  digitalWrite(LED_BUILTIN, LOW);
  inRange = false;
}
