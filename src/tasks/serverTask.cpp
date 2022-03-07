#include <Arduino.h>

#include <ESPAsyncWebserver.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

#include "WiFi.h"
#include "config.h"
#include "utils.h"
#include "watchdog.h"
#include "display.h"

#include "wifiTask.h"
#include "sensorTask.h"
#include "serverTask.h"
#include "ntpTask.h"

#define OUTPUT_JSON_BUFFER_SIZE 512

class ServerTaskCtx {
private:
	bool m_wifiReconfigureRequested;
	bool m_wifiResetRequested;
public:
	ServerTaskCtx()
	{
		m_wifiReconfigureRequested = false;
		m_wifiResetRequested = false;
	}

	//
	// HTTP handlers
	//

	void indexHandler(AsyncWebServerRequest *request)
	{
		LOG_PRINTF("%s(%d): request from %s\n", __FUNCTION__, __LINE__, request->client()->remoteIP().toString().c_str());

		String body =
		"<!DOCTYPE html>\n"
		"<html>\n"
	#if USE_CO2_SENSOR
		"<title>IKEA VINDRIKTNING + SCD41 co2/temperature/humidity server</title>\n"
	#elif (USE_ENV_SENSOR == 1)
		"<title>IKEA VINDRIKTNING + SHT3X temperature/humidity + QMP6988 pressure server</title>\n"
	#else
		"<title>IKEA VINDRIKTNING server</title>\n"
	#endif
		"<meta charset=\"UTF-8\">\n"
		"<meta http-equiv=\"refresh\" content=\"5\">\n"
		"<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n"
		"<style>\n"
		"html { font-family: Helvetica; display: inline-block; margin: 10px auto}\n"
		"body{margin-top: 0px;} h1 {color: #444444;margin: 50px auto 30px;}\n"
		"p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n"
		"</style>\n"
		"</head>\n"
		"<body>\n"
	#if USE_CO2_SENSOR
		"<h2>IKEA VINDRIKTNING + SCD41 co2/temperature/humidity server</h2>\n"
	#elif (USE_ENV_SENSOR == 1)
		"<h2>IKEA VINDRIKTNING + SHT3X temperature/humidity + QMP6988 pressure server</h2>\n"
	#else
		"<h2>IKEA VINDRIKTNING server</h2>\n"
	#endif
		"(c) 2022 Embedded Softworks, s.r.o."
	#if USE_CO2_SENSOR
		"<br><br>"
		"LEDS (from bottom up): pm2.5, humidity, CO2"
		"<br>";
	#elif (USE_ENV_SENSOR == 1)
		"<br><br>"
		"LEDS (from bottom up): pm2.5, humidity"
		"<br>";
	#else
		"<br>";
	#endif

		uint16_t pm2_5 = 0;
	#if (USE_CO2_SENSOR == 1)
		float temperature = 0;
		float humidity = 0;
		uint16_t co2 = 0;
		if (lastSensorData(pm2_5, temperature, humidity, co2)) {
			body += "<br>";
			body += "<b>PM2.5 value:</b> " + String(pm2_5) + " µg/m³<br>";
			body += "<b>Temperature:</b> " + String(temperature) + " ℃<br>";
			body += "<b>Humidity:</b> " + String(humidity) + " %<br>";
			body += "<b>CO2 level:</b> " + String(co2) + " ppm<br>";
			body += "<br>";
		}
	#elif (USE_ENV_SENSOR == 1)
		float temperature = 0;
		float humidity = 0;
		float pressure = 0;
		if (lastSensorData(pm2_5, temperature, humidity, pressure)) {
			body += "<br>";
			body += "<b>PM2.5 value:</b> " + String(pm2_5) + " µg/m³<br>";
			body += "<b>Temperature:</b> " + String(temperature) + " ℃<br>";
			body += "<b>Humidity:</b> " + String(humidity) + " %<br>";
			body += "<b>Pressure:</b> " + String(pressure) + " kPa<br>";
			body += "<br>";
		}
	#else
		if (lastSensorData(pm2_5)) {
			body += "<br>";
			body += "<b>PM2.5 value: " + String(pm2_5) + " µg/m³</b><br>";
			body += "<br>";
		}
	#endif

		body +=
	#if (USE_CO2_SENSOR == 1)
		"Click <a href=\"/get\">here</a> to retrieve PM2.5, co2, temperature and humidity readings<br>"
	#elif (USE_ENV_SENSOR == 1)
		"Click <a href=\"/get\">here</a> to retrieve PM2.5, temperature, humidity and pressure readings<br>"
	#else
		"Click <a href=\"/get\">here</a> to retrieve PM2.5 readings<br>"
	#endif
		"Click <a href=\"/rssi\">here</a> to get RSSI<br><br>"

		"Click <a href=\"/fan?value=on\">here</a> to turn on the Fan<br>"
		"Click <a href=\"/fan?value=off\">here</a> to turn off the Fan<br><br>"
		"Click <a href=\"/led?value=100\">here</a> to set LED brightness to 100<br>"
		"Click <a href=\"/led?value=10\">here</a> to set LED brightness to 10<br>"
		"Click <a href=\"/led?value=0\">here</a> to set LED brightness to 0<br>"
	#if DEBUG_LEDS
		"Click <a href=\"/ledColor?value=4278190080\">here</a> to set LED color to 0xFF000000<br>"
		"Click <a href=\"/ledColor?value=16711680\">here</a> to set LED color to 0x00FF0000<br>"
		"Click <a href=\"/ledColor?value=65280\">here</a> to set LED color to 0x0000FF00<br>"
		"Click <a href=\"/ledColor?value=255\">here</a> to set LED color to 0x000000FF<br>"
	#endif
		"<br>"
		"Click <a href=\"/reconfigureWifi\">here</a> to reconfigure Wifi<br><br>"
		"Click <a href=\"/resetWifi\">here</a> to erase all Wifi settings<br>"
		"Click <a href=\"/reboot\">here</a> to reboot the device<br>"
		"</body>"
		"</html>";

		request->send(200, "text/html", body);
	}

	void getHandler(AsyncWebServerRequest *request)
	{
		LOG_PRINTF("%s(%d): request from %s\n", __FUNCTION__, __LINE__, request->client()->remoteIP().toString().c_str());
		StaticJsonDocument<OUTPUT_JSON_BUFFER_SIZE> doc;

		uint16_t pm2_5 = 0;
	#if (USE_CO2_SENSOR == 1)
		float temperature = 0;
		float humidity = 0;
		uint16_t co2 = 0;
		if (lastSensorData(pm2_5, temperature, humidity, co2)) {
			doc["co2"] = co2;
			doc["temperature"] = temperature;
			doc["humidity"] = humidity;
			doc["pm2_5"] = pm2_5;
		} else {
			doc["co2"] = 0;
			doc["temperature"] = 0;
			doc["humidity"] = 0;
			doc["pm2_5"] = 0;
		}
	#elif (USE_ENV_SENSOR == 1)
		float temperature = 0;
		float humidity = 0;
		float pressure = 0;
		if (lastSensorData(pm2_5, temperature, humidity, pressure)) {
			doc["pressure"] = pressure;
			doc["temperature"] = temperature;
			doc["humidity"] = humidity;
			doc["pm2_5"] = pm2_5;
		} else {
			doc["pressure"] = 0;
			doc["temperature"] = 0;
			doc["humidity"] = 0;
			doc["pm2_5"] = 0;
		}
	#else
		if (lastSensorData(pm2_5)) {
			doc["pm2_5"] = pm2_5;
		} else {
			doc["pm2_5"] = 0;
		}
	#endif

		// add time parameter
		uint64_t currTimeMs = compensatedMillis();
		doc["currTimeMs"] = currTimeMs;
		doc["currTime"] = msToTimeStr(currTimeMs);
		doc["watchdogTimeToReset"] = msToTimeStr(watchdogTimeToReset());

		char buffer[OUTPUT_JSON_BUFFER_SIZE];
		serializeJson(doc, buffer, sizeof(buffer));

		AsyncWebServerResponse *response = request->beginResponse(200, "application/json", buffer);
		request->send(response);
	}

	void rssiHandler(AsyncWebServerRequest *request)
	{
		StaticJsonDocument<OUTPUT_JSON_BUFFER_SIZE> doc;

		// print the received signal strength:
		long rssi = WiFi.RSSI();
		LOG_PRINTF("signal strength (RSSI): %d dBm\n", rssi);

		doc["rssi"] = rssi;

		// add time parameter
		uint64_t currTimeMs = compensatedMillis();
		doc["currTimeMs"] = currTimeMs;
		doc["currTime"] = msToTimeStr(currTimeMs);
		doc["watchdogTimeToReset"] = msToTimeStr(watchdogTimeToReset());

		char buffer[OUTPUT_JSON_BUFFER_SIZE];
		serializeJson(doc, buffer, sizeof(buffer));
		request->send(200, "application/json", buffer);
	}

	void reconfigureWifiHandler(AsyncWebServerRequest *request)
	{
		String body =
		"<!DOCTYPE html>"
		"<html>"
		"<meta http-equiv=\"refresh\" content=\"2; url=/index\">"
		"<body>"
		"WiFi reconfiguration initiated"
		"</body>";
		request->send(200, "text/html", body);
		m_wifiReconfigureRequested = true;
	}

	void resetWifi(AsyncWebServerRequest *request)
	{
		String body =
		"<!DOCTYPE html>"
		"<html>"
		"<meta http-equiv=\"refresh\" content=\"2; url=/index\">"
		"<body>"
		"Resetting WiFi..."
		"</body>";
		request->send(200, "text/html", body);

		// give it enough time to deliver response back to caller
		delay(100);

		// now reset
		m_wifiResetRequested = true;
	}

	void rebootHandler(AsyncWebServerRequest *request)
	{
		String body =
		"<!DOCTYPE html>"
		"<html>"
		"<meta http-equiv=\"refresh\" content=\"10; url=/index\">"
		"<body>"
		"Rebooting..."
		"</body>";
		request->send(200, "text/html", body);

		// schedule reboot
		watchdogScheduleReboot();
	}

	void ledHandler(AsyncWebServerRequest *request)
	{
		StaticJsonDocument<OUTPUT_JSON_BUFFER_SIZE> doc;

		if (request->hasParam("value")) {
			int value = atoi(request->getParam("value")->value().c_str());
			LOG_PRINTF("LED value: %d\n", value);
			Display::instance().setLedBrightness(value);
			request->redirect("/index");
		} else {
			request->send(404, "text/plain", "Not found");
		}
	}

	void fanHandler(AsyncWebServerRequest *request)
	{
		StaticJsonDocument<OUTPUT_JSON_BUFFER_SIZE> doc;

		if (request->hasParam("value")) {
			String value = request->getParam("value")->value().c_str();
			LOG_PRINTF("FAN value: %d\n", value.c_str());
			if (value == "on") {
				sensorFanMode(true);
			} else {
				sensorFanMode(false);
			}
			request->redirect("/index");
		} else {
			request->send(404, "text/plain", "Not found");
		}
	}

	#if DEBUG_LEDS
	void ledColorHandler(AsyncWebServerRequest *request)
	{
		StaticJsonDocument<OUTPUT_JSON_BUFFER_SIZE> doc;

		if (request->hasParam("value")) {
			int value = atoi(request->getParam("value")->value().c_str());
			LOG_PRINTF("LED color value: %d\n", value);
			Display::instance().setColor(value, PM_LED);
			request->redirect("/index");
		} else {
			request->send(404, "text/plain", "Not found");
		}
	}
	#endif

	void task()
	{
		AsyncWebServer *server = wifiGetHttpServer();
		bool shallInitServer = true;

		while (1) {

			//
			// init server handlers if necessary
			//

			if (shallInitServer) {
				shallInitServer = false;

				server->on("/", HTTP_GET, [=](AsyncWebServerRequest *request){
					indexHandler(request);
				});

				server->on("/index", HTTP_GET, [=](AsyncWebServerRequest *request){
					indexHandler(request);
				});

				server->on("/get", HTTP_GET, [=](AsyncWebServerRequest *request){
					getHandler(request);
				});

				server->on("/rssi", HTTP_GET, [=](AsyncWebServerRequest *request){
					rssiHandler(request);
				});

				server->on("/led", HTTP_GET, [=](AsyncWebServerRequest *request){
					ledHandler(request);
				});

				server->on("/fan", HTTP_GET, [=](AsyncWebServerRequest *request){
					fanHandler(request);
				});

#if DEBUG_LEDS
				server->on("/ledColor", HTTP_GET, [=](AsyncWebServerRequest *request){
					ledColorHandler(request);
				});
#endif

				server->on("/reconfigureWifi", HTTP_GET, [=](AsyncWebServerRequest *request){
					reconfigureWifiHandler(request);
				});

				server->on("/resetWifi", HTTP_GET, [=](AsyncWebServerRequest *request){
					resetWifi(request);
				});

				server->on("/reboot", HTTP_GET, [=](AsyncWebServerRequest *request){
					rebootHandler(request);
				});

				server->onNotFound([=](AsyncWebServerRequest *request){
					request->send(404, "text/plain", "Not found");
				});

				server->begin();

				if (!MDNS.begin(wifiHostName().c_str())) {
					LOG_PRINTF("Error starting MDNS responder!\n");
				}

				// Add service to MDNS-SD so our webserver can be located
				MDNS.addService("http", "tcp", 80);
			}

			//
			// handle wifi reconfigure request
			//

			if (m_wifiReconfigureRequested) {
				m_wifiReconfigureRequested = false;
				LOG_PRINTF("WiFi reconfiguration requested\n");

				// reset server handlers
				server->reset();

				// init wifi reconfiguration
				wifiReconfigure();

				// and now we have to re-init the server again
				shallInitServer = true;
			}

			//
			// handle wifi reset request
			//

			if (m_wifiResetRequested) {
				m_wifiResetRequested = false;
				LOG_PRINTF("WiFi reset requested\n");

				// reset server handlers
				server->reset();

				// init wifi reset
				wifiReset();

				// and now we have to re-init the server again
				shallInitServer = true;
			}

			delay(100);
		}
	}

};

static ServerTaskCtx g_ctx;

void serverTask(void *pvParameters __attribute__((unused)))
{
	g_ctx.task();
}
