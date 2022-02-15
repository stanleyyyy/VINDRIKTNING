#pragma once

void wifiTask(void *pvParameters __attribute__((unused)));
bool wifiReconfigure();
bool wifiReset();
bool wifiIsConnected();
void wifiWaitForConnection();
String wifiHostName();

