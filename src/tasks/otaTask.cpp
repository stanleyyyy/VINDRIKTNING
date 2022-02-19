#include <Arduino.h>
#include <ArduinoOTA.h>
#include "otaTask.h"

#include "utils.h"
#include "watchdog.h"
#include "wifiTask.h"

void otaTask(void * parameter)
{
	// wait until the network is connected
	wifiWaitForConnection();
	LOG_PRINTF("WiFi available, initializing OTA service\n");

	//
	// arduino ota
	//

	ArduinoOTA
		.onStart([]() {
			String type;
			if (ArduinoOTA.getCommand() == U_FLASH)
				type = "sketch";
			else // U_SPIFFS
				type = "filesystem";

			// NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
			LOG_PRINTF("Start updating %s\n", type.c_str());
		})
		.onEnd([](){
			LOG_PRINTF("\nEnd");
		})
		.onProgress([](unsigned int progress, unsigned int total) {
			static int lastProgressPercent = -1;
			int progressPercent = (progress / (total / 100));
			if (progressPercent != lastProgressPercent) {
				lastProgressPercent = progressPercent;
				LOG_PRINTF("Progress: %u%%\r", progressPercent);
			}
			watchdogReset();
		})
		.onError([](ota_error_t error) {
			LOG_PRINTF("Error[%u]: ", error);
			if (error == OTA_AUTH_ERROR) LOG_PRINTF("Auth Failed\n");
			else if (error == OTA_BEGIN_ERROR) LOG_PRINTF("Begin Failed\n");
			else if (error == OTA_CONNECT_ERROR) LOG_PRINTF("Connect Failed\n");
			else if (error == OTA_RECEIVE_ERROR) LOG_PRINTF("Receive Failed\n");
			else if (error == OTA_END_ERROR) LOG_PRINTF("End Failed\n");
		});

	ArduinoOTA.setHostname(wifiHostName().c_str());
	ArduinoOTA.setMdnsEnabled(true);
	ArduinoOTA.begin();

	while (1) {
		// handle OTA support
		ArduinoOTA.handle();
		delay(100);
	}
}
