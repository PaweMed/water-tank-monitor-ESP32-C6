// ESP32 Water Monitor z trybem offline/online, OTA, Pushover i WWW
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Update.h>

Preferences preferences;
WebServer server(80);

int sensorLowPin = 34;
int sensorHighPin = 35;
int sensorMidPin = -1;
int relayPin = 25;
int manualButtonPin = -1;  // GPIO0 dla przycisku BOOT

bool isConfigured = false;
bool pumpOn = false;
bool testMode = false;
bool wifiConnected = false;
bool manualMode = false;
unsigned long manualModeStartTime = 0;
const unsigned long manualModeTimeout = 30 * 60 * 1000;

// Zmienne do zabezpieczenia przed zbyt częstym przełączaniem
unsigned long lastPumpToggleTime = 0;
const unsigned long minPumpToggleInterval = 30000; // 30 sekund minimalnego odstępu
int pumpToggleCount = 0;
const int maxPumpTogglesPerMinute = 4; // Maksymalnie 4 przełączenia na minutę
unsigned long lastMinuteCheck = 0;

// Zmienne do opóźnionej reakcji na czujniki
unsigned long lastSensorChangeTime = 0;
bool lastLowState = false;
bool lastHighState = false;
bool lastMidState = false;
const unsigned long sensorDebounceTime = 5000; // 5 sekund opóźnienia

// Zmienne dla przycisku ręcznego
bool lastButtonState = HIGH;
bool buttonState = HIGH;
bool lastStableButtonState = HIGH;
unsigned long lastButtonDebounceTime = 0;
const unsigned long buttonDebounceDelay = 50;
unsigned long lastButtonPressTime = 0;
const unsigned long buttonPressDelay = 1000; // Minimalny czas między przełączeniami

String ssid = "iPhone Pawel";
String pass = "12345678";

const char* apSSID = "ESP32-Setup";
const char* apPASS = "12345678";

String pushoverUser = "udf4rw1948gnfuxbbcvmqvfpx3qy1o";
String pushoverToken = "a5f8roe5nvqvcj4f7r3wrppmpoi1ea";

#define EVENT_LIMIT 20
String events[EVENT_LIMIT];
int eventIndex = 0;

// Watchdog timer
hw_timer_t *watchdogTimer = NULL;

void IRAM_ATTR resetModule() {
  ets_printf("Watchdog reboot\n");
  esp_restart();
}

bool canTogglePump(bool manualOverride = false) {
  if (manualOverride) return true;  // Pomijaj zabezpieczenia dla trybu manualnego
  
  unsigned long now = millis();
  
  // Resetuj licznik co minutę
  if (now - lastMinuteCheck > 60000) {
    pumpToggleCount = 0;
    lastMinuteCheck = now;
  }
  
  // Sprawdź czy nie przekraczamy limitu przełączeń
  if (pumpToggleCount >= maxPumpTogglesPerMinute) {
    addEvent("Osiągnięto limit przełączeń pompy (4/min)");
    sendPushover("Osiągnięto limit przełączeń pompy (4/min) - bezpiecznik");
    return false;
  }
  
  // Sprawdź minimalny odstęp czasowy
  if (now - lastPumpToggleTime < minPumpToggleInterval) {
    addEvent("Zbyt częste przełączanie pompy - bezpiecznik");
    sendPushover("Zbyt częste przełączanie pompy - bezpiecznik");
    return false;
  }
  
  return true;
}

void addEvent(String msg) {
  Serial.println(msg);
  events[eventIndex] = msg;
  eventIndex = (eventIndex + 1) % EVENT_LIMIT;
}

void sendPushover(String msg) {
  static String lastMessage = "";
  static unsigned long lastSendTime = 0;
  
  // Nie wysyłaj tego samego komunikatu częściej niż co 30 sekund
  if (msg == lastMessage && millis() - lastSendTime < 30000) {
    Serial.println("[Pushover] Pominięto duplikat wiadomości: " + msg);
    return;
  }
  
  Serial.println("[Pushover] Próba wysłania: " + msg);
  
  if (!wifiConnected) {
    Serial.println("[Pushover] Błąd: Brak połączenia WiFi");
    return;
  }
  if (pushoverToken == "" || pushoverUser == "") {
    Serial.println("[Pushover] Błąd: Brak tokenu lub użytkownika");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  client.setTimeout(10000);
  https.setTimeout(10000);

  String url = "https://api.pushover.net/1/messages.json";
  Serial.println("[Pushover] Łączenie z: " + url);
  
  if (https.begin(client, url)) {
    https.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    String postData = "token=" + pushoverToken + 
                     "&user=" + pushoverUser + 
                     "&message=" + urlEncode(msg) +
                     "&title=Zbiornik z wodą";
    
    Serial.println("[Pushover] Wysyłane dane: " + postData);
    
    int httpCode = https.POST(postData);
    String response = https.getString();
    
    Serial.println("[Pushover] HTTP Code: " + String(httpCode));
    Serial.println("[Pushover] Odpowiedź: " + response);
    
    https.end();
    
    if (httpCode == HTTP_CODE_OK) {
      Serial.println("[Pushover] Wysłano pomyślnie!");
      lastMessage = msg;
      lastSendTime = millis();
    } else {
      Serial.println("[Pushover] Błąd wysyłania!");
    }
  } else {
    Serial.println("[Pushover] Błąd początkowania połączenia");
  }
}

String urlEncode(String str) {
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (unsigned int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encodedString += '+';
    } else if (isalnum(c)) {
      encodedString += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) {
        code0 = c - 10 + 'A';
      }
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
  }
  return encodedString;
}

void saveConfig(int low, int high, int mid, int relay, int button, String s, String p, String token, String user) {
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

void loadConfig() {
  preferences.begin("config", true);
  isConfigured = preferences.getBool("configured", false);
  if (isConfigured) {
    sensorLowPin = preferences.getInt("lowPin", 34);
    sensorHighPin = preferences.getInt("highPin", 35);
    sensorMidPin = preferences.getInt("midPin", -1);
    relayPin = preferences.getInt("relayPin", 25);
    manualButtonPin = preferences.getInt("buttonPin", -1);
    ssid = preferences.getString("ssid", "");
    pass = preferences.getString("pass", "");
    pushoverToken = preferences.getString("pushtoken", "");
    pushoverUser = preferences.getString("pushuser", "");
  }
  preferences.end();
}

void setupPins() {
  pinMode(sensorLowPin, INPUT);
  pinMode(sensorHighPin, INPUT);
  if (sensorMidPin != -1) pinMode(sensorMidPin, INPUT);
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);
  
  if (manualButtonPin != -1) {
    pinMode(manualButtonPin, INPUT_PULLUP);
    lastButtonState = digitalRead(manualButtonPin);
    lastStableButtonState = lastButtonState;
    Serial.println("Przycisk ręczny skonfigurowany na pinie: " + String(manualButtonPin));
  }
}

String getStatusHTML(String content = "") {
  bool low = testMode || digitalRead(sensorLowPin) == LOW;
  bool high = testMode || digitalRead(sensorHighPin) == LOW;
  bool mid = (sensorMidPin != -1) ? (testMode || digitalRead(sensorMidPin) == LOW) : false;

  int waterLevel = 0;
  if (high) waterLevel = 100;
  else if (mid) waterLevel = 65;
  else if (low) waterLevel = 30;
  else waterLevel = 5;

  String modeBadge = "";
  if (testMode) {
    modeBadge = "<div class='badge test-mode'><i class='fas fa-flask'></i> Tryb testowy</div>";
  } else if (manualMode) {
    unsigned long remaining = (manualModeStartTime + manualModeTimeout - millis()) / 60000;
    modeBadge = "<div class='badge manual-mode'><i class='fas fa-hand-paper'></i> Tryb manualny (" + String(remaining) + " min)</div>";
  }

  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="pl">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>System Zbiornika Wody</title>
  <link href="https://fonts.googleapis.com/css2?family=Roboto:wght@300;400;500;700&display=swap" rel="stylesheet">
  <link href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.15.4/css/all.min.css" rel="stylesheet">
  <style>
    :root {
      --primary: #3498db;
      --secondary: #2ecc71;
      --danger: #e74c3c;
      --warning: #f39c12;
      --dark: #2c3e50;
      --light: #ecf0f1;
    }
    body {
      font-family: 'Roboto', sans-serif;
      background-color: #f5f7fa;
      color: #333;
      margin: 0;
      padding: 20px;
    }
    .container {
      max-width: 1000px;
      margin: 0 auto;
      background: white;
      border-radius: 15px;
      box-shadow: 0 5px 15px rgba(0,0,0,0.1);
      overflow: hidden;
    }
    header {
      background: linear-gradient(135deg, var(--primary), var(--dark));
      color: white;
      padding: 20px;
      text-align: center;
    }
    .badge {
      display: inline-block;
      padding: 5px 10px;
      border-radius: 20px;
      font-size: 14px;
      margin-top: 10px;
      font-weight: 500;
    }
    .test-mode {
      background-color: var(--warning);
      color: white;
    }
    .manual-mode {
      background-color: var(--primary);
      color: white;
    }
    .dashboard {
      display: grid;
      grid-template-columns: 2fr 1fr;
      gap: 20px;
      padding: 20px;
    }
    @media (max-width: 768px) {
      .dashboard {
        grid-template-columns: 1fr;
      }
    }
    .tank-container {
      background: white;
      border-radius: 10px;
      padding: 20px;
      box-shadow: 0 3px 10px rgba(0,0,0,0.05);
    }
    .tank {
      position: relative;
      max-width: 300px;
      margin: 0 auto;
      width: 100%;
      height: 300px;
      background: #e0f2fe;
      border-radius: 5px;
      overflow: hidden;
      border: 3px solid #b3e0ff;
    }
    .water {
      position: absolute;
      bottom: 0;
      width: 100%;
      height: )rawliteral" + String(waterLevel) + R"rawliteral(%;
      background: linear-gradient(to top, #3b82f6, #60a5fa);
      transition: height 0.5s ease;
    }
    .sensor {
      position: absolute;
      left: 10px;
      width: calc(100% - 20px);
      height: 3px;
      background: var(--dark);
      border-radius: 3px;
    }
    .sensor::after {
      content: '';
      position: absolute;
      right: -15px;
      top: -5px;
      width: 10px;
      height: 10px;
      border-radius: 50%;
    }
    .sensor.high { top: 10%; }
    .sensor.high::after { background: )rawliteral" + (high ? "var(--secondary)" : "var(--danger)") + R"rawliteral(; }
    .sensor.mid { top: 35%; display: )rawliteral" + (sensorMidPin != -1 ? "block" : "none") + R"rawliteral(; }
    .sensor.mid::after { background: )rawliteral" + (mid ? "var(--secondary)" : "var(--danger)") + R"rawliteral(; }
    .sensor.low { top: 70%; }
    .sensor.low::after { background: )rawliteral" + (low ? "var(--secondary)" : "var(--danger)") + R"rawliteral(; }
    .sensor-label {
      position: absolute;
      right: -80px;
      top: -10px;
      font-size: 14px;
      font-weight: 500;
      white-space: nowrap;
    }
    .water-percentage {
      position: absolute;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      font-size: 24px;
      font-weight: 700;
      color: rgba(255,255,255,0.8);
      text-shadow: 0 2px 4px rgba(0,0,0,0.3);
    }
    .status-indicator {
      display: flex;
      align-items: center;
      margin-bottom: 10px;
    }
    .status-dot {
      width: 12px;
      height: 12px;
      border-radius: 50%;
      margin-right: 10px;
    }
    .status-on { background-color: var(--secondary); }
    .status-off { background-color: var(--danger); }
    .nav {
      display: flex;
      justify-content: space-around;
      background: var(--light);
      padding: 15px;
      border-radius: 10px;
      margin-top: 20px;
    }
    .nav a {
      color: var(--dark);
      text-decoration: none;
      font-weight: 500;
      transition: color 0.3s;
    }
    .nav a:hover { color: var(--primary); }
    .control-panel {
      background: white;
      border-radius: 10px;
      padding: 20px;
      box-shadow: 0 3px 10px rgba(0,0,0,0.05);
    }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <h1><i class="fas fa-tint"></i> System Zbiornika Wody</h1>
      )rawliteral" + modeBadge + R"rawliteral(
    </header>

    <div class="dashboard">
      <div class="tank-container">
        <h2><i class="fas fa-water"></i> Wizualizacja Zbiornika</h2>
        <div class="tank">
          <div class="water">
            <div class="water-percentage">)rawliteral" + String(waterLevel) + R"rawliteral(%</div>
          </div>
          <div class="sensor high">
            <span class="sensor-label">Górny: )rawliteral" + (high ? "Zanurzony" : "Suchy") + R"rawliteral(</span>
          </div>
          <div class="sensor mid">
            <span class="sensor-label">Środkowy: )rawliteral" + (sensorMidPin != -1 ? (mid ? "Zanurzony" : "Suchy") : "Nieaktywny") + R"rawliteral(</span>
          </div>
          <div class="sensor low">
            <span class="sensor-label">Dolny: )rawliteral" + (low ? "Zanurzony" : "Suchy") + R"rawliteral(</span>
          </div>
        </div>
      </div>

      <div>
        )rawliteral" + content + R"rawliteral(
        <div class="control-panel" style="margin-top:20px;">
          <h3><i class="fas fa-info-circle"></i> Status Systemu</h3>
          <div class="status-indicator">
            <div class="status-dot )rawliteral" + (pumpOn ? "status-on" : "status-off") + R"rawliteral("></div>
            <span>Pompa: )rawliteral" + (pumpOn ? "WŁĄCZONA" : "WYŁĄCZONA") + R"rawliteral(</span>
          </div>
          <div class="status-indicator">
            <div class="status-dot )rawliteral" + (wifiConnected ? "status-on" : "status-off") + R"rawliteral("></div>
            <span>WiFi: )rawliteral" + (wifiConnected ? "Podłączone" : "Rozłączone") + R"rawliteral(</span>
          </div>
          <div class="status-indicator">
            <div class="status-dot )rawliteral" + ((pushoverToken != "" && pushoverUser != "") ? "status-on" : "status-off") + R"rawliteral("></div>
            <span>Powiadomienia: )rawliteral" + ((pushoverToken != "" && pushoverUser != "") ? "Aktywne" : "Nieaktywne") + R"rawliteral(</span>
          </div>
          <div class="status-indicator">
            <div class="status-dot )rawliteral" + (manualButtonPin != -1 ? "status-on" : "status-off") + R"rawliteral("></div>
            <span>Przycisk ręczny: )rawliteral" + (manualButtonPin != -1 ? "Aktywny (GPIO " + String(manualButtonPin) + ")" : "Nieaktywny") + R"rawliteral(</span>
          </div>
        </div>
      </div>
    </div>

    <div class="nav">
      <a href="/"><i class="fas fa-home"></i> Strona Główna</a>
      <a href="/manual"><i class="fas fa-hand-paper"></i> Sterowanie</a>
      <a href="/config"><i class="fas fa-sliders-h"></i> Konfiguracja</a>
      <a href="/log"><i class="fas fa-history"></i> Historia Zdarzeń</a>
    </div>
  </div>
</body>
</html>
)rawliteral";

  return html;
}

void handleStatus() {
  server.send(200, "text/html; charset=utf-8", getStatusHTML());
}

void handleLog() {
  String content = "<div class='control-panel'><h3><i class='fas fa-history'></i> Historia Zdarzeń</h3><ul style='padding-left:20px;'>";
  for (int i = 0; i < EVENT_LIMIT; i++) {
    int idx = (eventIndex + i) % EVENT_LIMIT;
    if (events[idx] != "") content += "<li>" + events[idx] + "</li>";
  }
  content += "</ul></div>";
  server.send(200, "text/html; charset=utf-8", getStatusHTML(content));
}

void handleManual() {
  if (server.method() == HTTP_POST) {
    if (server.hasArg("toggle")) {
      if (canTogglePump(true)) {  // Użyj manualOverride=true dla przełączania ręcznego
        manualMode = true;
        manualModeStartTime = millis();
        pumpOn = !pumpOn;
        digitalWrite(relayPin, pumpOn ? HIGH : LOW);
        lastPumpToggleTime = millis();
        pumpToggleCount++;
        addEvent(String("Ręczne sterowanie POMPA – ") + (pumpOn ? "WŁĄCZONA" : "WYŁĄCZONA"));
        sendPushover(String("Ręczne sterowanie POMPA: ") + (pumpOn ? "włączono" : "wyłączono"));
      }
      server.sendHeader("Location", "/manual");
      server.send(303);
      return;
    } 
    else if (server.hasArg("test")) {
      testMode = !testMode;
      if (testMode) {
        manualMode = true;
        manualModeStartTime = millis();
      } else {
        manualMode = false;
      }
      addEvent(testMode ? "Włączono tryb testowy" : "Wyłączono tryb testowy");
      server.sendHeader("Location", "/manual");
      server.send(303);
      return;
    }
    else if (server.hasArg("auto")) {
      manualMode = false;
      addEvent("Przywrócono sterowanie automatyczne");
      server.sendHeader("Location", "/manual");
      server.send(303);
      return;
    }
  }

  String content = "";
  content += "<div class=\"control-panel\">";
  content += "<div class=\"control-group\">";
  content += "<h3><i class=\"fas fa-cog\"></i> Sterowanie Pompą</h3>";
  content += "<form method=\"POST\" action=\"/manual\" style=\"margin: 0;\">";
  content += "<input type=\"hidden\" name=\"toggle\" value=\"1\">";
  content += "<button type=\"submit\" class=\"btn btn-pump\">";
  content += "<i class=\"fas fa-power-off\"></i> ";
  content += pumpOn ? "WYŁĄCZ POMPĘ" : "WŁĄCZ POMPĘ";
  content += "</button></form></div>";

  content += "<div class=\"control-group\">";
  content += "<h3><i class=\"fas fa-vial\"></i> Tryb Testowy</h3>";
  content += "<form method=\"POST\" action=\"/manual\" style=\"margin: 0;\">";
  content += "<button type=\"submit\" name=\"test\" class=\"btn ";
  content += testMode ? "btn-danger" : "btn-primary";
  content += "\"><i class=\"fas fa-flask\"></i> ";
  content += testMode ? "Wyłącz Tryb Testowy" : "Włącz Tryb Testowy";
  content += "</button></form></div>";

  if (manualMode && !testMode) {
    content += "<div class='control-group'>";
    content += "<h3><i class='fas fa-robot'></i> Sterowanie Automatyczne</h3>";
    content += "<form method='POST' action='/manual' style='margin: 0;'>";
    content += "<button type='submit' name='auto' class='btn btn-secondary'>";
    content += "<i class='fas fa-redo'></i> Przywróć Automat";
    content += "</button></form></div>";
  }

  content += "</div>"; // .control-panel

  server.send(200, "text/html; charset=utf-8", getStatusHTML(content));
}

void handleConfigForm() {
  String content = R"rawliteral(
  <div class="control-panel">
    <h3><i class="fas fa-sliders-h"></i> Konfiguracja</h3>
    <form action='/save'>
      <label>Pin DOLNY:</label><br><input name='low' required><br><br>
      <label>Pin GÓRNY:</label><br><input name='high' required><br><br>
      <label>Pin ŚRODKOWY:</label><br><input name='mid'><br><br>
      <label>Pin przekaźnika:</label><br><input name='relay' required><br><br>
      <label>Pin przycisku ręcznego (opcjonalnie):</label><br><input name='button'><br><br>
      <label>SSID Wi-Fi:</label><br><input name='ssid'><br><br>
      <label>Hasło Wi-Fi:</label><br><input name='pass'><br><br>
      <label>Token Pushover:</label><br><input name='token'><br><br>
      <label>Użytkownik Pushover:</label><br><input name='user'><br><br>
      <input type='submit' class='btn btn-primary' value='Zapisz'>
    </form>
  </div>
  )rawliteral";

  server.send(200, "text/html; charset=utf-8", getStatusHTML(content));
}

void handleSave() {
  int low = server.arg("low").toInt();
  int high = server.arg("high").toInt();
  int mid = server.hasArg("mid") ? server.arg("mid").toInt() : -1;
  int relay = server.arg("relay").toInt();
  int button = server.hasArg("button") ? server.arg("button").toInt() : -1;
  String s = server.arg("ssid");
  String p = server.arg("pass");
  String token = server.arg("token");
  String user = server.arg("user");
  saveConfig(low, high, mid, relay, button, s, p, token, user);
  server.send(200, "text/html; charset=utf-8", "<h3>Zapisano konfigurację. Restart...</h3>");
  delay(2000);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Inicjalizacja WiFiClientSecure...");
  WiFiClientSecure client;
  client.setInsecure();
  loadConfig();
  server.on("/manual", HTTP_POST, handleManual);

  // Inicjalizacja watchdoga
  watchdogTimer = timerBegin(0);
  timerAttachInterrupt(watchdogTimer, &resetModule);
  timerAlarm(watchdogTimer, 5000000, false, 0);
  
  if (!isConfigured) {
    WiFi.softAP(apSSID, apPASS);
    server.on("/", handleConfigForm);
    server.on("/save", handleSave);
    server.begin();
    return;
  }

  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) delay(500);
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    addEvent("Połączono z Wi-Fi: " + WiFi.localIP().toString());
    sendPushover("Urządzenie online: " + WiFi.localIP().toString());
  } else {
    WiFi.softAP("ESP32-WaterMonitor", "pompa123");
    addEvent("Tryb offline - AP");
  }

  setupPins();
  server.on("/", handleStatus);
  server.on("/config", handleConfigForm);
  server.on("/save", handleSave);
  server.on("/manual", handleManual);
  server.on("/log", handleLog);
  server.begin();

  MDNS.begin("esp32");
  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
    else if (upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
    else if (upload.status == UPLOAD_FILE_END) Update.end(true);
  });
}

void loop() {
  timerWrite(watchdogTimer, 0); // restartuj timer, by uniknąć resetu

  server.handleClient();
  static unsigned long lastWifiCheck = 0;
  
  // Sprawdzaj połączenie WiFi co 60 sekund
  if (millis() - lastWifiCheck > 60000) {
    lastWifiCheck = millis();
    bool wasConnected = wifiConnected;
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    
    if (wifiConnected && !wasConnected) {
      addEvent("Ponownie połączono z WiFi");
      sendPushover("Urządzenie ponownie online");
    }
    else if (!wifiConnected && wasConnected) {
      addEvent("Utracono połączenie WiFi");
    }
  }
  
  // Sprawdź czy minął czas trybu manualnego
  if (manualMode && !testMode && (millis() - manualModeStartTime > manualModeTimeout)) {
    manualMode = false;
    addEvent("Automatyczne wyłączenie trybu manualnego po 30 minutach");
  }

  // Obsługa przycisku ręcznego (jeśli skonfigurowany)
  if (manualButtonPin != -1) {
    // Odczytaj aktualny stan przycisku z debouncingiem
    bool reading = digitalRead(manualButtonPin);
    
    if (reading != lastButtonState) {
      lastButtonDebounceTime = millis();
    }
    
    if ((millis() - lastButtonDebounceTime) > buttonDebounceDelay) {
      // Stan przycisku jest stabilny
      if (reading != buttonState) {
        buttonState = reading;
        
        // Sprawdź czy przycisk został naciśnięty (przejście z HIGH na LOW)
        if (buttonState == LOW && lastStableButtonState == HIGH) {
          // Sprawdź czy minął minimalny czas od ostatniego naciśnięcia
          if (millis() - lastButtonPressTime > buttonPressDelay) {
            lastButtonPressTime = millis();
            
            // Przełącz pompę (pomijając zabezpieczenia czasowe)
            if (canTogglePump(true)) {
              manualMode = true;
              manualModeStartTime = millis();
              pumpOn = !pumpOn;
              digitalWrite(relayPin, pumpOn ? HIGH : LOW);
              lastPumpToggleTime = millis();
              addEvent(String("Przycisk BOOT POMPA – ") + (pumpOn ? "WŁĄCZONA" : "WYŁĄCZONA"));
              sendPushover(String("Przycisk BOOT POMPA: ") + (pumpOn ? "włączono" : "wyłączono"));
            }
          }
        }
      }
    }
    
    lastButtonState = reading;
    lastStableButtonState = buttonState;
  }

  // Odczytaj aktualne stany czujników
  bool currentLow = digitalRead(sensorLowPin) == LOW;
  bool currentHigh = digitalRead(sensorHighPin) == LOW;
  bool currentMid = (sensorMidPin != -1) ? (digitalRead(sensorMidPin) == LOW) : false;

  // Sprawdź czy stan czujników się zmienił
  if (currentLow != lastLowState || currentHigh != lastHighState || currentMid != lastMidState) {
    lastSensorChangeTime = millis();
    lastLowState = currentLow;
    lastHighState = currentHigh;
    lastMidState = currentMid;
  }

  // Automatyczne sterowanie działa tylko gdy nie jesteśmy w trybie manualnym/testowym
  if (!manualMode && !testMode) {
    // Sprawdź czy minęło wystarczająco czasu od ostatniej zmiany stanu czujników
    if (millis() - lastSensorChangeTime > sensorDebounceTime) {
      if (currentHigh && pumpOn && canTogglePump()) {
        digitalWrite(relayPin, LOW);
        pumpOn = false;
        lastPumpToggleTime = millis();
        pumpToggleCount++;
        addEvent("Automatyczne wyłączenie pompy (górny czujnik)");
        sendPushover("Pompa została automatycznie wyłączona - zbiornik pełny");
      }
      else if (!currentLow && !pumpOn && canTogglePump()) {
        digitalWrite(relayPin, HIGH);
        pumpOn = true;
        lastPumpToggleTime = millis();
        pumpToggleCount++;
        addEvent("Automatyczne włączenie pompy (brak wody)");
        sendPushover("Pompa została automatycznie włączona - niski poziom wody");
      }
    }
  }
}