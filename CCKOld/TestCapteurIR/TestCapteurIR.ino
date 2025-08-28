const int irPin = 34;  // GPIO 34 (ADC1) pour lire la photodiode
int irValue = 0;




void setup() {
  Serial.begin(115200);
  pinMode (2, OUTPUT);
  // analogReadResolution(12);  // Résolution des ADC sur 12 bits (valeur de 0 à 4095)
  // analogSetAttenuation(ADC_11db); // Plage de tension de 0 à ~3.3V
  pinMode(irPin, INPUT);
}

void loop() {
  int rawValue = analogRead(irPin);  // Lire la valeur brute
float voltage = rawValue * (3.3 / 4095.0);  // Convertir en tension (pour une plage 0-3.3V)
Serial.println(voltage);
  Serial.print("Valeur IR : ");
  Serial.println(rawValue);

  if (rawValue < 4050)
  {
    digitalWrite (2, HIGH);
  }
  else
  digitalWrite (2, LOW);

  delay(100);  // Petit délai pour stabiliser les lectures
}
