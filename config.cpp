#include "Config.h"
#include <WiFi.h>
#include "WebInterface.h"

void loadConfig(Preferences &preferences) {
  preferences.begin("config", true);
  SystemState::isConfigured = preferences.getBool("configured", false);
  if (SystemState::isConfigured) {
    SystemState::sensorLowPin  = preferences.getInt("lowPin", 34);
    SystemState::sensorHighPin = preferences.getInt("highPin", 35);
    SystemState::sensorMidPin  = preferences.getInt("midPin", -1);
    SystemState::relayPin      = preferences.getInt("relayPin", 25);
    SystemState::manualButtonPin = preferences.getInt("buttonPin", -1);
    SystemState::ssid          = preferences.getString("ssid", "");
    SystemState::pass          = preferences.getString("pass", "");
    SystemState::pushoverToken = preferences.getString("pushtoken", "");
    SystemState::pushoverUser  = preferences.getString("pushuser", "");
  }
  preferences.end();
}

void saveConfig(int low, int high, int mid, int relay, int button, String s, String p, String token, String user, Preferences &preferences) {
  preferences.begin("config", false);
  preferences.putInt("lowPin", low);
  preferences.putInt("highPin", high);
  preferences.putInt("midPin", mid);
  preferences.putInt("relayPin", relay);
  preferences.putInt("buttonPin", button);
  preferences.putString("ssid", s);
  preferences.putString("pass", p);
  preferences.putString("pushtoken", token);
  preferences.putString("pushuser", user);
  preferences.putBool("configured", true);
  preferences.end();
}

void setupPins() {
  pinMode(SystemState::sensorLowPin, INPUT_PULLUP);
  pinMode(SystemState::sensorHighPin, INPUT_PULLUP);
  if (SystemState::sensorMidPin != -1) pinMode(SystemState::sensorMidPin, INPUT_PULLUP);
  pinMode(SystemState::relayPin, OUTPUT);
  digitalWrite(SystemState::relayPin, LOW);
  pinMode(SystemState::ledPin, OUTPUT);
  digitalWrite(SystemState::ledPin, LOW);

  if (SystemState::manualButtonPin != -1) {
    pinMode(SystemState::manualButtonPin, INPUT_PULLUP);
    SystemState::lastButtonState = digitalRead(SystemState::manualButtonPin);
    SystemState::lastStableButtonState = SystemState::lastButtonState;
    Serial.println("Przycisk ręczny skonfigurowany na pinie: " + String(SystemState::manualButtonPin));
  }
}

void startConfigAP(WebServer &server) {
  Serial.println("Tryb konfiguracji - brak zapisanych ustawień.");
  WiFi.softAP(SystemState::apSSID, SystemState::apPASS);
  Serial.print("Adres IP AP: ");
  Serial.println(WiFi.softAPIP());
  server.on("/", HTTP_GET, [&server]() { handleConfigForm(server); });
  server.on("/save", HTTP_GET, [&server]() { handleSave(server); });
  server.begin();
}
