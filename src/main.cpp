#include <Arduino.h>

#include "config/config.h"
#include "utils/utils.h"
#include "utils/SerialAndTelnetInit.h"
#include "utils/watchdog.h"
#include "utils/display.h"
#include "tasks/wifiTask.h"
#include "tasks/ntpTask.h"
#include "tasks/otaTask.h"
#include "tasks/sensorTask.h"
#include "tasks/serverTask.h"

void setup()
{
	//
	// make sure wifi is initialized before calling anything else
	//

	WiFi.mode(WIFI_OFF);
	WiFi.mode(WIFI_MODE_STA);
	WiFi.setSleep(false);

	// init serial/telnet
	SerialAndTelnetInit::init();

	// init watchdog
	watchdogInit();

	// initialize display
	Display::instance();

	//
	// start sensor task
	//

	xTaskCreatePinnedToCore(
		sensorTask,
		"sensorTask",	 // Task name
		8192,			 // Stack size (bytes)
		NULL,			 // Parameter
		2,				 // Task priority
		NULL,			 // Task handle
		ARDUINO_RUNNING_CORE);

	//
	// start OTA task
	//

	xTaskCreatePinnedToCore(
		otaTask,
		"otaTask",		 // Task name
		8192,			 // Stack size (bytes)
		NULL,			 // Parameter
		1,				 // Task priority
		NULL,			 // Task handle
		ARDUINO_RUNNING_CORE);

	//
	// Connect to WiFi & keep the connection alive.
	//

	xTaskCreatePinnedToCore(
		wifiTask,
		"wifiTask", // Task name
		8192,			 // Stack size (bytes)
		NULL,			 // Parameter
		3,				 // Task priority
		NULL,			 // Task handle
		ARDUINO_RUNNING_CORE);

	//
	// wait for connection
	//

	wifiWaitForConnection();

	//
	// now start the server task and NTP task
	//

	xTaskCreatePinnedToCore(
		serverTask,
		"serverTask",	 // Task name
		8192,			 // Stack size (bytes)
		NULL,			 // Parameter
		2,				 // Task priority
		NULL,			 // Task handle
		ARDUINO_RUNNING_CORE);

#if NTP_TIME_SYNC_ENABLED == true
	//
	// Update time from NTP server.
	//

	xTaskCreate(
		fetchTimeFromNTP,
		"Update NTP time",
		5000, // Stack size (bytes)
		NULL, // Parameter
		1,	  // Task priority
		NULL  // Task handle
	);
#endif

}

void loop()
{
	delay(500);
}