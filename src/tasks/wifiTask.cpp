#include <Arduino.h>
#include "WiFi.h"
#include "config.h"
#include "secrets.h"
#include "utils.h"
#include "watchdog.h"

#include "wifiTask.h"
#include <SPIFFS.h>
#include <WiFiSettings.h>
#include <ESPmDNS.h>

static wl_status_t g_prevWirelessStatus = WL_NO_SHIELD;
static volatile bool g_connected = false;

// this variable can persist across reboots
RTC_DATA_ATTR uint32_t p_wifiReconnectRetry = 0;

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

bool wifiIsConnected()
{
	return g_connected;
}

void wifiWaitForConnection()
{
	while (!wifiIsConnected()) {
		delay(100);
	}
}

bool wifiReconfigure()
{
	LOG_PRINTF("Forcing WiFi AP mode\n");
	if (WiFiSettings.forceApMode(false)) {
		watchdogScheduleReboot();
		return true;
	}

	return false;
}

bool wifiReset()
{
	LOG_PRINTF("Clearing WiFi credentials\n");
	if (WiFiSettings.forceApMode(true)) {
		watchdogScheduleReboot();
		return true;
	}

	return false;
}

String wifiHostName()
{
	return WiFiSettings.hostname;
}

void wifiTask(void * parameter)
{
	// required by Wifisettings!
	SPIFFS.begin(true);

	// Set custom callback functions
	WiFiSettings.onSuccess = []() {
		LOG_PRINTF("Success!\n");
		watchdogEnable(true);
		watchdogReset();
	};
	WiFiSettings.onFailure = []() {
		LOG_PRINTF("Failed!\n");
		watchdogEnable(true);
		watchdogReset();
	};

	WiFiSettings.onConnect = []() {
		LOG_PRINTF("Connect started\n");
		watchdogReset();
	};

	WiFiSettings.onPortal = []() {
		LOG_PRINTF("Portal started\n");
		// disable watchdog as scan can take a lot of time
		watchdogEnable(false);
		watchdogReset();
	};

	WiFiSettings.onPortalView = []() {
		LOG_PRINTF("Portal view\n");
		watchdogReset();
	};

	WiFiSettings.onPortalWaitLoop = []() {
		// update watchdog time
		watchdogReset();
		// Delay next function call by 500ms
		return 500;
	};

	WiFiSettings.onWaitLoop = []() {
		// update watchdog time
		watchdogReset();
		// Delay next function call by 500ms
		return 500;
	};

	WiFiSettings.onConfigSaved = []() {
		LOG_PRINTF("Settings saved, rebooting");
		// we can now reboot
		watchdogScheduleReboot();
	};

	WiFiSettings.onRestart = []() {
		LOG_PRINTF("Restart requested");
		delay(50);
	};

	// if this is the first reconnect after cold reboot (p_wifiReconnectRetry == 0)
	// don't run portal
	WiFiSettings.connect(!p_wifiReconnectRetry ? false : true, WIFI_TIMEOUT);

	// did connection succeed?
	if (!wifiCheckConnection()) {
		if (!p_wifiReconnectRetry) {
			LOG_PRINTF("First Wifi reconnect failed, rebooting");
			p_wifiReconnectRetry++;

			// p_wifiReconnectRetry is stored in RTC memory, we can safely 'reboot' via deep sleep
			long timeMicros = 1000 * 1000L;
			esp_sleep_enable_timer_wakeup(timeMicros);
			esp_deep_sleep_start();
		}
	}

	// re-enable watchdog
	watchdogEnable(true);
	g_connected = wifiCheckConnection();

	while (1) {
		// update watchdog time
		watchdogReset();

		// periodic Wifi check
		if (!wifiCheckConnection()) {

			// try to connect again
			WiFiSettings.connect(true, WIFI_TIMEOUT);
			g_connected = wifiCheckConnection();

			// if the connection failed, reboot
			if (!g_connected) {
				LOG_PRINTF("Failed to reconnect, restarting board!\n");
				ESP.restart();
			}
		}

		delay(500);
	}
}
