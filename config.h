#ifndef CONFIG_H
#define CONFIG_H

#include <Preferences.h>
#include <WebServer.h>
#include "SystemState.h"

void loadConfig(Preferences &preferences);
void saveConfig(int low, int high, int mid, int relay, int button, String s, String p, String token, String user, Preferences &preferences);
void setupPins();
void startConfigAP(WebServer &server);

#endif
