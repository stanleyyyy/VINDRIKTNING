#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <SensirionI2CScd4x.h>
#include <Wire.h>
#include <Preferences.h>

#include "sensorTask.h"
#include "../3rd_party/pm1006/pm1006.h"

#include "../config/config.h"
#include "../utils/utils.h"
#include "../utils/watchdog.h"
#include "../utils/hsvToRgb.h"
#include "../utils/display.h"

#if (USE_CO2_SENSOR == 1)
#include "../utils/scd4xHelper.h"
#endif

#define CLAMP(min, max, val) ((val < min) ? min : ((val > max) ? max : val))

//
// sensor task context
//

class Context {
private:
	// preferences object
	Preferences m_preferences;

	// synchronization mutex
	SemaphoreHandle_t m_mutex;

	// particle sensor
	static PM1006 m_pm1006;

#if (USE_CO2_SENSOR == 1)
	Scd4xHelper m_scd4x;
	float m_temperature;
	float m_humidity;
	uint16_t m_co2;
#endif
	uint16_t m_pm2_5;

public:
	Context()
#if (USE_CO2_SENSOR == 1)
	: m_temperature(0)
	, m_humidity(0)
	, m_co2(0)
	, m_pm2_5(0)
#else
	: m_pm2_5(0)
#endif
	{
		// create semaphore for watchdog
		m_mutex = xSemaphoreCreateMutex();
	}

	PM1006& pm1006() { return m_pm1006; }

#if (USE_CO2_SENSOR == 1)
	bool initScd4x()
	{
		return m_scd4x.init(SDA, SCL);
	}

	Scd4xHelper& scd4x() { return m_scd4x; }
#endif

	bool readPm25(uint16_t &pm2_5)
	{
		uint16_t value;
		if (m_pm1006.read_pm25(&m_pm2_5)) {
			LOG_PRINTF("PM2.5 concentration: %u µg/m³\n", m_pm2_5);
			pm2_5 = m_pm2_5;
			return true;
		} else {
			LOG_PRINTF("Measurement failed!\n");
			Display::instance().alert(PM_LED);
			pm2_5 = 0;
			return false;
		}
	}

	void executeAtomically(std::function<void(void)> fn, int time = portMAX_DELAY)
	{
		if (xSemaphoreTakeRecursive(m_mutex, time) == pdTRUE) {
			if (fn)
				fn();
			xSemaphoreGiveRecursive(m_mutex);
		}
	}

#if (USE_CO2_SENSOR == 1)
	bool readCo2(float &temperature, float &humidity, uint16_t &co2)
	{
		if (m_scd4x.getSensorData(m_temperature, m_humidity, m_co2)) {
			temperature = m_temperature;
			humidity = m_humidity;
			co2 = m_co2;
			return true;
		} else {
			Display::instance().alert(CO2_LED);
			temperature = 0;
			humidity = 0;
			co2 = 0;
			return false;
		}
	}

	bool lastSensorData(uint16_t &pm2_5, float &temperature, float &humidity, uint16_t &co2)
	{
		bool ret = false;
		executeAtomically([&]{
			pm2_5 = m_pm2_5;
			temperature = m_temperature;
			humidity = m_humidity;
			co2 = m_co2;
			ret = true;
		});
		return ret;
	}
#else
	bool lastSensorData(uint16_t &pm2_5)
	{
		bool ret = false;
		executeAtomically([&]{
			pm2_5 = m_pm2_5;
			ret = true;
		});
		return ret;
	}
#endif
} g_ctx;

PM1006 Context::m_pm1006(&Serial2);

//
// sensor task implementation
//

void sensorTask(void * parameter)
{
	LOG_PRINTF("Starting Sensor task\n");

	pinMode(PIN_FAN, OUTPUT);

	// initialize UART to access the PM1006 sensor
	Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

#if (USE_CO2_SENSOR == 1)
	g_ctx.initScd4x();
#endif

	digitalWrite(PIN_FAN, HIGH);
	LOG_PRINTF("Fan ON\n");

	// give 10 seconds for initial measurement
	delay(10000);

	while (1) {

		//
		// read data from sensors
		//

		// pm2.5 sensor
		uint16_t pm2_5 = 0;
		while (!g_ctx.readPm25(pm2_5)) {
			delay(1000);
		}

#if (USE_CO2_SENSOR == 1)
		// co2 sensor
		uint16_t co2;
		float temperature;
		float humidity;

		g_ctx.readCo2(temperature, humidity, co2);
#endif

		//
		// show PM readings on the led
		//

		uint32_t pmColor = 0;
		uint32_t co2Color = 0;
		uint32_t humColor = 0;

		//
		// convert pm2_5 value to HSV values
		//  < 30 -> 120 degrees HSV (green)
		// <30; 90> -> 120 degrees to 0 degrees (green to red)
		// > 90 -> 0 degrees (red);
		//

		{
			float h;

			if (pm2_5 < 30) {
				h = 120;
			} else if ((pm2_5 >= 30) && (pm2_5 < 90)) {
				// scale pm2_5 from <30;90> to <1;100>
				float scaled = (pm2_5 - 30.0) / (90.0 - 30.0);
				h = 120 * (1.0 - scaled);
			} else {
				h = 0;
			}

			pmColor = utils::HSVtoRGB(h, 100, BRIGHTNESS);
		}

#if (USE_CO2_SENSOR == 1)
		//
		// show CO2 + humidity readings on the led
		//

		if (co2 == 0) {
			LOG_PRINTF("Invalid sample detected, skipping.\n");
		} else {
			LOG_PRINTF("Co2: %d ppm, temperature = %f C, humidity = %f %%\n", co2, temperature, humidity);

			//
			// convert CO2 levels from <400; 4000> to HSV values
			//  <  400		-> 120 degrees HSV (green)
			//  <400;2000>	-> 0 degrees HSV (red)
			//  > 2000		-> 0 degrees HSV (red)

			float h;
			if (co2 < 400) {
				h = 120;
			} else if (co2 >= 400 && co2 <= 2000) {
				h = 120 * (1.0 - (co2 - 400.0) / (2000.0 - 400.0));
			} else {
				h = 0;
			}

			co2Color = utils::HSVtoRGB(h, 100, BRIGHTNESS);

			//
			// convert humidity from <0;100> to HSV values
			//   0% -> 180 degrees HSV (aqua)
			//  50% -> 120 degrees HSV (green)
			// 100% ->   0 degrees HSV (red)
			//

			if (humidity < 50) {
				h = 180.0 - (180.0 - 120.0) * humidity / 50.0;
			} else {
				h = 120.0 * (1.0 - (humidity - 50.0) / 50.0);
			}

			humColor = utils::HSVtoRGB(h, 100, BRIGHTNESS);
		}
#endif

		//
		// fade new color in 16 steps
		//

		Display::instance().fadeColors(pmColor, humColor, co2Color, 16);

		//
		// wait 10 seconds and read data again
		//

		longDelay(10000);
	}
}


#if (USE_CO2_SENSOR == 1)
bool lastSensorData(uint16_t &pm2_5, float &temperature, float &humidity, uint16_t &co2)
{
	return g_ctx.lastSensorData(pm2_5, temperature, humidity, co2);
}
#else
bool lastSensorData(uint16_t &pm2_5)
{
	return g_ctx.lastSensorData(pm2_5);
}
#endif
