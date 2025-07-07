#ifndef NOTIFIER_H
#define NOTIFIER_H

#include <Arduino.h>

void addEvent(String msg);
void sendPushover(String msg);
String urlEncode(String str);

#endif
