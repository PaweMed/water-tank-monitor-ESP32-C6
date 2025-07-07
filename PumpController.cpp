#include "PumpController.h"
#include "SystemState.h"
#include "Notifier.h"
#include <WiFi.h>

// NIE wymuszamy trybu 'n only', NIE przełączamy AP po reconnect.

void connectWiFi() {
  Serial.println("Próbuję połączyć się z WiFi SSID: " + SystemState::ssid);
  WiFi.mode(WIFI_STA); // tylko raz na starcie!
  WiFi.begin(SystemState::ssid.c_str(), SystemState::pass.c_str());
  WiFi.setSleep(false);
  int retryCount = 0;

  while (WiFi.status() != WL_CONNECTED && retryCount < 5) {
    delay(1000);
    Serial.print(".");
    retryCount++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    SystemState::wifiConnected = true;
    Serial.println("Połączono z Wi-Fi!");
    Serial.print("Adres IP: ");
    Serial.println(WiFi.localIP());
    addEvent("Połączono z Wi-Fi: " + WiFi.localIP().toString());
    sendPushover("Urządzenie online: " + WiFi.localIP().toString());
    digitalWrite(SystemState::ledPin, HIGH);
  } else {
    Serial.println("Nie udało się połączyć z Wi-Fi.");
    SystemState::wifiConnected = false;
    // NIE przełączamy w tryb AP – tylko na początku przez startConfigAP(server)
  }
}

void handleWiFiReconnect() {
  static unsigned long wifiLostSince = 0;
  static unsigned long lastReconnectAttempt = 0;
  const unsigned long reconnectDelay = 20000;      // co 20 sek. może próbować
  const unsigned long maxOfflineRetry = 300000;    // co 5 minut próbujemy ponownie

  if (WiFi.status() != WL_CONNECTED) {
    if (wifiLostSince == 0) wifiLostSince = millis();

    // próba połączenia co reconnectDelay
    if (millis() - lastReconnectAttempt > reconnectDelay) {
      Serial.println("[WiFi] Próba ponownego połączenia...");
      WiFi.begin(SystemState::ssid.c_str(), SystemState::pass.c_str());
      WiFi.setSleep(false);
      lastReconnectAttempt = millis();
    }

    SystemState::wifiConnected = false;

    if (millis() - wifiLostSince > maxOfflineRetry) {
      Serial.println("[WiFi] Offline ponad 5 minut – kontynuuję działanie lokalnie.");
      wifiLostSince = millis(); // zresetuj licznik, by próbować znów za 5 minut

      // UWAGA: Poniższy restart został WYŁĄCZONY
      /*
      Serial.println("[WiFi] Restartuję ESP – brak WiFi od 5 minut");
      delay(200);
      ESP.restart();
      */
    }
  } else {
    if (!SystemState::wifiConnected) {
      SystemState::wifiConnected = true;
      Serial.println("Ponownie połączono z WiFi. Adres IP: " + WiFi.localIP().toString());
      addEvent("Ponownie połączono z WiFi");
      sendPushover("Urządzenie ponownie online");
      digitalWrite(SystemState::ledPin, HIGH);
    }
    wifiLostSince = 0;
  }
}

void handleManualButton() {
  if (SystemState::manualButtonPin != -1) {
    bool reading = digitalRead(SystemState::manualButtonPin);
    if (reading != SystemState::lastButtonState) {
      SystemState::lastButtonDebounceTime = millis();
    }
    if ((millis() - SystemState::lastButtonDebounceTime) > SystemState::buttonDebounceDelay) {
      if (reading != SystemState::buttonState) {
        SystemState::buttonState = reading;
        if (SystemState::buttonState == LOW && SystemState::lastStableButtonState == HIGH) {
          if (millis() - SystemState::lastButtonPressTime > SystemState::buttonPressDelay) {
            SystemState::lastButtonPressTime = millis();
            SystemState::manualMode = true;
            SystemState::manualModeStartTime = millis();
            SystemState::pumpOn = !SystemState::pumpOn;
            digitalWrite(SystemState::relayPin, SystemState::pumpOn ? HIGH : LOW);
            SystemState::lastPumpToggleTime = millis();
            Serial.println(String("Przycisk aktywowany. Pompa: ") + (SystemState::pumpOn ? "WŁĄCZONA" : "WYŁĄCZONA"));
            addEvent(String("Przycisk POMPA – ") + (SystemState::pumpOn ? "WŁĄCZONA" : "WYŁĄCZONA"));
            sendPushover(String("Przycisk POMPA: ") + (SystemState::pumpOn ? "włączono" : "wyłączono"));
          } else {
            Serial.println("Przycisk BOOT - zignorowano zbyt szybkie naciśnięcie.");
          }
        }
      }
    }
    SystemState::lastButtonState = reading;
    SystemState::lastStableButtonState = SystemState::buttonState;
  }
}

void handleLED() {
  static unsigned long lastLedToggle = 0;
  const unsigned long ledBlinkInterval = 500;
  if (SystemState::wifiConnected) {
    digitalWrite(SystemState::ledPin, HIGH);
  } else {
    if (millis() - lastLedToggle > ledBlinkInterval) {
      lastLedToggle = millis();
      digitalWrite(SystemState::ledPin, !digitalRead(SystemState::ledPin));
    }
  }
}

void handlePumpLogic() {
  if (SystemState::manualMode && !SystemState::testMode && (millis() - SystemState::manualModeStartTime > SystemState::manualModeTimeout)) {
    SystemState::manualMode = false;
    Serial.println("Automatyczne wyłączenie trybu manualnego po 30 minutach.");
    addEvent("Automatyczne wyłączenie trybu manualnego po 30 minutach");
  }

  bool currentLow = digitalRead(SystemState::sensorLowPin) == LOW;
  bool currentHigh = digitalRead(SystemState::sensorHighPin) == LOW;
  bool currentMid = (SystemState::sensorMidPin != -1) ? (digitalRead(SystemState::sensorMidPin) == LOW) : false;
  Serial.print("Stany czujników: ");
  Serial.print("LOW="); Serial.print(currentLow);
  Serial.print(", MID="); Serial.print(currentMid);
  Serial.print(", HIGH="); Serial.println(currentHigh);

  if (currentLow != SystemState::lastLowState || currentHigh != SystemState::lastHighState || currentMid != SystemState::lastMidState) {
    SystemState::lastSensorChangeTime = millis();
    SystemState::lastLowState = currentLow;
    SystemState::lastHighState = currentHigh;
    SystemState::lastMidState = currentMid;
    Serial.println("Wykryto zmianę stanu czujników. Czekam na stabilizację...");
  }

  if (!SystemState::manualMode && !SystemState::testMode) {
    if (millis() - SystemState::lastSensorChangeTime > SystemState::sensorDebounceTime) {
      Serial.println("Czujniki stabilne, sprawdzam automatyczne sterowanie.");
      unsigned long now = millis();
      if (now - SystemState::lastMinuteCheck > 60000) {
        SystemState::pumpToggleCount = 0;
        SystemState::lastMinuteCheck = now;
      }

      if (currentHigh && SystemState::pumpOn &&
          SystemState::pumpToggleCount < SystemState::maxPumpTogglesPerMinute &&
          now - SystemState::lastPumpToggleTime > SystemState::minPumpToggleInterval) {
        digitalWrite(SystemState::relayPin, LOW);
        SystemState::pumpOn = false;
        SystemState::lastPumpToggleTime = millis();
        SystemState::pumpToggleCount++;
        Serial.println("Automatyczne wyłączenie pompy: Górny czujnik zanurzony.");
        addEvent("Automatyczne wyłączenie pompy (górny czujnik)");
        sendPushover("Pompa została automatycznie wyłączona - zbiornik pełny");
      }
      else if (!currentLow && !SystemState::pumpOn &&
          SystemState::pumpToggleCount < SystemState::maxPumpTogglesPerMinute &&
          now - SystemState::lastPumpToggleTime > SystemState::minPumpToggleInterval) {
        digitalWrite(SystemState::relayPin, HIGH);
        SystemState::pumpOn = true;
        SystemState::lastPumpToggleTime = millis();
        SystemState::pumpToggleCount++;
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
}
