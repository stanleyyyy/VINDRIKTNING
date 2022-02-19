#include <Arduino.h>
#include "watchdog.h"
#include "config.h"
#include "utils.h"
#include "display.h"

static SemaphoreHandle_t mutex = NULL;
static uint32_t periodicResetTs = 0;
static uint32_t watchdogResetTs = 0;
static bool watchdogReboot = false;
static bool watchdogEnabled = true;

void watchdogTask(void *pvParameters __attribute__((unused)))
{
	while (1) {

		uint32_t diffMs = 0;
		if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
			diffMs = millis() - watchdogResetTs;
			xSemaphoreGive(mutex);
		}

		if (watchdogReboot) {
			LOG_PRINTF("Reboot scheduled, resetting the board!\n");
			uint32_t red = Display::instance().rgbColor(Display::eColorRed);
			Display::instance().fadeColors(red, red, red, 16);
			delay(2000);
			ESP.restart();
		}

		if ((diffMs > WATCHDOG_TIMEOUT) && watchdogEnabled) {
			LOG_PRINTF("Watchddog timeout elapsed, resetting the board!\n");
			uint32_t red = Display::instance().rgbColor(Display::eColorRed);
			Display::instance().fadeColors(red, red, red, 16);
			delay(2000);
			ESP.restart();
		}

		uint32_t periodicResetDiff = millis() - periodicResetTs;

		if ((periodicResetDiff > PERIODIC_RESET_TIMEOUT) && watchdogEnabled) {
			LOG_PRINTF("Periodic reset!\n");
			uint32_t red = Display::instance().rgbColor(Display::eColorRed);
			Display::instance().fadeColors(red, red, red, 16);
			delay(2000);
			ESP.restart();
		}

		delay(2000);
	}
}

void watchdogInit()
{
	// create semaphore for watchdog
	mutex = xSemaphoreCreateMutex();
	periodicResetTs = millis();

	//
	// watchdog task
	//

	xTaskCreate(
		watchdogTask,
		"watchdogTask",
		2048, // Stack size (bytes)
		NULL, // Parameter
		0,	  // Task priority
		NULL  // Task handle
	);	
}

void watchdogReset()
{
	if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
		if (!watchdogReboot)
			watchdogResetTs = millis();
		xSemaphoreGive(mutex);
	}
}

bool watchdogEnable(const bool &enable)
{
	bool ret = false;
	if (xSemaphoreTake(mutex, portMAX_DELAY ) == pdTRUE) {
		ret = watchdogEnabled;
		watchdogEnabled = enable;

		// if re-enabled, reset it
		if (enable) {
			if (!watchdogReboot)
				watchdogResetTs = millis();
		}
		xSemaphoreGive(mutex);
	}

	return ret;
}

void watchdogOverride(std::function<void(void)> fn)
{
	if (fn) {
		bool wasEnabled = watchdogEnable(false);
		fn();
		watchdogEnable(wasEnabled);
	}
}

void watchdogScheduleReboot()
{
	if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
		watchdogReboot = true;
		xSemaphoreGive(mutex);
	}
}

//
// Watchdog task
//

uint32_t watchdogTimeToReset()
{
	return PERIODIC_RESET_TIMEOUT - (millis() - periodicResetTs);
}
