#ifndef SYSTEMSTATE_H
#define SYSTEMSTATE_H

#include <Arduino.h>

namespace SystemState {
  extern int sensorLowPin;
  extern int sensorHighPin;
  extern int sensorMidPin;
  extern int relayPin;
  extern int manualButtonPin;
  extern int ledPin;

  extern bool isConfigured;
  extern bool pumpOn;
  extern bool testMode;
  extern bool wifiConnected;
  extern bool manualMode;
  extern unsigned long manualModeStartTime;
  extern const unsigned long manualModeTimeout;
  extern unsigned long lastPumpToggleTime;
  extern const unsigned long minPumpToggleInterval;
  extern int pumpToggleCount;
  extern const int maxPumpTogglesPerMinute;
  extern unsigned long lastMinuteCheck;
  extern unsigned long lastSensorChangeTime;
  extern bool lastLowState;
  extern bool lastHighState;
  extern bool lastMidState;
  extern const unsigned long sensorDebounceTime;
  extern bool lastButtonState;
  extern bool buttonState;
  extern bool lastStableButtonState;
  extern unsigned long lastButtonDebounceTime;
  extern const unsigned long buttonDebounceDelay;
  extern unsigned long lastButtonPressTime;
  extern const unsigned long buttonPressDelay;

  extern String ssid;
  extern String pass;
  extern const char* apSSID;
  extern const char* apPASS;
  extern String pushoverUser;
  extern String pushoverToken;

  const int EVENT_LIMIT = 20;
  extern String events[EVENT_LIMIT];
  extern int eventIndex;
}

#endif
