#ifndef WEBINTERFACE_H
#define WEBINTERFACE_H

#include <WebServer.h>

void setupWebEndpoints(WebServer &server);
void handleWebServer(WebServer &server);
void handleConfigForm(WebServer &server);
void handleSave(WebServer &server);
void handleUpdate(WebServer &server);

#endif
