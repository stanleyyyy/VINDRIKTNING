#pragma once

/**
 * Set this to false to disable Serial logging
 */
#define DEBUG false

/**
 * Set this to 1 if you have SensirionI2CScd4x CO2 sensor
 */
#define USE_CO2_SENSOR	1

/**
 * Timeout for the WiFi connection. When this is reached,
 * the ESP goes into deep sleep for 30seconds to try and
 * recover.
 */
#define WIFI_TIMEOUT 20000 // 20 seconds

/**
 * How long should we wait after a failed WiFi connection
 * before trying to set one up again.
 */
#define WIFI_RECOVER_TIME_MS 20000 // 20 seconds

/**
 * Syncing time with an NTP server
 */
#define NTP_TIME_SYNC_ENABLED true
#define NTP_SERVER "pool.ntp.org"
#define NTP_OFFSET_SECONDS 3600
#define NTP_UPDATE_INTERVAL_MS (5 * 60 * 1000)

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


//
// Preferences ID (limited to 15 characters)
//

#define PREFERENCES_ID "Vindriktning"

// Check which core Arduino is running on. This is done because updating the 
// display only works from the Arduino core.
#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif
