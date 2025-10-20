// --- Pins ESP32 ---
#define SW1 18
#define SW2 19
#define SW3 21
#define LED_BUILTIN 2

// Si tes interrupteurs ferment vers GND => actif LOW (pullup interne)
const bool ACTIVE_LOW = true;

// Etat cible (3 bits, 0..7)
byte target = 0;
byte lastTarget = 255;

// Timer de validation
unsigned long matchSince = 0;
bool inMatch = false;

void setup() {
  Serial.begin(115200);

  pinMode(SW1, INPUT_PULLUP);
  pinMode(SW2, INPUT_PULLUP);
  pinMode(SW3, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);

  randomSeed(analogRead(0));
  newTarget();
}

void loop() {
  // Lecture simple des 3 interrupteurs
  bool s1 = readSwitch(SW1);
  bool s2 = readSwitch(SW2);
  bool s3 = readSwitch(SW3);

  // Encode l'état courant sur 3 bits: b2 b1 b0
  byte current = (s3 << 2) | (s2 << 1) | (s1 << 0);

  // Feedback et logique de validation
  if (current == target) {
    digitalWrite(LED_BUILTIN, HIGH);
    if (!inMatch) { matchSince = millis(); inMatch = true; }
    if (millis() - matchSince > 3000) {
      newTarget();
      inMatch = false;
    }
  } else {
    digitalWrite(LED_BUILTIN, LOW);
    inMatch = false;
  }

  // Debug (facultatif)
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 200) {
    Serial.print("SW:[");
    Serial.print(s1); Serial.print(s2); Serial.print(s3);
    Serial.print("]  CUR="); printBits(current);
    Serial.print("  TGT=");  printBits(target);
    Serial.println();
    lastPrint = millis();
  }

  delay(20); // petit confort anti-rebond soft (simplissime)
}

// --- Utils ---
bool readSwitch(int pin) {
  int v = digitalRead(pin);
  return ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

void newTarget() {
  do {
    target = random(0, 8); // 0..7
  } while (target == lastTarget); // évite la même combinaison deux fois de suite
  lastTarget = target;

  Serial.print("Nouvelle combinaison cible = ");
  printBits(target);
  Serial.println("  (bit0=SW1, bit1=SW2, bit2=SW3)");
  digitalWrite(LED_BUILTIN, LOW);
}

// Affiche 3 bits (b2 b1 b0)
void printBits(byte x) {
  Serial.print((x >> 2) & 1);
  Serial.print((x >> 1) & 1);
  Serial.print(x & 1);
}
