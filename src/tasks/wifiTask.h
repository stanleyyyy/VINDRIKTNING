#pragma once

#include <WiFi.h>
#include <ESPAsyncWebServer.h>

void wifiTask(void *pvParameters __attribute__((unused)));
bool wifiReconfigure();
bool wifiReset();
bool wifiIsConnected();
void wifiWaitForConnection();
AsyncWebServer *wifiGetHttpServer();
String wifiHostName();

