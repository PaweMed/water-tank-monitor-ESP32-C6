#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "Config.h"
#include "WebInterface.h"
#include "PumpController.h"
#include "Notifier.h"
#include "SystemState.h"
#include "esp_task_wdt.h"

Preferences preferences;
WebServer server(80);

unsigned long lastHeartbeat = 0;
unsigned long lastRamLog = 0;

void printFreeHeapGlobal(const char* where = "") {
  Serial.printf("[RAM] Free heap%s: %lu\n", where, (unsigned long)ESP.getFreeHeap());
}

void setup() {
  Serial.begin(115200);
  Serial.println("Inicjalizacja ESP32 Water Monitor...");

  loadConfig(preferences);
  setupPins();

  // Poprawny watchdog dla ESP32-C6 (IDF 5+)
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 6000,
    .idle_core_mask = (1 << portGET_CORE_ID()),
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);

  if (!SystemState::isConfigured) {
    startConfigAP(server);
    return;
  }

  connectWiFi();
  setupWebEndpoints(server);

  server.begin();
  Serial.println("Serwer WWW uruchomiony.");
  MDNS.begin("esp32");
  Serial.println("mDNS uruchomiony jako 'esp32.local'");
}

void loop() {
  esp_task_wdt_reset();
  handleWebServer(server);
  handlePumpLogic();
  handleWiFiReconnect();
  handleManualButton();
  handleLED();

  // Heartbeat co 5s
  if (millis() - lastHeartbeat > 5000) {
    Serial.println("[HEARTBEAT] System działa, millis: " + String(millis()));
    lastHeartbeat = millis();
  }

  // Kontrola RAM co 10s
  if (millis() - lastRamLog > 10000) {
    printFreeHeapGlobal(" [main loop]");
    lastRamLog = millis();
  }

  yield();
  delay(10); // Nie blokuj długo loopa!
}
