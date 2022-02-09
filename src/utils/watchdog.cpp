#include <Arduino.h>
#include "watchdog.h"
#include "../config/config.h"
#include "../utils/utils.h"
#include "../utils/display.h"

static SemaphoreHandle_t mutex = NULL;
static uint32_t periodicResetTs = 0;
static uint32_t watchdogResetTs = 0;
static bool watchdogReboot = false;

void watchdogTask(void *pvParameters __attribute__((unused)))
{
	while (1) {

		uint32_t diffMs = 0;
		if (xSemaphoreTake(mutex, (TickType_t)5) == pdTRUE) {
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

		if (diffMs > WATCHDOG_TIMEOUT){
			LOG_PRINTF("Watchddog timeout elapsed, resetting the board!\n");
			uint32_t red = Display::instance().rgbColor(Display::eColorRed);
			Display::instance().fadeColors(red, red, red, 16);
			delay(2000);
			ESP.restart();
		}

		uint32_t periodicResetDiff = millis() - periodicResetTs;

		if (periodicResetDiff > PERIODIC_RESET_TIMEOUT) {
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
	if (xSemaphoreTake(mutex, (TickType_t)5) == pdTRUE) {
		if (!watchdogReboot)
			watchdogResetTs = millis();
		xSemaphoreGive(mutex);
	}
}

void watchdogScheduleReboot()
{
	if (xSemaphoreTake(mutex, (TickType_t)5) == pdTRUE) {
		watchdogReboot = true;
		xSemaphoreGive(mutex);
	}
}

//
// Watchdog task
//

uint32_t timeToReset()
{
	return PERIODIC_RESET_TIMEOUT - (millis() - periodicResetTs);
}
