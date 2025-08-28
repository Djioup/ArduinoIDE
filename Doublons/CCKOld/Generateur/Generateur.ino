// Pins des LED et des boutons
const int signalLedPins[] = {13, 12, 14, 27, 26}; // LEDs de signal : bleu, rouge, blanc, jaune, vert
const int buttonLedPins[] = {33, 25, 2, 21, 22}; // LEDs associées aux boutons
const int buttonPins[] = {23, 4, 5, 18, 19}; // Boutons associés aux LEDs : bleu, rouge, blanc, jaune, vert

const int numLeds = sizeof(signalLedPins) / sizeof(signalLedPins[0]);
const int numButtons = sizeof(buttonPins) / sizeof(buttonPins[0]);

int sequence[8]; // Stocke la séquence de couleurs
int level = 1;   // Niveau actuel du jeu
int currentInputIndex = 0; // Indice de la séquence en cours de saisie
bool StartGame = false;
bool playerFailed = false; // Indique si le joueur a échoué pendant la séquence
unsigned long gameStartTime = 0; // Temps pour éviter un rebond initial après le démarrage
unsigned long stepStartTime = 0; // Temps de début pour chaque étape
unsigned long inputTimeout = 2000; // Temps limite par étape (2 secondes pour le niveau 1)
unsigned long blinkTimer = 0; // Temps pour gérer le clignotement du bouton rouge
unsigned long lastPressTime = 0; // Temps du dernier appui pour gérer l'anti-rebond
unsigned long effectStartTime = 0; // Temps de début pour l'effet visuel des LEDs des boutons
const unsigned long debounceDelay = 300; // Délai pour l'anti-rebond
bool blinkState = false; // État actuel du clignotement
bool disableLedEffects = false; // Drapeau pour désactiver les effets LED

void setup() {
  // Initialiser les pins des LEDs de signal comme sorties
  for (int i = 0; i < numLeds; i++) {
    pinMode(signalLedPins[i], OUTPUT);
    digitalWrite(signalLedPins[i], LOW);
  }

  // Initialiser les pins des LEDs des boutons comme sorties
  for (int i = 0; i < numLeds; i++) {
    pinMode(buttonLedPins[i], OUTPUT);
    analogWrite(buttonLedPins[i], 0);
    delay(10); // Toutes les LEDs des boutons éteintes au départ
  }

  // Initialiser les pins des boutons comme entrées avec pull-up
  for (int i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }

  // Initialiser le générateur de nombres aléatoires
  randomSeed(analogRead(0));

  Serial.begin(9600);

  // Générer la première séquence
  generateRandomSequence();
}

void loop() {
  if (!StartGame) {
    handlePreGameState();
    return;
  }

  if (StartGame && !playerFailed) {
    if (currentInputIndex < level + 4) {
      processStep();
      if (!disableLedEffects) {
        updateButtonLedEffect(); // Mettre à jour l'effet visuel
      }
    } else {
      Serial.println("Séquence complétée avec succès");
      if (level < 3) {
        level++;
        resetGameForNextLevel();
      } else {
        celebrateWin();
        //resetGame();
      }
    }
  } else if (playerFailed) {
    Serial.println("Échec, réinitialisation du jeu");
    resetGame();
  }
}

void handlePreGameState() {
  // Clignotement du bouton rouge pour indiquer que le jeu n'a pas encore commencé
  if (millis() - blinkTimer >= 500) {
    blinkTimer = millis();
    blinkState = !blinkState;
    digitalWrite(signalLedPins[1], blinkState);
    analogWrite(buttonLedPins[1], blinkState ? 255 : 0);
    delay(10);
  }

  // Détection de l'appui sur le bouton rouge pour démarrer le jeu
  if (digitalRead(buttonPins[1]) == LOW && (millis() - lastPressTime > debounceDelay)) {
    lastPressTime = millis();
    StartGame = true;
    gameStartTime = millis();
    currentInputIndex = 0;
    playerFailed = false;
    inputTimeout = calculateTimeout(level); // Initialiser le temps limite en fonction du niveau

    // Allumer les LEDs des boutons et éteindre toutes les LEDs de signal
    for (int i = 0; i < numLeds; i++) {
      analogWrite(buttonLedPins[i], 255);
    delay(10);
      digitalWrite(signalLedPins[i], LOW);
    }

    effectStartTime = millis(); // Démarrer l'effet visuel

    Serial.println("Jeu démarré, début de la séquence");
    playSequence();
  }
}

void processStep() {
  int currentSignal = sequence[currentInputIndex];

  if (millis() - stepStartTime > inputTimeout) {
    Serial.println("Temps écoulé, échec");
    playerFailed = true;
    return;
  }

  digitalWrite(signalLedPins[currentSignal], HIGH);

  for (int i = 0; i < numButtons; i++) {
    int buttonState = digitalRead(buttonPins[i]);

    if (buttonState == LOW && (millis() - lastPressTime > debounceDelay)) {
      lastPressTime = millis();
      Serial.print("Bouton ");
      Serial.print(i);
      Serial.print(" pressé. Attendu : ");
      Serial.println(currentSignal);

      if (i == currentSignal) {
        Serial.println("Correct");
        digitalWrite(signalLedPins[currentSignal], LOW);
        stepStartTime = millis(); // Redémarrer le timer pour la prochaine étape
        currentInputIndex++;
        effectStartTime = millis(); // Redémarrer l'effet visuel
        return;
      } else {
        Serial.println("Incorrect");
        playerFailed = true;
        return;
      }
    }
  }
}

void updateButtonLedEffect() {
  if (disableLedEffects) {
    return;
  }

  unsigned long elapsed = millis() - effectStartTime;
  unsigned long brightness = map(elapsed, 0, inputTimeout, 255, 0); // Diminue la luminosité en fonction du temps

  for (int i = 0; i < numLeds; i++) {
    analogWrite(buttonLedPins[i], brightness); // Ajuste la luminosité des LEDs des boutons
    delay(10);
  }
}

void playSequence() {
  for (int i = 0; i < level + 4; i++) {
    Serial.print("Séquence générée : ");
    Serial.println(sequence[i]);
  }
  stepStartTime = millis();
}

unsigned long calculateTimeout(int lvl) {
  switch (lvl) {
    case 1:
      return 2000;
    case 2:
      return 1500;
    case 3:
      return 1000;
    default:
      return 2000;
  }
}

void generateRandomSequence() {
  int numColors = level + 4;
  for (int i = 0; i < numColors; i++) {
    int attempts = 0;
    int randIndex;
    do {
      randIndex = random(0, numButtons);
      attempts++;
      if (attempts > 10) break;
    } while ((i > 0 && randIndex == sequence[i - 1]) || (i == 0 && level > 1 && randIndex == sequence[level + 3]));
    sequence[i] = randIndex;
  }
}

void resetGame() {
  disableLedEffects = true; // Désactiver les effets LED

  //level = 1;
  currentInputIndex = 0;
  playerFailed = false;
  StartGame = false;

  // Éteindre toutes les LEDs des boutons et de signal
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(signalLedPins[i], LOW);
    analogWrite(buttonLedPins[i], 0);
    delay(15);
  }

  generateRandomSequence();

  disableLedEffects = false; // Réactiver les effets LED après réinitialisation
}

void resetGameForNextLevel() {
  // Pause avec toutes les LEDs éteintes avant le prochain niveau
  disableLedEffects = true; // Désactiver les effets LED pendant la pause
  for (int i = 0; i < numLeds; i++) {
    analogWrite(buttonLedPins[i], 0); // Éteindre les LEDs des boutons
    delay(15);
    digitalWrite(signalLedPins[i], LOW); // Éteindre les LEDs de signal
  }
  delay(2000); // Pause de 2 secondes

  // Rallumer toutes les LEDs des boutons après la pause
  for (int i = 0; i < numLeds; i++) {
    analogWrite(buttonLedPins[i], 255); // Rallumer les LEDs des boutons avec luminosité maximale
    delay(15);
  }
  
  currentInputIndex = 0;
  playerFailed = false;
  inputTimeout = calculateTimeout(level);
  generateRandomSequence();
  stepStartTime = millis(); // Réinitialiser le timer pour le nouveau niveau
  disableLedEffects = false; // Réactiver les effets LED après la pause

  Serial.println("Niveau suivant");
}

void celebrateWin() {
  disableLedEffects = true; // Désactiver les effets LED pendant la célébration

  unsigned long winStartTime = millis();
  unsigned long lastBlinkTime = millis();
  const unsigned long blinkInterval = 200;
  bool ledState = false;

  while (millis() - winStartTime < 5000) { // Clignotement pendant 5 secondes
    if (millis() - lastBlinkTime >= blinkInterval) {
      lastBlinkTime = millis();
      ledState = !ledState;
      for (int i = 0; i < numLeds; i++) {
        analogWrite(buttonLedPins[i], ledState ? 255 : 0);
        delay(5);
        digitalWrite(signalLedPins[i], ledState ? HIGH : LOW);
      }
    }
  }

  // Éteindre toutes les LEDs après la victoire
  for (int i = 0; i < numLeds; i++) {
    analogWrite(buttonLedPins[i], 255);
    delay(5);
    digitalWrite(signalLedPins[i], HIGH);
  }

  disableLedEffects = false; // Réactiver les effets LED après la célébration
}
