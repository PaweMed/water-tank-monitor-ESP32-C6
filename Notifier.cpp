#include "esp_task_wdt.h"
#include "Notifier.h"
#include "SystemState.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Arduino.h>

void addEvent(String msg) {
  Serial.println(msg);
  SystemState::events[SystemState::eventIndex] = msg;
  SystemState::eventIndex = (SystemState::eventIndex + 1) % SystemState::EVENT_LIMIT;
}

// RAM monitoring helper
void printFreeHeap(const char* where = "") {
  Serial.printf("[RAM] Free heap%s: %lu\n", where, (unsigned long)ESP.getFreeHeap());
}

void sendPushover(String msg) {
  static String lastMessage = "";
  static unsigned long lastSendTime = 0;
  if (msg == lastMessage && millis() - lastSendTime < 30000) {
    Serial.println("[Pushover] Pominięto duplikat wiadomości: " + msg);
    return;
  }
  Serial.println("[Pushover] Próba wysłania: " + msg);
  printFreeHeap(" przed Pushover");

  if (!SystemState::wifiConnected) {
    Serial.println("[Pushover] Błąd: Brak połączenia WiFi");
    return;
  }
  if (SystemState::pushoverToken == "" || SystemState::pushoverUser == "") {
    Serial.println("[Pushover] Błąd: Brak tokenu lub użytkownika");
    return;
  }

  WiFiClientSecure* client = new WiFiClientSecure();
  client->setInsecure();
  client->setTimeout(3000); // Skrócony timeout do 3s

  HTTPClient* https = new HTTPClient();
  https->setTimeout(3000); // Skrócony timeout do 3s

  String url = "https://api.pushover.net/1/messages.json";
  Serial.println("[Pushover] Łączenie z: " + url);

  bool ok = false;
  int httpCode = -1;
  String response = "";

  esp_task_wdt_reset();
  yield();

  if (https->begin(*client, url)) {
    https->addHeader("Content-Type", "application/x-www-form-urlencoded");
    String postData = "token=" + SystemState::pushoverToken +
                      "&user=" + SystemState::pushoverUser +
                      "&message=" + urlEncode(msg) +
                      "&title=Zbiornik z wodą";
    Serial.println("[Pushover] Wysyłane dane: " + postData);

    // W trakcie oczekiwania na POST — regularny reset watchdoga
    unsigned long postStart = millis();
    httpCode = https->POST(postData);
    unsigned long elapsed = millis() - postStart;
    esp_task_wdt_reset();
    yield();

    response = https->getString();
    Serial.println("[Pushover] HTTP Code: " + String(httpCode));
    Serial.println("[Pushover] Odpowiedź: " + response);

    if (httpCode == HTTP_CODE_OK) {
      Serial.println("[Pushover] Wysłano pomyślnie!");
      lastMessage = msg;
      lastSendTime = millis();
      ok = true;
    } else {
      Serial.println("[Pushover] Błąd wysyłania!");
    }
    https->end();
  } else {
    Serial.println("[Pushover] Błąd początkowania połączenia");
  }
  // Upewnij się, że wszystko jest posprzątane (by uniknąć memory leak)
  delete https;
  delete client;
  esp_task_wdt_reset();
  printFreeHeap(" po Pushover");
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
