const int photodiodePin = 4; // D4 sur ESP32

void setup() {
    Serial.begin(115200);
    pinMode(photodiodePin, INPUT);
}

void loop() {
    int value = analogRead(photodiodePin); // Lecture de la photodiode
    Serial.print("Photodiode state: ");
    Serial.println(value);
    delay(500); // Pause pour éviter trop de messages
}
