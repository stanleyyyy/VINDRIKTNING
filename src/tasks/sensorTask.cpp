#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <SensirionI2CScd4x.h>
#include <Wire.h>
#include <Preferences.h>

#include "sensorTask.h"
#include "pm1006.h"

#include "config.h"
#include "utils.h"
#include "watchdog.h"
#include "hsvToRgb.h"
#include "display.h"

#if (USE_CO2_SENSOR == 1)
#include "scd4xHelper.h"
#elif (USE_ENV_SENSOR == 1)
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include "UNIT_ENV.h"
#endif

#define CLAMP(min, max, val) ((val < min) ? min : ((val > max) ? max : val))

//
// sensor task context
//

class Context {
private:

	// synchronization mutex
	SemaphoreHandle_t m_mutex;

	// particle sensor
	static PM1006 m_pm1006;

#if (USE_CO2_SENSOR == 1)
	Scd4xHelper m_scd4x;
	uint16_t m_co2;
#elif (USE_ENV_SENSOR == 1)
	// temp/humidity sensor
	SHT3X m_sht30;
	// pressure sensor
	QMP6988 m_qmp6988;
	float m_pressure;
#endif
#if (USE_CO2_SENSOR == 1) || (USE_ENV_SENSOR == 1)
	float m_temperature;
	float m_humidity;
#endif
	uint16_t m_pm2_5;
	bool m_fanEnabled;

public:
	Context()
	{
#if (USE_CO2_SENSOR == 1)
		m_co2 = 0;
#endif
#if (USE_ENV_SENSOR == 1)
		m_pressure = 0;
#endif

#if (USE_CO2_SENSOR == 1) || (USE_ENV_SENSOR == 1)
		m_temperature = 0;
		m_humidity = 0;
#endif
		m_pm2_5 = 0;
		m_fanEnabled = true;

		// create semaphore for watchdog
		m_mutex = xSemaphoreCreateMutex();
	}

	void init()
	{
#if (USE_CO2_SENSOR == 1)
		initScd4x();
#endif

#if (USE_ENV_SENSOR == 1)
		// initialize i2c
		Wire.begin(SDA, SCL);

		// init pressure sensor
		if (!m_qmp6988.init()) {
			LOG_PRINTF("QMP6988 sensor failed to initialize!\n");
		}
#endif

		pinMode(PIN_FAN, OUTPUT);

		// read fan mode
		Preferences preferences;
		preferences.begin(PREFERENCES_ID, false);
		m_fanEnabled = preferences.getUInt("fan", 1) ? true : false;
		preferences.end();

		LOG_PRINTF("Fan is %s\n", m_fanEnabled ? "enabled" : "disabled");
		digitalWrite(PIN_FAN, m_fanEnabled ? HIGH : LOW);
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

	void sensorFanMode(const bool &enabled)
	{
		executeAtomically([&]{
			m_fanEnabled = enabled;

			LOG_PRINTF("Fan is %s\n", m_fanEnabled ? "enabled" : "disabled");
			digitalWrite(PIN_FAN, m_fanEnabled ? HIGH : LOW);

			Preferences preferences;
			preferences.begin(PREFERENCES_ID, false);
			preferences.putUInt("fan", m_fanEnabled ? 1 : 0);
			preferences.end();
		});
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
#elif (USE_ENV_SENSOR == 1)
	bool readEnv(float &temperature, float &humidity, float &pressure)
	{
		// get pressure in kPa
		pressure = m_pressure = m_qmp6988.calcPressure() / 1000.0;

		if (m_sht30.get() == 0){
			temperature = m_temperature = m_sht30.cTemp;
			humidity = m_humidity = m_sht30.humidity;

			LOG_PRINTF("Pressure: %f kPa\n", pressure);
			LOG_PRINTF("Temperature: %f °C\n", temperature);
			LOG_PRINTF("Humidity: %f %%\n", humidity);
			return true;
		} else {
			LOG_PRINTF("Failed to read temperature/humidity data!\n");
			temperature = 0;
			humidity = 0;
			return false;
		}
	}

	bool lastSensorData(uint16_t &pm2_5, float &temperature, float &humidity, float &pressure)
	{
		bool ret = false;
		executeAtomically([&]{
			pm2_5 = m_pm2_5;
			temperature = m_temperature;
			humidity = m_humidity;
			pressure = m_pressure;
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

	// initialize UART to access the PM1006 sensor
	Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

	// init context
	g_ctx.init();

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
#elif (USE_ENV_SENSOR == 1)
		float temperature;
		float humidity;
		float pressure;
		g_ctx.readEnv(temperature, humidity, pressure);
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
#elif (USE_ENV_SENSOR == 1)
		//
		// show humidity readings on the led
		//

		LOG_PRINTF("Pressure: %f kPa, temperature = %f C, humidity = %f %%\n", pressure, temperature, humidity);

		//
		// convert humidity from <0;100> to HSV values
		//   0% -> 180 degrees HSV (aqua)
		//  50% -> 120 degrees HSV (green)
		// 100% ->   0 degrees HSV (red)
		//

		{
			float h;
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

void sensorFanMode(const bool &enabled)
{
	g_ctx.sensorFanMode(enabled);
}

#if (USE_CO2_SENSOR == 1)
bool lastSensorData(uint16_t &pm2_5, float &temperature, float &humidity, uint16_t &co2)
{
	return g_ctx.lastSensorData(pm2_5, temperature, humidity, co2);
}
#elif (USE_ENV_SENSOR == 1)
bool lastSensorData(uint16_t &pm2_5, float &temperature, float &humidity, float &pressure)
{
	return g_ctx.lastSensorData(pm2_5, temperature, humidity, pressure);
}
#else
bool lastSensorData(uint16_t &pm2_5)
{
	return g_ctx.lastSensorData(pm2_5);
}
#endif
