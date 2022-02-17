/****************************************************************************************************************************
  Async_ConfigOnDoubleReset_minimal.ino
  For ESP8266 / ESP32 boards
  Built by Khoi Hoang https://github.com/khoih-prog/ESPAsync_WiFiManager
  Licensed under MIT license
 *****************************************************************************************************************************/
#if !(defined(ESP32) )
  #error This code is intended to run on the ESP32 platform! Please check your Tools->Board setting.
#endif

#if defined(ESP32)
  #define USE_SPIFFS            true
  #define ESP_DRD_USE_EEPROM    true
#else  
  #error This code is intended to run on the ESP32 platform! Please check your Tools->Board setting.  
#endif
#include <ESPAsync_WiFiManager.h>               //https://github.com/khoih-prog/ESPAsync_WiFiManager
// Redundant, for v1.10.0 only
//#include <ESPAsync_WiFiManager-Impl.h>          //https://github.com/khoih-prog/ESPAsync_WiFiManager
#define DRD_TIMEOUT             10
#define DRD_ADDRESS             0
#include <ESP_DoubleResetDetector.h>            //https://github.com/khoih-prog/ESP_DoubleResetDetector
DoubleResetDetector* drd;
const int PIN_LED       = 2;
bool      initialConfig = false;
AsyncWebServer webServer(80);
#if !( USING_ESP32_S2 || USING_ESP32_C3 )
DNSServer dnsServer;
#endif

void setup() {
  pinMode(PIN_LED, OUTPUT);
  Serial.begin(115200); while (!Serial); delay(200);
  Serial.print(F("\nStarting Async_ConfigOnDoubleReset_minimal on ")); Serial.println(ARDUINO_BOARD);
  Serial.println(ESP_ASYNC_WIFIMANAGER_VERSION); 
  drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);
  if (drd->detectDoubleReset()) { Serial.println(F("DRD")); initialConfig = true; }
#if ( USING_ESP32_S2 || USING_ESP32_C3 )
  ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, NULL, "Async_ConfigOnDRD");
#else
  ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer, "Async_ConfigOnDRD");
#endif
  if (ESPAsync_wifiManager.WiFi_SSID() == "") { Serial.println(F("No AP credentials")); initialConfig = true; }
  if (initialConfig) {
    Serial.println(F("Starting Config Portal")); digitalWrite(PIN_LED, HIGH);
    if (!ESPAsync_wifiManager.startConfigPortal()) { Serial.println(F("Not connected to WiFi")); }
    else { Serial.println(F("connected")); }
  }
  else { WiFi.mode(WIFI_STA); WiFi.begin(); }
  digitalWrite(PIN_LED, LOW); 
  unsigned long startedAt = millis();
  Serial.print(F("After waiting "));
  int connRes = WiFi.waitForConnectResult();
  float waited = (millis() - startedAt);
  Serial.print(waited / 1000); Serial.print(F(" secs , Connection result is ")); Serial.println(connRes);
  if (WiFi.status() != WL_CONNECTED) { Serial.println(F("Failed to connect")); }
  else { Serial.print(F("Local IP: ")); Serial.println(WiFi.localIP()); }
}
void loop() { drd->loop(); }
