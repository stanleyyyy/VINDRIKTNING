#include <Arduino.h>

#include "WebServer.h"
#include <ESPAsyncWebServer.h>

#include "WiFi.h"
#include "config.h"
#include "secrets.h"
#include "utils.h"
#include "watchdog.h"
#include "display.h"

#include "wifiTask.h"
#include "sensorTask.h"
#include "serverTask.h"
#include "ntpTask.h"

// this needs PathVariableHandlers library
#include <UrlTokenBindings.h>
#include <RichHttpServer.h>

using namespace std::placeholders;

using RichHttpConfig = RichHttp::Generics::Configs::AsyncWebServer;
using RequestContext = RichHttpConfig::RequestContextType;

class Context {
public:
	// http server
	SimpleAuthProvider m_authProvider;
	RichHttpServer<RichHttpConfig> m_server;

	Context()
		: m_server(80, m_authProvider)
	{
	}
};

static Context g_ctx;

//
// HTTP handlers
//

void indexHandler(RequestContext& request)
{
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
	"Click <a href=\"/led/100\">here</a> to set LED brightness to 100<br>"
	"Click <a href=\"/led/10\">here</a> to set LED brightness to 10<br>"
	"Click <a href=\"/led/0\">here</a> to set LED brightness to 0<br>"
#if DEBUG_LEDS
	"Click <a href=\"/ledColor/4278190080\">here</a> to set LED color to 0xFF000000<br>"
	"Click <a href=\"/ledColor/16711680\">here</a> to set LED color to 0x00FF0000<br>"
	"Click <a href=\"/ledColor/65280\">here</a> to set LED color to 0x0000FF00<br>"
	"Click <a href=\"/ledColor/255\">here</a> to set LED color to 0x000000FF<br>"
#endif
	"Click <a href=\"/reconfigureWifi\">here</a> to reconfigure Wifi<br><br>"
	"Click <a href=\"/resetWifi\">here</a> to erase all Wifi settings<br>"
	"Click <a href=\"/reboot\">here</a> to reboot the device<br>"
	"</body>"
	"</html>";

	request.response.sendRaw(200, "text/html", body.c_str());
}

#if 0
void infoHandler(RequestContext& request)
{
	JsonObject tempObj = request.response.json.createNestedObject("temperature");
	tempObj["type"] = "SCD41";

	JsonObject humObj = request.response.json.createNestedObject("humidity");
	humObj["type"] = "SCD41";

	JsonObject co2Obj = request.response.json.createNestedObject("co2");
	co2Obj["type"] = "SCD41";

	// stop potentially previously started measurement
	uint16_t error = g_ctx.m_scd4x.stopPeriodicMeasurement();
	if (error) {
		LOG_PRINTF("Error trying to execute stopPeriodicMeasurement(): ");
		char errorMessage[256];
		errorToString(error, errorMessage, 256);
		LOG_PRINTF("%s\n", errorMessage);
	}

	uint16_t serial0;
	uint16_t serial1;
	uint16_t serial2;

	if (!g_ctx.m_scd4x.getSerialNumber(serial0, serial1, serial2)) {
		char serial[32];
		snprintf(serial, sizeof(serial) - 1, "0x%2x.%2x.%2x", serial0, serial1, serial2);
		LOG_PRINTF("Serial: %s\n", serial);
		co2Obj["serial"] = serial;
	} else {
		co2Obj["serial"] = "unknown";
	}

	uint16_t altitude;
	if (!g_ctx.m_scd4x.getSensorAltitude(altitude)) {
		co2Obj["altitude"] = altitude;
	} else {
		co2Obj["altitude"] = "unknown";
	}

	uint16_t ascEnabled = false;
	if (!g_ctx.m_scd4x.getAutomaticSelfCalibration(ascEnabled)) {
		co2Obj["autoCalibrationEnabled"] = ascEnabled ? "true" : "false";
	} else {
		co2Obj["autoCalibrationEnabled"] = "unknown";
	}

	// re-start measurements
	error = g_ctx.m_scd4x.startPeriodicMeasurement();
	if (error) {
		LOG_PRINTF("Error trying to execute startPeriodicMeasurement(): ");
		char errorMessage[256];
		errorToString(error, errorMessage, 256);
		LOG_PRINTF("%s\n", errorMessage);
	}

	uint64_t currTimeMs = compensatedMillis();
	request.response.json["currTimeMs"] = currTimeMs;
	request.response.json["currTime"] = msToTimeStr(currTimeMs);
	request.response.json["timeToReset"] = msToTimeStr(timeToReset());
}
#endif

void getHandler(RequestContext& request)
{
	uint16_t pm2_5 = 0;
#if (USE_CO2_SENSOR == 1)
	float temperature = 0;
	float humidity = 0;
	uint16_t co2 = 0;
	if (lastSensorData(pm2_5, temperature, humidity, co2)) {
		request.response.json["co2"] = co2;
		request.response.json["temperature"] = temperature;
		request.response.json["humidity"] = humidity;
		request.response.json["pm2_5"] = pm2_5;
	} else {
		request.response.json["co2"] = 0;
		request.response.json["temperature"] = 0;
		request.response.json["humidity"] = 0;
		request.response.json["pm2_5"] = 0;
	}
#elif (USE_ENV_SENSOR == 1)
	float temperature = 0;
	float humidity = 0;
	float pressure = 0;
	if (lastSensorData(pm2_5, temperature, humidity, pressure)) {
		request.response.json["pressure"] = pressure;
		request.response.json["temperature"] = temperature;
		request.response.json["humidity"] = humidity;
		request.response.json["pm2_5"] = pm2_5;
	} else {
		request.response.json["pressure"] = 0;
		request.response.json["temperature"] = 0;
		request.response.json["humidity"] = 0;
		request.response.json["pm2_5"] = 0;
	}
#else
	if (lastSensorData(pm2_5)) {
		request.response.json["pm2_5"] = pm2_5;
	} else {
		request.response.json["pm2_5"] = 0;
	}
#endif

	// add time parameter
	uint64_t currTimeMs = compensatedMillis();
	request.response.json["currTimeMs"] = currTimeMs;
	request.response.json["currTime"] = msToTimeStr(currTimeMs);
	request.response.json["timeToReset"] = msToTimeStr(timeToReset());
}

void rssiHandler(RequestContext& request)
{
	// print the received signal strength:
	long rssi = WiFi.RSSI();
	LOG_PRINTF("signal strength (RSSI): %d dBm\n", rssi);

	request.response.json["rssi"] = rssi;

	// add time parameter
	uint64_t currTimeMs = compensatedMillis();
	request.response.json["currTimeMs"] = currTimeMs;
	request.response.json["currTime"] = msToTimeStr(currTimeMs);
	request.response.json["timeToReset"] = msToTimeStr(timeToReset());
}

void reconfigureWifiHandler(RequestContext& request)
{
	bool success = wifiReconfigure();

	// add time parameter
	uint64_t currTimeMs = compensatedMillis();
	request.response.json["currTimeMs"] = currTimeMs;
	request.response.json["currTime"] = msToTimeStr(currTimeMs);
	request.response.json["timeToReset"] = msToTimeStr(timeToReset());
	request.response.json["success"] = success;
}

void resetWifi(RequestContext& request)
{
	bool success = wifiReset();

	// add time parameter
	uint64_t currTimeMs = compensatedMillis();
	request.response.json["currTimeMs"] = currTimeMs;
	request.response.json["currTime"] = msToTimeStr(currTimeMs);
	request.response.json["timeToReset"] = msToTimeStr(timeToReset());
	request.response.json["success"] = success;
}

void rebootHandler(RequestContext& request)
{
	// add time parameter
	uint64_t currTimeMs = compensatedMillis();
	request.response.json["currTimeMs"] = currTimeMs;
	request.response.json["currTime"] = msToTimeStr(currTimeMs);
	request.response.json["timeToReset"] = msToTimeStr(timeToReset());

	// schedule reboot
	watchdogScheduleReboot();
}

void configureHandler(RequestContext& request)
{
	const char *opParam = request.pathVariables.get("operation");
	const char *valParam = request.pathVariables.get("value");
	if (!opParam || !valParam) {
		LOG_PRINTF("Please use /<operation>/<value> path!\n");
		request.response.json["error"] = "invalid";
	} else {

		// what configuration operation was requested?
		String opParamStr(opParam);

		if (opParamStr == "led") {
			int value = atoi(valParam);
			LOG_PRINTF("LED value: %d\n", value);
			Display::instance().setLedBrightness(value);
			request.response.json["led"] = value;
		}
#if DEBUG_LEDS
		else if (opParamStr == "ledColor") {
			int value = atoi(valParam);
			LOG_PRINTF("LED color value: %d\n", value);
			Display::instance().setColor(value, PM_LED);
			request.response.json["led"] = value;
		}
#endif
	}

	// add time parameter
	uint64_t currTimeMs = compensatedMillis();
	request.response.json["currTimeMs"] = currTimeMs;
	request.response.json["currTime"] = msToTimeStr(currTimeMs);
	request.response.json["timeToReset"] = msToTimeStr(timeToReset());
}

void serverTask(void *pvParameters __attribute__((unused)))
{
	g_ctx.m_server
		.buildHandler("/")
		.setDisableAuthOverride()
		.on(HTTP_GET, indexHandler);

#if 0
	g_ctx.m_server
		.buildHandler("/info")
		.setDisableAuthOverride()
		.on(HTTP_GET, infoHandler);
#endif

	g_ctx.m_server
		.buildHandler("/get")
		.setDisableAuthOverride()
		.on(HTTP_GET, getHandler);

	g_ctx.m_server
		.buildHandler("/rssi")
		.setDisableAuthOverride()
		.on(HTTP_GET, rssiHandler);

	g_ctx.m_server
		.buildHandler("/:operation/:value")
		.setDisableAuthOverride()
		.on(HTTP_GET, configureHandler);

	g_ctx.m_server
		.buildHandler("/reconfigureWifi")
		.setDisableAuthOverride()
		.on(HTTP_GET, reconfigureWifiHandler);

	g_ctx.m_server
		.buildHandler("/resetWifi")
		.setDisableAuthOverride()
		.on(HTTP_GET, resetWifi);

	g_ctx.m_server
		.buildHandler("/reboot")
		.setDisableAuthOverride()
		.on(HTTP_GET, rebootHandler);

	g_ctx.m_server.clearBuilders();
	g_ctx.m_server.begin();

	while (1) {
		delay(100);
	}
}
