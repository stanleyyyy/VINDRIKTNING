#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>

#include "display.h"

#include "../config/config.h"
#include "../utils/utils.h"
#include "../utils/watchdog.h"
#include "../utils/hsvToRgb.h"

#define CLAMP(min, max, val) ((val < min) ? min : ((val > max) ? max : val))

//
// display task context
//

class DisplayImpl : public Display {
private:
	// preferences object
	Preferences m_preferences;

	// synchronization mutex
	SemaphoreHandle_t m_mutex;

	// rgb leds
	static Adafruit_NeoPixel m_rgbWS;
	uint32_t m_colors[NUM_LEDS] = {0};
	uint8_t m_brightness;

	bool m_booting = true;

	static void displayTask(void * parameter)
	{
		DisplayImpl *instance = (DisplayImpl *)parameter;
		if (instance) {
			instance->task();
		}
	}

	void bootAnimation()
	{
		while (m_booting) {
			for (int i = 0; i < NUM_LEDS; i++) {

				// fade in/out one led segment
				for (int j = 0; j <= 32; j++) {
					uint32_t rgb =  utils::HSVtoRGB(120, 100, j * BRIGHTNESS / 32);
					setColor(rgb, i);
					delay(10);

					if (!m_booting) {
						return;
					}
				}

				for (int j = 32; j >= 0; j--) {
					uint32_t rgb =  utils::HSVtoRGB(120, 100, j * BRIGHTNESS / 32);
					setColor(rgb, i);
					delay(10);

					if (!m_booting) {
						return;
					}
				}

				delay(100);
			}
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

	void bootFinished()
	{
		executeAtomically([=]{
			if (m_booting) {
				m_booting = false;

				// read brightness from preferences
				m_preferences.begin(PREFERENCES_ID, false);
				m_brightness = m_preferences.getUInt("brightness", BRIGHTNESS);
				m_preferences.end();

				//
				// fade leds to zero brightness
				//

				fadeColors(0, 0, 0, 16);
			}
		});
	}

	void task()
	{
		LOG_PRINTF("Starting Display task\n");

		//
		// initial animation (green)
		//

		bootAnimation();

		while (1) {
			longDelay(30000);
		}
	}

public:

	Adafruit_NeoPixel& rgbWs() { return m_rgbWS; }

	DisplayImpl()
	{
		LOG_PRINTF("Initializing display\n");

		// create semaphore for watchdog
		m_mutex = xSemaphoreCreateMutex();
		m_brightness = BRIGHTNESS;
		m_booting = true;

		// init/animate leds
		init();

		xTaskCreatePinnedToCore(
			&DisplayImpl::displayTask,
			"displayTask",	 // Task name
			8192,			 // Stack size (bytes)
			this,			 // Parameter
			2,				 // Task priority
			NULL,			 // Task handle
			ARDUINO_RUNNING_CORE);
	}

	bool init()
	{
		// WS2718 init
		m_rgbWS.begin();
		m_rgbWS.setBrightness(m_brightness);
		m_rgbWS.setPixelColor(PM_LED, 0);
		m_rgbWS.setPixelColor(HUM_LED, 0);
		m_rgbWS.setPixelColor(CO2_LED, 0);
		m_rgbWS.show();

		return true;
	}

	void setColor(uint32_t rgb, unsigned int id)
	{
		executeAtomically([=]{
			if (id < NUM_LEDS)
				m_colors[id] = rgb;

			m_rgbWS.setPixelColor(id, rgb);
			m_rgbWS.show();
		});
	}

	void setColors(uint32_t led1, uint32_t led2, uint32_t led3, int brightness = -1)
	{
		executeAtomically([=]{
			m_colors[PM_LED] = led1;
			m_colors[HUM_LED] = led2;
			m_colors[CO2_LED] = led3;

			if (brightness >= 0)
				m_rgbWS.setBrightness(brightness);

			m_rgbWS.setPixelColor(PM_LED, led1);
			m_rgbWS.setPixelColor(HUM_LED, led2);
			m_rgbWS.setPixelColor(CO2_LED, led3);

			m_rgbWS.show();
		});
	}

	uint32_t getColor(unsigned int id)
	{
		if (id < NUM_LEDS) {
			return m_colors[id];
		}

		return 0;
	}

	static uint32_t mixColors(uint32_t c1, uint32_t c2, float ratio)
	{
		if (ratio < 0) {
			ratio = 0;
		} else if (ratio > 1.0) {
			ratio = 1.0;
		}

		uint8_t r1 = (c1 & 0xFF0000) >> 16;
		uint8_t g1 = (c1 & 0x00FF00) >> 8;
		uint8_t b1 = (c1 & 0x0000FF) >> 0;

		uint8_t r2 = (c2 & 0xFF0000) >> 16;
		uint8_t g2 = (c2 & 0x00FF00) >> 8;
		uint8_t b2 = (c2 & 0x0000FF) >> 0;

		r1 = r1 + (r2 - r1) * ratio;
		g1 = g1 + (g2 - g1) * ratio;
		b1 = b1 + (b2 - b1) * ratio;

		r1 = CLAMP(0, 255, r1);
		g1 = CLAMP(0, 255, g1);
		b1 = CLAMP(0, 255, b1);

		return Adafruit_NeoPixel::Color(r1, g1, b1);
	}

	void setLedBrightness(const uint8_t &brightness)
	{	
		bootFinished();

		executeAtomically([=]{
			if (m_brightness != brightness) {
				int oldBrightness = m_brightness;
				m_brightness = brightness;
				m_preferences.begin(PREFERENCES_ID, false);
				m_preferences.putUInt("brightness", m_brightness);
				m_preferences.end();

				// interpolate brightness
				for (int i = 0; i <= 16; i++) {
					int brightness = oldBrightness + (m_brightness - oldBrightness) * i / 16;
					setColors(getColor(PM_LED), getColor(HUM_LED), getColor(CO2_LED), brightness);
					delay(10);
				}
			}
		});
	}

	void fadeColors(uint32_t pmColor, uint32_t humColor, uint32_t co2Color, const int &steps)
	{
		bootFinished();

		executeAtomically([=]{
			uint32_t prevPmColor;
			uint32_t prevHumColor;
			uint32_t prevCo2Color;

			prevPmColor = getColor(PM_LED);
			prevHumColor = getColor(HUM_LED);
			prevCo2Color = getColor(CO2_LED);

			for (int i = 0; i <= steps; i++) {
				uint32_t c1 = mixColors(prevPmColor, pmColor, i / (float)steps);
				uint32_t c2 = mixColors(prevHumColor, humColor, i / (float)steps);
				uint32_t c3 = mixColors(prevCo2Color, co2Color, i / (float)steps);

				setColors(c1, c2, c3, m_brightness);
				delay(10);
			}
		});
	}

	//
	// alert user that an error has occured
	// via RGB animation (blink red 10x)
	//

	void alert(int id)
	{
		bootFinished();

		executeAtomically([=]{
			int i = 0;
			m_rgbWS.setBrightness(BRIGHTNESS);
			while (1) {
				if (i > 10) {
					break;
				}

				// blink with red
				setColor(rgbColor(Display::eColorRed), id);
				delay(200);
				setColor(0, id);
				delay(200);
				i++;
			}
		});
	}

	virtual uint32_t rgbColor(const Colors &color)
	{
		switch (color) {
		case eColorRed:
			return Adafruit_NeoPixel::Color(255, 0, 0);
		case eColorGreen:
			return Adafruit_NeoPixel::Color(0, 255, 0);
		case eColorBlue:
			return Adafruit_NeoPixel::Color(0, 0, 255);
		default:
			return 0;
		}
	}

};

Display &Display::instance()
{
	static DisplayImpl instance;
	return instance;
}

Adafruit_NeoPixel DisplayImpl::m_rgbWS = Adafruit_NeoPixel(3, PIN_LED, NEO_GRB + NEO_KHZ800);
