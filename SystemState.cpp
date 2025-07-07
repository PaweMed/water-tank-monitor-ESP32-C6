#include "SystemState.h"

namespace SystemState {
  int sensorLowPin = 21;
  int sensorHighPin = 23;
  int sensorMidPin = -1;
  int relayPin = 19;
  int manualButtonPin = 4;
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

  String events[EVENT_LIMIT];
  int eventIndex = 0;
}
