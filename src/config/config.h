#pragma once

/**
 * Set this to false to disable Serial logging
 */
#define DEBUG false

/**
 * Set this to 1 if you have SensirionI2CScd4x CO2 sensor
 */
#define USE_CO2_SENSOR	0

/**
 * Set this to 1 if you have M5Stack ENV III sensor
 */
#define USE_ENV_SENSOR	1

/**
 * Timeout for the WiFi connection. When this is reached,
 * the ESP goes into deep sleep for 30seconds to try and
 * recover.
 */
#define WIFI_TIMEOUT 10 // 10 seconds

/**
 * Syncing time with an NTP server
 */
#define NTP_TIME_SYNC_ENABLED true
#define NTP_SERVER "pool.ntp.org"
#define NTP_OFFSET_SECONDS 3600
#define NTP_UPDATE_INTERVAL_MS (5 * 60 * 1000)


/**
 * Wifi related settings
 */

#define ESPASYNC_WIFIMGR_DEBUG_PORT SERIAL
#define _ESPASYNC_WIFIMGR_LOGLEVEL_ 2		// Use from 0 to 4. Higher number, more debugging messages and memory usage.
#define DOUBLERESETDETECTOR_DEBUG true		// double reset detector enabled
#define DRD_TIMEOUT 10						// Number of seconds after reset during which a subseqent reset will be considered a double reset.
#define DRD_ADDRESS 0						// RTC Memory Address for the DoubleResetDetector to use
#define HEARTBEAT_INTERVAL 10000
#define MIN_AP_PASSWORD_SIZE 8
#define SSID_MAX_LEN 32
#define PASS_MAX_LEN 64
#define WIFICHECK_INTERVAL 1000L
#define NUM_WIFI_CREDENTIALS 1
#define CONFIG_FILENAME F("/wifi_cred.dat")
#define USING_CORS_FEATURE false
#define USE_DHCP_IP true
#define USE_CONFIGURABLE_DNS true
#define USE_CUSTOM_AP_IP false
#define ESP_DRD_USE_SPIFFS true
#define DEFAULT_HOST_NAME "ESP32-VINDRIKTNING"
#define HOST_NAME_LEN 40
#define HTTP_PORT 80

#if ESP32
	// For ESP32, this better be 0 to shorten the connect time.
	// For ESP32-S2/C3, must be > 500
	#if (USING_ESP32_S2 || USING_ESP32_C3)
		#define WIFI_MULTI_1ST_CONNECT_WAITING_MS 500L
	#else
		// For ESP32 core v1.0.6, must be >= 500
		#define WIFI_MULTI_1ST_CONNECT_WAITING_MS 800L
	#endif
#else
	// For ESP8266, this better be 2200 to enable connect the 1st time
	#define WIFI_MULTI_1ST_CONNECT_WAITING_MS 2200L
#endif

#define WIFI_MULTI_CONNECT_WAITING_MS 500L

//
// debug uart speed
//

#define HW_UART_SPEED 115200L

//
// 5 second timeout to reset the board
//

#define WATCHDOG_TIMEOUT 5000

//
// Periodic reset every 8 hours
//

#define PERIODIC_RESET_TIMEOUT (24 * 60 * 60 * 1000)

//
// Pins and definitions
//

#define PIN_FAN 12		// FAN gpio
#define PIN_LED 25		// LED gpio
#define RXD2	16		// RX pin for PMS sensor
#define TXD2	17		// TX pin for PMS sensor

#define SDA		21		// I2C SDA pin for CO2 sensor
#define SCL		22		// I2C SCL pin for CO2 sensor

#define BRIGHTNESS_MIN	10	// LED brightness (min) - range <0;100>
#define BRIGHTNESS		100	// LED brightness (default) - range <0;100>

// altitude in meters
#define ALTITUDE	134

// atmospheric pressure in hecto Pascals (hPA)
#define AIR_PRESSURE 1020

//
// LED ids
//

#define PM_LED		0
#define HUM_LED		1
#define CO2_LED 	2

// set to 1 to use adafruit neopixel library
#define USE_ADAFRUIT_NEOPIXEL 0

#if USE_ADAFRUIT_NEOPIXEL
#define NUM_LEDS 	3
#endif

// set to 1 to enable HTTP api to debug led colors
#define DEBUG_LEDS 0

//
// Preferences ID (limited to 15 characters)
//

#define PREFERENCES_ID "Vindriktning"

// Check which core Arduino is running on. This is done because updating the 
// display only works from the Arduino core.
#ifndef ARDUINO_RUNNING_CORE
#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif
#endif