#include <Arduino.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "../utils/utils.h"
#include "../config/config.h"
#include "ntpTask.h"
#include "../tasks/wifiTask.h"

static WiFiUDP ntpUDP;

// TODO: this does not take timezones into account! Only UTC for now.
static NTPClient timeClient(ntpUDP, NTP_SERVER, NTP_OFFSET_SECONDS, NTP_UPDATE_INTERVAL_MS);
static SemaphoreHandle_t g_mutex = xSemaphoreCreateMutex();
static unsigned long g_lastEpochTime = 0;
static uint32_t g_lastEpochMillis = 0;

void fetchTimeFromNTP(void * parameter)
{
	while (1) {

		// wait until the network is connected
		if (!wifiIsConnected()){
			delay(100);
			continue;
		}

		LOG_PRINTF("[NTP] Updating...\n");
		timeClient.update();

		// atomically update last epoch time and millis() reference
		if (xSemaphoreTake(g_mutex, (TickType_t)5) == pdTRUE) {
			g_lastEpochTime = timeClient.getEpochTime();
			g_lastEpochMillis = millis();
			xSemaphoreGive(g_mutex);
		}

		String timestring = timeClient.getFormattedTime();
		short tIndex = timestring.indexOf("T");
		LOG_PRINTF("NTP time: %s\n", timestring.substring(tIndex + 1, timestring.length() -3).c_str());

		// Sleep for a minute before checking again
		longDelay(NTP_UPDATE_INTERVAL_MS);
	}
}

uint64_t compensatedMillis()
{
	unsigned long lastEpochTime = 0;
	uint32_t lastEpochMillis;

	if (xSemaphoreTake(g_mutex, (TickType_t)5) == pdTRUE) {
		lastEpochTime = g_lastEpochTime;
		lastEpochMillis = g_lastEpochMillis;
		xSemaphoreGive(g_mutex);

		// conversion
		return (uint64_t)lastEpochTime * 1000 + (millis() - lastEpochMillis);
	} else {
		return 0;
	}
}
