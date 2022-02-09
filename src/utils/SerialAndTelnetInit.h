#pragma once

#include <Arduino.h>
#include "../config/config.h"
#include "../utils/utils.h"
#include "../tasks/wifiTask.h"

class SerialAndTelnetInit {
private:
	static SemaphoreHandle_t m_mutex;

public:
	static void init()
	{
		// create semaphore for watchdog
		m_mutex = xSemaphoreCreateMutex();

		SerialAndTelnet.setWelcomeMsg((char *)"CarbonDioxide server by Embedded Softworks, s.r.o.\n\n");
		SerialAndTelnet.setCallbackOnConnect([]{
			SERIAL.println("Telnet connection established.");
		});
		SerialAndTelnet.setCallbackOnDisconnect([]{
			SERIAL.println("Telnet connection closed.");
		});

		SERIAL.begin(115200);
		SERIAL.flush();
		delay(50);

		// create task that will wait until Wifi is initialized and then handle all Telnet traffic
		xTaskCreate(
			[](void * parameter) {
				while (1) {
					// wait until the network is connected
					if (!wifiIsConnected()){
						delay(100);
						continue;
					}

					// handle telnet
					SerialAndTelnet.handle();
					delay(50);
				}
			},
			"telnetTask",
			4096, // Stack size (bytes)
			NULL, // Parameter
			3,	  // Task priority
			NULL  // Task handle
		);
	}

	static bool lock()
	{
		if (xSemaphoreTake(m_mutex, (TickType_t)50) == pdTRUE)
			return true;

		return false;
	}

	static void unlock()
	{
		xSemaphoreGive(m_mutex);
	}
};
