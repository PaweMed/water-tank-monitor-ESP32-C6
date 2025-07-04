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

int sensorLowPin = 21;
int sensorHighPin = 23;
int sensorMidPin = -1;
int relayPin = 19;
int manualButtonPin = 4;  // GPIO0 dla przycisku BOOT
int ledPin = 2;

bool isConfigured = false;
bool pumpOn = false;
bool testMode = false;
bool wifiConnected = false;
bool manualMode = false;
unsigned long manualModeStartTime = 0;
const unsigned long manualModeTimeout = 30 * 60 * 1000;
unsigned long lastPumpToggleTime = 0;
const unsigned long minPumpToggleInterval = 30000;
int pumpToggleCount = 0;
const int maxPumpTogglesPerMinute = 4;
unsigned long lastMinuteCheck = 0;
unsigned long lastSensorChangeTime = 0;
bool lastLowState = false;
bool lastHighState = false;
bool lastMidState = false;
const unsigned long sensorDebounceTime = 5000;

bool lastButtonState = HIGH;
bool buttonState = HIGH;
bool lastStableButtonState = HIGH;
unsigned long lastButtonDebounceTime = 0;
const unsigned long buttonDebounceDelay = 50;
unsigned long lastButtonPressTime = 0;
const unsigned long buttonPressDelay = 1000;

String ssid = "iPhone Pawel";
String pass = "12345678";

const char* apSSID = "ESP32-Setup";
const char* apPASS = "12345678";

String pushoverUser = "";
String pushoverToken = "";

#define EVENT_LIMIT 20
String events[EVENT_LIMIT];
int eventIndex = 0;

// OTA autoryzacja
const char* update_username = "admin";
const char* update_password = "admin";

// Watchdog timer
hw_timer_t *watchdogTimer = NULL;
void IRAM_ATTR resetModule() {
  ets_printf("Watchdog reboot\n");
  esp_restart();
}

// --- Zmienne do reconnect WiFi ---
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 15000;
unsigned long wifiLostTime = 0;
// --- Koniec zmiennych ---

bool canTogglePump(bool manualOverride = false) {
  if (manualOverride) return true;
  unsigned long now = millis();
  if (now - lastMinuteCheck > 60000) {
    pumpToggleCount = 0;
    lastMinuteCheck = now;
  }
  if (pumpToggleCount >= maxPumpTogglesPerMinute) {
    addEvent("Osiągnięto limit przełączeń pompy (4/min)");
    sendPushover("Osiągnięto limit przełączeń pompy (4/min) - bezpiecznik");
    return false;
  }
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
  pinMode(sensorLowPin, INPUT_PULLUP);
  pinMode(sensorHighPin, INPUT_PULLUP);
  if (sensorMidPin != -1) pinMode(sensorMidPin, INPUT_PULLUP);
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

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
    #footer-brand {
      position: fixed;
      right: 18px;
      bottom: 10px;
      z-index: 999;
      font-size: 0.9em;
      color: #000;
      opacity: 0.9;
      pointer-events: none;
      font-family: inherit;
      background: rgba(255,255,255,0.88);
      border-radius: 8px 0 0 8px;
      padding: 3px 18px 3px 10px;
      box-shadow: 0 2px 8px rgba(0,0,0,0.06);
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
      <a href="/update"><i class="fas fa-upload"></i> Update</a>
    </div>
    <div id="footer-brand">
      2025 PaweMed v1.2
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
      if (canTogglePump(true)) {
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

  content += "</div>";
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

// --- OTA HANDLER ---
// GET /update (formularz)
void handleUpdateForm() {
  if(!server.authenticate(update_username, update_password)) {
    return server.requestAuthentication();
  }
  String html = R"rawliteral(
    <div class="control-panel">
      <h3><i class="fas fa-upload"></i> Aktualizacja OTA</h3>
      <form method="POST" action="/update" enctype="multipart/form-data" style="margin-bottom: 20px;">
        <input type="file" name="update" required style="margin-bottom: 10px; display: block;">
        <button class="btn btn-primary" type="submit"><i class="fas fa-upload"></i> Wyślij firmware</button>
      </form>
      <p style="color:#444; margin-top: 10px;">
        Po udanej aktualizacji urządzenie uruchomi się ponownie.<br>
        Obsługiwane pliki: <b>.bin</b> z Arduino IDE/PlatformIO.
      </p>
    </div>
  )rawliteral";
  server.send(200, "text/html; charset=utf-8", getStatusHTML(html));
}

void setup() {
  Serial.begin(115200);
  Serial.println("Inicjalizacja WiFiClientSecure...");
  WiFiClientSecure client;
  client.setInsecure();
  loadConfig();
  server.on("/manual", HTTP_POST, handleManual);

  watchdogTimer = timerBegin(0);
  timerAttachInterrupt(watchdogTimer, &resetModule);
  timerAlarm(watchdogTimer, 5000000, false, 0);

  if (!isConfigured) {
    Serial.println("Tryb konfiguracji - brak zapisanych ustawień.");
    Serial.println("Uruchamiam AP: " + String(apSSID) + " z hasłem: " + String(apPASS));
    WiFi.softAP(apSSID, apPASS);
    Serial.print("Adres IP AP: ");
    Serial.println(WiFi.softAPIP());
    server.on("/", handleConfigForm);
    server.on("/save", handleSave);
    server.begin();
    return;
  }

  Serial.println("Wczytano konfigurację.");
  Serial.println("Próbuję połączyć się z WiFi SSID: " + ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  WiFi.setSleep(false);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("Połączono z Wi-Fi!");
    Serial.print("Adres IP: ");
    Serial.println(WiFi.localIP());
    addEvent("Połączono z Wi-Fi: " + WiFi.localIP().toString());
    sendPushover("Urządzenie online: " + WiFi.localIP().toString());
    digitalWrite(ledPin, HIGH);
  } else {
    Serial.println("Nie udało się połączyć z Wi-Fi. Uruchamiam tryb offline (AP).");
    WiFi.softAP("ESP32-WaterMonitor", "pompa123");
    Serial.print("Adres IP AP (tryb offline): ");
    Serial.println(WiFi.softAPIP());
    addEvent("Tryb offline - AP");
  }

  setupPins();
  server.on("/", handleStatus);
  server.on("/config", handleConfigForm);
  server.on("/save", handleSave);
  server.on("/manual", handleManual);
  server.on("/log", handleLog);

  // --- OTA z autoryzacją na GET/POST ---
  server.on("/update", HTTP_GET, handleUpdateForm);
  server.on("/update", HTTP_POST, []() {
    if (!server.authenticate(update_username, update_password)) {
      return server.requestAuthentication();
    }
    server.send(200, "text/html; charset=utf-8",
      "<div class='control-panel'><h3>Aktualizacja " +
      String(Update.hasError() ? "NIEUDANA" : "udana!") + "</h3>" +
      (Update.hasError() ? "<p style='color:red'>Sprawdź plik .bin i spróbuj ponownie.</p>"
                         : "<p style='color:green'>Restartuję urządzenie...</p>") +
      "</div>");
    delay(1200);
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if(upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
    else if(upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
    else if(upload.status == UPLOAD_FILE_END) Update.end(true);
  });
  Serial.println("OTA Update handler skonfigurowany na /update.");

  server.begin();
  Serial.println("Serwer WWW uruchomiony.");

  MDNS.begin("esp32");
  Serial.println("mDNS uruchomiony jako 'esp32.local'");
}

void loop() {
  timerWrite(watchdogTimer, 0);

  server.handleClient();
  static unsigned long lastLedToggle = 0;
  const unsigned long ledBlinkInterval = 500;

  // BLINK LED offline
  if (wifiConnected) {
    digitalWrite(ledPin, HIGH);
  } else {
    if (millis() - lastLedToggle > ledBlinkInterval) {
      lastLedToggle = millis();
      digitalWrite(ledPin, !digitalRead(ledPin));
    }
  }

  // AUTOMATYCZNY reconnect WiFi
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiLostTime == 0) wifiLostTime = millis();
    unsigned long now = millis();
    if (now - lastReconnectAttempt > reconnectInterval) {
      Serial.println("Próba ponownego połączenia z WiFi...");
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      WiFi.begin(ssid.c_str(), pass.c_str());
      WiFi.setSleep(false);
      lastReconnectAttempt = now;
    }
    wifiConnected = false;
  } else {
    if (!wifiConnected) {
      wifiConnected = true;
      Serial.println("Ponownie połączono z WiFi. Adres IP: " + WiFi.localIP().toString());
      addEvent("Ponownie połączono z WiFi");
      sendPushover("Urządzenie ponownie online");
      digitalWrite(ledPin, HIGH);
    }
    wifiLostTime = 0;
  }

  if (manualMode && !testMode && (millis() - manualModeStartTime > manualModeTimeout)) {
    manualMode = false;
    Serial.println("Automatyczne wyłączenie trybu manualnego po 30 minutach.");
    addEvent("Automatyczne wyłączenie trybu manualnego po 30 minutach");
  }

  if (manualButtonPin != -1) {
    bool reading = digitalRead(manualButtonPin);
    if (reading != lastButtonState) {
      lastButtonDebounceTime = millis();
    }
    if ((millis() - lastButtonDebounceTime) > buttonDebounceDelay) {
      if (reading != buttonState) {
        buttonState = reading;
        if (buttonState == LOW && lastStableButtonState == HIGH) {
          if (millis() - lastButtonPressTime > buttonPressDelay) {
            lastButtonPressTime = millis();
            if (canTogglePump(true)) {
              manualMode = true;
              manualModeStartTime = millis();
              pumpOn = !pumpOn;
              digitalWrite(relayPin, pumpOn ? HIGH : LOW);
              lastPumpToggleTime = millis();
              Serial.println(String("Przycisk aktywowany. Pompa: ") + (pumpOn ? "WŁĄCZONA" : "WYŁĄCZONA"));
              addEvent(String("Przycisk POMPA – ") + (pumpOn ? "WŁĄCZONA" : "WYŁĄCZONA"));
              sendPushover(String("Przycisk POMPA: ") + (pumpOn ? "włączono" : "wyłączono"));
            }
          } else {
            Serial.println("Przycisk BOOT - zignorowano zbyt szybkie naciśnięcie.");
          }
        }
      }
    }
    lastButtonState = reading;
    lastStableButtonState = buttonState;
  }

  // --- Najważniejsze: Odczyt czujników (teraz z wejściem PULLUP) ---
  bool currentLow = digitalRead(sensorLowPin) == LOW;
  bool currentHigh = digitalRead(sensorHighPin) == LOW;
  bool currentMid = (sensorMidPin != -1) ? (digitalRead(sensorMidPin) == LOW) : false;
  Serial.print("Stany czujników: ");
  Serial.print("LOW="); Serial.print(currentLow);
  Serial.print(", MID="); Serial.print(currentMid);
  Serial.print(", HIGH="); Serial.println(currentHigh);

  if (currentLow != lastLowState || currentHigh != lastHighState || currentMid != lastMidState) {
    lastSensorChangeTime = millis();
    lastLowState = currentLow;
    lastHighState = currentHigh;
    lastMidState = currentMid;
    Serial.println("Wykryto zmianę stanu czujników. Czekam na stabilizację...");
  }

  // Automatyczne sterowanie: pompa włącza się gdy dolny czujnik suchy i nie jest w trybie manualnym/testowym
  if (!manualMode && !testMode) {
    if (millis() - lastSensorChangeTime > sensorDebounceTime) {
      Serial.println("Czujniki stabilne, sprawdzam automatyczne sterowanie.");
      if (currentHigh && pumpOn && canTogglePump()) {
        digitalWrite(relayPin, LOW);
        pumpOn = false;
        lastPumpToggleTime = millis();
        pumpToggleCount++;
        Serial.println("Automatyczne wyłączenie pompy: Górny czujnik zanurzony.");
        addEvent("Automatyczne wyłączenie pompy (górny czujnik)");
        sendPushover("Pompa została automatycznie wyłączona - zbiornik pełny");
      }
      else if (!currentLow && !pumpOn && canTogglePump()) {
        digitalWrite(relayPin, HIGH);
        pumpOn = true;
        lastPumpToggleTime = millis();
        pumpToggleCount++;
        Serial.println("Automatyczne włączenie pompy: Niski poziom wody (dolny czujnik suchy).");
        addEvent("Automatyczne włączenie pompy (brak wody)");
        sendPushover("Pompa została automatycznie włączona - niski poziom wody");
      } else {
        Serial.println("Warunki dla automatycznego sterowania nie zostały spełnione lub pompa jest już w odpowiednim stanie.");
      }
    } else {
      Serial.println("Debounce czujników w toku...");
    }
  } else {
    Serial.println("Tryb manualny/testowy aktywny - automatyczne sterowanie wyłączone.");
  }
  delay(100);
}
