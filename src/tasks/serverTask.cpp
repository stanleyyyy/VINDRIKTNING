#include <Arduino.h>
#include "WiFi.h"
#include "../config/config.h"
#include "../config/secrets.h"
#include "../utils/utils.h"
#include "../utils/watchdog.h"
#include "../utils/display.h"

#include "wifiTask.h"
#include "sensorTask.h"
#include "serverTask.h"
#include "ntpTask.h"

// this needs PathVariableHandlers library
#include <UrlTokenBindings.h>
#include <RichHttpServer.h>

using namespace std::placeholders;
using RichHttpConfig = RichHttp::Generics::Configs::EspressifBuiltin;
using RequestContext = RichHttpConfig::RequestContextType;

class Context {
public:
	// http server
	SimpleAuthProvider m_authProvider;
	RichHttpServer<RichHttpConfig> m_server;

	Context()
		: m_server(80, m_authProvider)
	{
		//
	}
};

static Context g_ctx;

//
// HTTP handlers
//

void indexHandler(RequestContext& request)
{
	const char body[] =
	"IKEA VINDRIKTNING + SCD41 co2/temperature/humidity server <br>"
	"(c) 2022 Embedded Softworks, s.r.o. <br>"
	"<br>"
	"LEDS (from bottom up): pm2.5, humidity, CO2"
	"<br>"
	"<br>"
#if 0
	"Click <a href=\"/info\">here</a> to show sensor information<br>"
#endif
	"Click <a href=\"/get\">here</a> to retrieve co2, temperature and humidity readings<br>"
	"Click <a href=\"/rssi\">here</a> to get RSSI<br><br>"
	"Click <a href=\"/led/100\">here</a> to set LED brightness to 100<br>"
	"Click <a href=\"/led/0\">here</a> to set LED brightness to 0<br>"
#if DEBUG_LEDS
	"Click <a href=\"/ledColor/4278190080\">here</a> to set LED color to 0xFF000000<br>"
	"Click <a href=\"/ledColor/16711680\">here</a> to set LED color to 0x00FF0000<br>"
	"Click <a href=\"/ledColor/65280\">here</a> to set LED color to 0x0000FF00<br>"
	"Click <a href=\"/ledColor/255\">here</a> to set LED color to 0x000000FF<br>"
#endif
	"Click <a href=\"/reboot\">here</a> to reboot the device<br>";

	request.response.sendRaw(200, "text/html", body);
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
		.buildHandler("/reboot")
		.setDisableAuthOverride()
		.on(HTTP_GET, rebootHandler);

	g_ctx.m_server.clearBuilders();
	g_ctx.m_server.begin();

	while (1) {
		// handle one client
		g_ctx.m_server.handleClient();
		delay(100);
	}
}
