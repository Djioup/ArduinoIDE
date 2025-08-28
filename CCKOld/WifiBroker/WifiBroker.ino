#include <WiFi.h>
#include <PubSubClient.h>

// Informations de votre réseau Wi-Fi
const char* ssid = "DjiooDanTae";
const char* password = "DjioopPod";

// Adresse IP du broker MQTT
const char* mqtt_server = "192.168.5.192";

// Identifiants MQTT
const char* mqtt_user = "DjiooDanTae";  // Nom d'utilisateur MQTT
const char* mqtt_password = "DjioopPod"; // Mot de passe MQTT

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);

  // Connexion au réseau Wi-Fi
  setup_wifi();

  // Configuration du broker MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connexion à ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("Connexion Wi-Fi établie");
  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message reçu [");
  Serial.print(topic);
  Serial.print("] : ");
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  // Traitez le message reçu si nécessaire
}

void reconnect() {
  // Boucle jusqu'à ce que la connexion au broker MQTT soit établie
  while (!client.connected()) {
    Serial.print("Connexion au broker MQTT...");
    // Connexion avec authentification
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("connecté");

      // S'abonner aux topics nécessaires
      client.subscribe("unity/commandes");
    } else {
      Serial.print("échec, rc=");
      Serial.print(client.state());
      Serial.println(" nouvelle tentative dans 5 secondes");
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Exemple de publication d'un message
  String message = "Données du capteur";
  client.publish("esp32/donnees", message.c_str());

  delay(2000); // Attendre 2 secondes avant la prochaine publication
}
