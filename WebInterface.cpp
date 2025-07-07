#include "WebInterface.h"
#include "SystemState.h"
#include "Notifier.h"
#include "Config.h"
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>

// --- DEKLARACJA, żeby nie było błędu ---
String getStatusHTML(String content = "");

// ---------- PANEL OTA ----------
String getUpdatePanelHTML() {
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
  return html;
}

String getUpdateResultPanelHTML(bool success) {
  String html = "<div class='control-panel'><h3>Aktualizacja " +
                String(success ? "udana!" : "NIEUDANA") + "</h3>" +
                (success
                    ? "<p style='color:green'>Restartuję urządzenie...</p>"
                    : "<p style='color:red'>Sprawdź plik .bin i spróbuj ponownie.</p>") +
                "</div>";
  return html;
}

// ---------- HANDLER OTA JAKO ZAKŁADKA ----------
void handleUpdate(WebServer &server) {
  static bool uploadSuccess = false;
  if (server.method() == HTTP_POST) {
    HTTPUpload& upload = server.upload();
    if (!server.authenticate("admin", "admin")) {
      return server.requestAuthentication();
    }
    if (upload.status == UPLOAD_FILE_START) {
      uploadSuccess = Update.begin(UPDATE_SIZE_UNKNOWN);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (uploadSuccess)
        uploadSuccess = Update.write(upload.buf, upload.currentSize) == upload.currentSize;
    } else if (upload.status == UPLOAD_FILE_END) {
      uploadSuccess = Update.end(true);
      server.send(200, "text/html; charset=utf-8", getStatusHTML(getUpdateResultPanelHTML(uploadSuccess)));
      delay(1500);
      if (!uploadSuccess) {
        Serial.println("[OTA] Nieudana aktualizacja. Czyszczenie konfiguracji.");
        Preferences preferences;
        preferences.begin("config", false);
        preferences.clear();
        preferences.end();
      }
      ESP.restart();
      return;
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      Update.end();
      uploadSuccess = false;
    }
    return;
  }
  // GET: wyświetl panel update z prawej (w layoucie!)
  if (!server.authenticate("admin", "admin")) {
    return server.requestAuthentication();
  }
  server.send(200, "text/html; charset=utf-8", getStatusHTML(getUpdatePanelHTML()));
}

// ---------- INNE HANDLERY ----------
void handleStatus(WebServer &server) {
  server.send(200, "text/html; charset=utf-8", getStatusHTML());
}

void handleLog(WebServer &server) {
  String content = "<div class='control-panel'><h3><i class='fas fa-history'></i> Historia Zdarzeń</h3><ul style='padding-left:20px;'>";
  for (int i = 0; i < SystemState::EVENT_LIMIT; i++) {
    int idx = (SystemState::eventIndex + i) % SystemState::EVENT_LIMIT;
    if (SystemState::events[idx] != "") content += "<li>" + SystemState::events[idx] + "</li>";
  }
  content += "</ul></div>";
  server.send(200, "text/html; charset=utf-8", getStatusHTML(content));
}

void handleManual(WebServer &server) {
  if (server.method() == HTTP_POST) {
    if (server.hasArg("toggle")) {
      SystemState::manualMode = true;
      SystemState::manualModeStartTime = millis();
      SystemState::pumpOn = !SystemState::pumpOn;
      digitalWrite(SystemState::relayPin, SystemState::pumpOn ? HIGH : LOW);
      SystemState::lastPumpToggleTime = millis();
      SystemState::pumpToggleCount++;
      addEvent(String("Ręczne sterowanie POMPA – ") + (SystemState::pumpOn ? "WŁĄCZONA" : "WYŁĄCZONA"));
      sendPushover(String("Ręczne sterowanie POMPA: ") + (SystemState::pumpOn ? "włączono" : "wyłączono"));
      server.sendHeader("Location", "/manual");
      server.send(303);
      return;
    }
    else if (server.hasArg("test")) {
      SystemState::testMode = !SystemState::testMode;
      if (SystemState::testMode) {
        SystemState::manualMode = true;
        SystemState::manualModeStartTime = millis();
      } else {
        SystemState::manualMode = false;
      }
      addEvent(SystemState::testMode ? "Włączono tryb testowy" : "Wyłączono tryb testowy");
      server.sendHeader("Location", "/manual");
      server.send(303);
      return;
    }
    else if (server.hasArg("auto")) {
      SystemState::manualMode = false;
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
  content += SystemState::pumpOn ? "WŁĄCZ POMPĘ" : "WYŁĄCZ POMPĘ";
  content += "</button></form></div>";

  content += "<div class=\"control-group\">";
  content += "<h3><i class=\"fas fa-vial\"></i> Tryb Testowy</h3>";
  content += "<form method=\"POST\" action=\"/manual\" style=\"margin: 0;\">";
  content += "<button type=\"submit\" name=\"test\" class=\"btn ";
  content += SystemState::testMode ? "btn-danger" : "btn-primary";
  content += "\"><i class=\"fas fa-flask\"></i> ";
  content += SystemState::testMode ? "Wyłącz Tryb Testowy" : "Włącz Tryb Testowy";
  content += "</button></form></div>";
  if (SystemState::manualMode && !SystemState::testMode) {
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

void handleConfigForm(WebServer &server) {
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

void handleSave(WebServer &server) {
  Preferences preferences;
  int low = server.arg("low").toInt();
  int high = server.arg("high").toInt();
  int mid = server.hasArg("mid") ? server.arg("mid").toInt() : -1;
  int relay = server.arg("relay").toInt();
  int button = server.hasArg("button") ? server.arg("button").toInt() : -1;
  String s = server.arg("ssid");
  String p = server.arg("pass");
  String token = server.arg("token");
  String user = server.arg("user");
  saveConfig(low, high, mid, relay, button, s, p, token, user, preferences);
  server.send(200, "text/html; charset=utf-8", "<h3>Zapisano konfigurację. Restart...</h3>");
  delay(2000);
  ESP.restart();
}

// ---------- ROUTING ZAKŁADEK ----------
void setupWebEndpoints(WebServer &server) {
  server.on("/", HTTP_GET, [&server]() { handleStatus(server); });
  server.on("/config", HTTP_GET, [&server]() { handleConfigForm(server); });
  server.on("/save", HTTP_GET, [&server]() { handleSave(server); });
  server.on("/manual", HTTP_GET, [&server]() { handleManual(server); });
  server.on("/manual", HTTP_POST, [&server]() { handleManual(server); });
  server.on("/log", HTTP_GET, [&server]() { handleLog(server); });
  server.on("/update", HTTP_GET, [&server]() { handleUpdate(server); });
  server.on("/update", HTTP_POST, [&server]() { handleUpdate(server); }, [&server]() { handleUpdate(server); });
}

void handleWebServer(WebServer &server) {
  server.handleClient();
}

// ---------- LAYOUT ----------
String getStatusHTML(String content) {
  bool low = SystemState::testMode || digitalRead(SystemState::sensorLowPin) == LOW;
  bool high = SystemState::testMode || digitalRead(SystemState::sensorHighPin) == LOW;
  bool mid = (SystemState::sensorMidPin != -1) ? (SystemState::testMode || digitalRead(SystemState::sensorMidPin) == LOW) : false;
  int waterLevel = 0;
  if (high) waterLevel = 100;
  else if (mid) waterLevel = 65;
  else if (low) waterLevel = 30;
  else waterLevel = 5;

  String modeBadge = "";
  if (SystemState::testMode) {
    modeBadge = "<div class='badge test-mode'><i class='fas fa-flask'></i> Tryb testowy</div>";
  } else if (SystemState::manualMode) {
    unsigned long remaining = (SystemState::manualModeStartTime + SystemState::manualModeTimeout - millis()) / 60000;
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
    .sensor.mid { top: 35%; display: )rawliteral" + (SystemState::sensorMidPin != -1 ? "block" : "none") + R"rawliteral(; }
    .sensor.mid::after { background: )rawliteral" + (mid ? "var(--secondary)" : "var(--danger)") + R"rawliteral(; }
    .sensor.low { top: 70%; }
    .sensor.low::after { background: )rawliteral" + (low ? "var(--secondary)" : "var(--danger)") + R"rawliteral(; }
    .sensor-label {
      position: absolute;
      left: 10px;
      top: 25px;
      font-size: 14px;
      font-weight: 500;
      white-space: nowrap;
      color: #000; 
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
            <div class="water-percentage">)rawliteral" + String(waterLevel) + R"rawliteral(%
            </div>
          </div>
          <div class="sensor high">
            <span class="sensor-label">Górny: )rawliteral" + (high ? "Zanurzony" : "Suchy") + R"rawliteral(</span>
          </div>
          <div class="sensor mid">
            <span class="sensor-label">Środkowy: )rawliteral" + (SystemState::sensorMidPin != -1 ? (mid ? "Zanurzony" : "Suchy") : "Nieaktywny") + R"rawliteral(</span>
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
            <div class="status-dot )rawliteral" + (SystemState::pumpOn ? "status-on" : "status-off") + R"rawliteral("></div>
            <span>Pompa: )rawliteral" + (SystemState::pumpOn ? "WŁĄCZONA" : "WYŁĄCZONA") + R"rawliteral(</span>
          </div>
          <div class="status-indicator">
            <div class="status-dot )rawliteral" + (SystemState::wifiConnected ? "status-on" : "status-off") + R"rawliteral("></div>
            <span>WiFi: )rawliteral" + (SystemState::wifiConnected ? "Podłączone" : "Rozłączone") + R"rawliteral(</span>
          </div>
)rawliteral";

  // Poprawny status Pushover:
  html += String("<div class='status-indicator'>");
  html += String("<div class='status-dot ") + ((SystemState::pushoverToken != "" && SystemState::pushoverUser != "") ? "status-on" : "status-off") + "'></div>";
  html += String("<span>Powiadomienia: ") + ((SystemState::pushoverToken != "" && SystemState::pushoverUser != "") ? "Aktywne" : "Nieaktywne") + "</span>";
  html += "</div>";

  // Poprawny status przycisku manualnego:
  html += String("<div class=\"status-indicator\">")
       + "<div class=\"status-dot "
       + (SystemState::manualButtonPin != -1 ? "status-on" : "status-off")
       + "\"></div>"
       + "<span>Przycisk ręczny: "
       + (SystemState::manualButtonPin != -1 ? "Aktywny (GPIO " + String(SystemState::manualButtonPin) + ")" : "Nieaktywny")
       + "</span></div>";

  html += R"rawliteral(
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
      2025 PaweMed v1.3
    </div>
  </div>
</body>
</html>
)rawliteral";
  return html;
}
