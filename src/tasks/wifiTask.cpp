#include <Arduino.h>
#include "WiFi.h"
#include "../config/config.h"
#include "../config/secrets.h"
#include "../utils/utils.h"
#include "../utils/watchdog.h"

#include "wifiTask.h"

static wl_status_t g_prevWirelessStatus = WL_NO_SHIELD;
static volatile bool g_connected = false;

bool wifiCheckConnection()
{
	wl_status_t status = (wl_status_t)WiFi.status();
	if (status != g_prevWirelessStatus) {
		g_prevWirelessStatus = status;

		// by default any state change means a failure
		bool success = false;

		switch (status) {
		case WL_NO_SHIELD:
			LOG_PRINTF("Wifi state changed to 'No Module'\n");
			break;

		case WL_IDLE_STATUS:
			LOG_PRINTF("Wifi state changed to 'Idle'\n");
			break;

		case WL_NO_SSID_AVAIL:
			LOG_PRINTF("Wifi state changed to 'No SSID available'\n");
			break;

		case WL_SCAN_COMPLETED:
			LOG_PRINTF("Wifi state changed to 'Scan completed'\n");
			break;

		case WL_CONNECTED:
			LOG_PRINTF("Wifi state changed to 'Connected'\n");
			success = true;
			break;

		case WL_CONNECT_FAILED:
			LOG_PRINTF("Wifi state changed to 'Connection failed'\n");
			break;

		case WL_CONNECTION_LOST:
			LOG_PRINTF("Wifi state changed to 'Connection lost'\n");
			break;

		case WL_DISCONNECTED:
			LOG_PRINTF("Wifi state changed to 'Disconnected'\n");
			break;

		default:
			LOG_PRINTF("Wifi state changed to 'Unknown'\n");
			break;
		}

		return success;
	}

	return true;
}

bool wifiReconnect()
{
	LOG_PRINTF("Connecting to %s\n", WIFI_SSID);
	g_connected = false;

	WiFi.mode(WIFI_MODE_STA);
	WiFi.setSleep(false);
	WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

	int pollTime = 500;	// ms
	int cnt = WIFI_TIMEOUT / pollTime;

	while (cnt--) {
		if (WiFi.status() == WL_CONNECTED) {
			LOG_PRINTF("WiFi connected\n");
			LOG_PRINTF("IP address: %s\n", WiFi.localIP().toString().c_str());
			g_connected = true;
			return true;
		}
		watchdogReset();
		delay(pollTime);
		Serial.print(".");
	}

	return false;
}

bool wifiIsConnected()
{
	return g_connected;
}

void wifiTask(void * parameter)
{
	//
	// We start by connecting to a WiFi network
	//

	if (!wifiReconnect()) {
		LOG_PRINTF("Failed to reconnect, restarting board!\n");
		ESP.restart();
	}

	while (1) {

		// update watchdog time
		watchdogReset();

		// periodic Wifi check
		if (!wifiCheckConnection()) {

			if (!wifiReconnect()) {
				LOG_PRINTF("Failed to reconnect, restarting board!\n");
				ESP.restart();
			}
		}

		delay(500);
	}

}
