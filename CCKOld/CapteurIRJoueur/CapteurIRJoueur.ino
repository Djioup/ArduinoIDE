#define IR_SENSOR_PIN 15 // Broche connectée à la sortie du capteur IR

void setup() {
  Serial.begin(115200);
  pinMode(IR_SENSOR_PIN, INPUT);
  Serial.println("Capteur IR prêt !");
}

void loop() {
  int irState = analogRead(IR_SENSOR_PIN); // Lire l'état du capteur (HIGH ou LOW)

  
    Serial.println(irState);
  

  delay(200); // Pause pour éviter de saturer le moniteur série
}
