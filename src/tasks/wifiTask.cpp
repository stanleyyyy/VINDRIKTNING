#include <Arduino.h>
#include "config.h"
#include "secrets.h"
#include "utils.h"
#include "watchdog.h"

#include "wifiTask.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>

//
// Configuration
//

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

// this has to be included AFTER we specified configuration properties above!
#include <ESPAsync_WiFiManager.h>
#include <ESP_DoubleResetDetector.h>

typedef struct {
	char wifi_ssid[SSID_MAX_LEN];
	char wifi_pw[PASS_MAX_LEN];
} WiFi_Credentials;

typedef struct {
	WiFi_Credentials m_wifiCredentials[NUM_WIFI_CREDENTIALS];
	char m_hostName[HOST_NAME_LEN];
	bool m_forceAp;
	uint16_t m_checksum;
} WM_config;


class WifiContext {
public:
	// persistent wifi configuration data
	WM_config m_WM_config;
	WiFi_AP_IPConfig m_WM_AP_IPconfig;
	WiFi_STA_IPConfig m_WM_STA_IPconfig;
	WiFiMulti m_wifiMulti;

	DoubleResetDetector *m_drd;

	// SSID and PW for Config Portal
	String m_ssid;
	String m_password;

	// SSID and PW for your Router
	String m_routerSSID;
	String m_routerPassword;

	DNSServer m_dnsServer;

	// Indicates whether ESP has WiFi credentials saved from previous session, or double reset detected
	bool m_initialConfig;

	// Use DHCP
	IPAddress stationIP = IPAddress(0, 0, 0, 0);
	IPAddress gatewayIP = IPAddress(192, 168, 2, 1);
	IPAddress netMask = IPAddress(255, 255, 255, 0);

	IPAddress dns1IP = gatewayIP;
	IPAddress dns2IP = IPAddress(8, 8, 8, 8);

	IPAddress APStaticIP = IPAddress(192, 168, 100, 1);
	IPAddress APStaticGW = IPAddress(192, 168, 100, 1);
	IPAddress APStaticSN = IPAddress(255, 255, 255, 0);

	volatile bool m_shallReconfigure;
	volatile bool m_shallReset;
	volatile bool m_connected;

	WifiContext()
	{
		m_ssid = "ESP_" + String((uint32_t)ESP.getEfuseMac(), HEX);
		m_drd = NULL;
		m_initialConfig = false;
		m_shallReconfigure = false;
		m_shallReset = false;
		m_connected = false;
	}

	void initAPIPConfigStruct(WiFi_AP_IPConfig &in_m_WM_AP_IPconfig)
	{
		in_m_WM_AP_IPconfig._ap_static_ip = APStaticIP;
		in_m_WM_AP_IPconfig._ap_static_gw = APStaticGW;
		in_m_WM_AP_IPconfig._ap_static_sn = APStaticSN;
	}

	void initSTAIPConfigStruct(WiFi_STA_IPConfig &staticIpConfig)
	{
		staticIpConfig._sta_static_ip = stationIP;
		staticIpConfig._sta_static_gw = gatewayIP;
		staticIpConfig._sta_static_sn = netMask;
		staticIpConfig._sta_static_dns1 = dns1IP;
		staticIpConfig._sta_static_dns2 = dns2IP;
	}

	void displayIPConfigStruct(WiFi_STA_IPConfig staticIpConfig)
	{
		LOG_PRINTF("stationIP = %s, gatewayIP = %s\n", staticIpConfig._sta_static_ip.toString().c_str(), staticIpConfig._sta_static_gw.toString().c_str());
		LOG_PRINTF("netMask = %s\n", staticIpConfig._sta_static_sn.toString().c_str());
		LOG_PRINTF("dns1IP = %s, dns2IP = %s\n", staticIpConfig._sta_static_dns1.toString().c_str(), staticIpConfig._sta_static_dns2.toString().c_str());
	}

	void configWiFi(const WiFi_STA_IPConfig &staticIpConfig)
	{
		// Set static IP, Gateway, Subnetmask, DNS1 and DNS2. New in v1.0.5
		WiFi.config(staticIpConfig._sta_static_ip, staticIpConfig._sta_static_gw, staticIpConfig._sta_static_sn, staticIpConfig._sta_static_dns1, staticIpConfig._sta_static_dns2);
	}

	uint8_t connectMultiWiFi()
	{
		uint8_t status;
		LOGERROR(F("ConnectMultiWiFi with :"));

		if ((m_routerSSID != "") && (m_routerPassword != "")) {
			LOGERROR3(F("* Flash-stored m_routerSSID = "), m_routerSSID, F(", m_routerPassword = "), m_routerPassword);
			LOGERROR3(F("* Add SSID = "), m_routerSSID, F(", PW = "), m_routerPassword);
			m_wifiMulti.addAP(m_routerSSID.c_str(), m_routerPassword.c_str());
		}

		for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
			// Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
			if ((String(m_WM_config.m_wifiCredentials[i].wifi_ssid) != "") && (strlen(m_WM_config.m_wifiCredentials[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE)) {
				LOGERROR3(F("* Additional SSID = "), m_WM_config.m_wifiCredentials[i].wifi_ssid, F(", PW = "), m_WM_config.m_wifiCredentials[i].wifi_pw);
			}
		}

		LOG_PRINTF("Connecting MultiWifi...\n");

		// set static ip if available
		configWiFi(m_WM_STA_IPconfig);

		watchdogEnable(false);
		status = m_wifiMulti.run();
		watchdogEnable(true);
		delay(WIFI_MULTI_1ST_CONNECT_WAITING_MS);

		int i = 0;
		while ((i++ < 20) && (status != WL_CONNECTED)) {
			status = WiFi.status();
			watchdogReset();

			if (status == WL_CONNECTED)
				break;
			else
				delay(WIFI_MULTI_CONNECT_WAITING_MS);
		}

		if (status == WL_CONNECTED) {
			LOG_PRINTF("WiFi connected after time: %d\n", i);
			LOG_PRINTF("SSID: %s, RSSI = %d\n", WiFi.SSID().c_str(), WiFi.RSSI());
			LOG_PRINTF("Channel: %d, IP address: %s\n", WiFi.channel(), WiFi.localIP().toString().c_str());
			m_connected = true;
		} else {
			LOG_PRINTF("WiFi not connected, rebooting...\n");
			// To avoid unnecessary DRD
			m_drd->loop();
			ESP.restart();
		}

		return status;
	}

	// format bytes
	String formatBytes(size_t bytes)
	{
		if (bytes < 1024)
		{
			return String(bytes) + "B";
		}
		else if (bytes < (1024 * 1024))
		{
			return String(bytes / 1024.0) + "KB";
		}
		else if (bytes < (1024 * 1024 * 1024))
		{
			return String(bytes / 1024.0 / 1024.0) + "MB";
		}
		else
		{
			return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
		}
	}

	void wifiReconnectIfNeeded()
	{
		if ((WiFi.status() != WL_CONNECTED)) {
			m_connected = false;
			LOG_PRINTF("\nWiFi lost. Call connectMultiWiFi in loop\n");
			connectMultiWiFi();
		}
	}

	void wifiCheckStatus()
	{
		static ulong checkwifi_timeout = 0;
		static ulong current_millis;

		current_millis = millis();

		// Check WiFi every WIFICHECK_INTERVAL (1) seconds.
		if ((current_millis > checkwifi_timeout) || (checkwifi_timeout == 0)) {
			wifiReconnectIfNeeded();
			checkwifi_timeout = current_millis + WIFICHECK_INTERVAL;
		}
	}

	int calcChecksum(uint8_t *address, uint16_t sizeToCalc)
	{
		uint16_t checkSum = 0;
		for (uint16_t index = 0; index < sizeToCalc; index++) {
			checkSum += *(((byte *)address) + index);
		}
		return checkSum;
	}

	bool wifiLoadConfiguration()
	{
		File file = SPIFFS.open(CONFIG_FILENAME, "r");
		LOG_PRINTF("Loading config...\n");

		memset((void *)&m_WM_config, 0, sizeof(m_WM_config));

		// New in v1.4.0
		memset((void *)&m_WM_STA_IPconfig, 0, sizeof(m_WM_STA_IPconfig));

		if (file) {
			file.readBytes((char *)&m_WM_config, sizeof(m_WM_config));

			// New in v1.4.0
			file.readBytes((char *)&m_WM_STA_IPconfig, sizeof(m_WM_STA_IPconfig));

			file.close();
			LOG_PRINTF("OK\n");

			if (m_WM_config.m_checksum != calcChecksum((uint8_t *)&m_WM_config, sizeof(m_WM_config) - sizeof(m_WM_config.m_checksum))) {
				LOG_PRINTF("Config checksum failed!\n");
				return false;
			}

			// New in v1.4.0
			displayIPConfigStruct(m_WM_STA_IPconfig);
			return true;
		} else {
			LOG_PRINTF("Failed\n");
			return false;
		}
	}

	void wifiEraseConfiguration()
	{
		LOG_PRINTF("Erasing config...\n");
		memset((void *)&m_WM_config, 0, sizeof(m_WM_config));
		memset((void *)&m_WM_STA_IPconfig, 0, sizeof(m_WM_STA_IPconfig));
		wifiSaveConfiguration();
	}

	void wifiSaveConfiguration()
	{
		File file = SPIFFS.open(CONFIG_FILENAME, "w");
		LOG_PRINTF("Saving config...\n");

		if (file) {
			m_WM_config.m_checksum = calcChecksum((uint8_t *)&m_WM_config, sizeof(m_WM_config) - sizeof(m_WM_config.m_checksum));
			file.write((uint8_t *)&m_WM_config, sizeof(m_WM_config));

			displayIPConfigStruct(m_WM_STA_IPconfig);

			file.write((uint8_t *)&m_WM_STA_IPconfig, sizeof(m_WM_STA_IPconfig));
			file.close();
			LOG_PRINTF("Suceeded\n");
		} else {
			LOG_PRINTF("Failed\n");
		}
	}

	void wifiSetup()
	{
		LOG_PRINTF("\nStarting Wifi Manager using SPIFFS on %s %s %s\n", ARDUINO_BOARD, ESP_ASYNC_WIFIMANAGER_VERSION, ESP_DOUBLE_RESET_DETECTOR_VERSION);

		if (!SPIFFS.begin(true)) {
			LOG_PRINTF("SPIFFS/LittleFS failed! Already tried formatting.\n");

			if (!SPIFFS.begin()) {
				// prevents debug info from the library to hide err message.
				delay(100);
				LOG_PRINTF("SPIFFS failed!. Please use LittleFS or EEPROM. Stay forever\n");

				while (true) {
					delay(1);
				}
			}
		}

		File root = SPIFFS.open("/");
		File file = root.openNextFile();

		while (file) {
			String fileName = file.name();
			size_t fileSize = file.size();
			LOG_PRINTF("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
			file = root.openNextFile();
		}

		LOG_PRINTF("\n");

		m_drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);

		if (!m_drd)
			LOG_PRINTF("Can't instantiate. Disable DRD feature\n");

		initAPIPConfigStruct(m_WM_AP_IPconfig);
		initSTAIPConfigStruct(m_WM_STA_IPconfig);

		// start manager now
		wifiStartManager();
	}

	void wifiStartManager(bool force = false)
	{
		AsyncWebServer m_server(HTTP_PORT);
		m_connected = false;

		bool configDataLoaded = false;
		if (wifiLoadConfiguration()) {
			configDataLoaded = true;
			LOG_PRINTF("Got stored Credentials. Timeout 120s for Config Portal\n");
		} else {
			// Enter CP only if no stored SSID on flash and file
			LOG_PRINTF("Open Config Portal without Timeout: No stored Credentials.\n");
			m_initialConfig = true;
		}

		// set default host name
		if (!m_WM_config.m_hostName[0]) {
			strcpy(m_WM_config.m_hostName, DEFAULT_HOST_NAME);
		}

		LOG_PRINTF("Using host name %s\n", m_WM_config.m_hostName);

		// Local intialization. Once its business is done, there is no need to keep it around
		//  Use this to default DHCP hostname to ESP8266-XXXXXX or ESP32-XXXXXX
		// ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &m_dnsServer);
		//  Use this to personalize DHCP hostname (RFC952 conformed)0
		#if (USING_ESP32_S2 || USING_ESP32_C3)
		ESPAsync_WiFiManager ESPAsync_wifiManager(&server, NULL, m_WM_config.m_hostName);
		#else
		DNSServer m_dnsServer;
		ESPAsync_WiFiManager ESPAsync_wifiManager(&m_server, &m_dnsServer, m_WM_config.m_hostName);
		#endif

		if (configDataLoaded) {
			ESPAsync_wifiManager.setConfigPortalTimeout(120); // If no access point name has been previously entered disable timeout.
		}

		#if USE_CUSTOM_AP_IP
		// set custom ip for portal
		//  New in v1.4.0
		ESPAsync_wifiManager.setAPStaticIPConfig(m_WM_AP_IPconfig);
		//////
		#endif

		ESPAsync_wifiManager.setMinimumSignalQuality(-1);			// no minimum signal quality
		ESPAsync_wifiManager.setConfigPortalChannel(0);				// Set config portal channel, default = 1. Use 0 => random channel from 1-13

		#if USING_CORS_FEATURE
		ESPAsync_wifiManager.setCORSHeader("Your Access-Control-Allow-Origin");
		#endif

		// We can't use WiFi.SSID() in ESP32as it's only valid after connected.
		// SSID and Password stored in ESP32 wifi_ap_record_t and wifi_config_t are also cleared in reboot
		// Have to create a new function to store in EEPROM/SPIFFS for this purpose
		m_routerSSID = ESPAsync_wifiManager.WiFi_SSID();
		m_routerPassword = ESPAsync_wifiManager.WiFi_Pass();

		// Remove this line if you do not want to see WiFi password printed
		// LOG_PRINTF("ESP Self-Stored: SSID = %s, pass = %s\n", m_routerSSID.c_str(), m_routerPassword.c_str());

		// SSID to uppercase
		m_ssid.toUpperCase();
		m_password = "";

		if (m_routerSSID != "") {
			LOGERROR3(F("* Add SSID = "), m_routerSSID, F(", PW = "), m_routerPassword);
			m_wifiMulti.addAP(m_routerSSID.c_str(), m_routerPassword.length() ? m_routerPassword.c_str() : nullptr);

			ESPAsync_wifiManager.setConfigPortalTimeout(120); // If no access point name has been previously entered disable timeout.
			LOG_PRINTF("Got ESP Self-Stored Credentials. Timeout 120s for Config Portal\n");
		}

		if (force || m_WM_config.m_forceAp) {
			LOG_PRINTF("AP forced\n");
			m_initialConfig = true;
		}

		if (m_drd->detectDoubleReset()) {
			// DRD, disable timeout.
			ESPAsync_wifiManager.setConfigPortalTimeout(0);
			LOG_PRINTF("Open Config Portal without Timeout: Double Reset Detected\n");
			m_initialConfig = true;
		}

		ESPAsync_WMParameter customHostName("hostName",  "host name", m_WM_config.m_hostName,  HOST_NAME_LEN);
		ESPAsync_wifiManager.addParameter(&customHostName);

		if (m_initialConfig) {
			#if USE_CUSTOM_AP_IP
			LOG_PRINTF("Starting configuration portal @%s\n", APStaticIP);
			#else
			LOG_PRINTF("Starting configuration portal @%s\n", "192.168.4.1");
			#endif

			// configure our stored static ip config if available
			ESPAsync_wifiManager.setSTAStaticIPConfig(m_WM_STA_IPconfig);
			LOG_PRINTF("SSID = %s, PWD = %s\n", m_ssid.c_str(), m_password.length() ? m_password.c_str() : "<none>");

			// Starts an access point
			if (!ESPAsync_wifiManager.startConfigPortal((const char *)m_ssid.c_str(), m_password.c_str())) {
				LOG_PRINTF("Not connected to WiFi but continuing anyway.\n");
			} else {
				LOG_PRINTF("WiFi connected...yeey :)\n");
			}

			// Stored	for later usage, from v1.1.0, but clear first
			memset(&m_WM_config, 0, sizeof(m_WM_config));

			for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
			{
				String tempSSID = ESPAsync_wifiManager.getSSID(i);
				String tempPW = ESPAsync_wifiManager.getPW(i);

				if (strlen(tempSSID.c_str()) < sizeof(m_WM_config.m_wifiCredentials[i].wifi_ssid) - 1)
					strcpy(m_WM_config.m_wifiCredentials[i].wifi_ssid, tempSSID.c_str());
				else
					strncpy(m_WM_config.m_wifiCredentials[i].wifi_ssid, tempSSID.c_str(), sizeof(m_WM_config.m_wifiCredentials[i].wifi_ssid) - 1);

				if (strlen(tempPW.c_str()) < sizeof(m_WM_config.m_wifiCredentials[i].wifi_pw) - 1)
					strcpy(m_WM_config.m_wifiCredentials[i].wifi_pw, tempPW.c_str());
				else
					strncpy(m_WM_config.m_wifiCredentials[i].wifi_pw, tempPW.c_str(), sizeof(m_WM_config.m_wifiCredentials[i].wifi_pw) - 1);

				// Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
				if ((String(m_WM_config.m_wifiCredentials[i].wifi_ssid) != "") && (strlen(m_WM_config.m_wifiCredentials[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE))
				{
					LOGERROR3(F("* Add SSID = "), m_WM_config.m_wifiCredentials[i].wifi_ssid, F(", PW = "), m_WM_config.m_wifiCredentials[i].wifi_pw);
					m_wifiMulti.addAP(m_WM_config.m_wifiCredentials[i].wifi_ssid, m_WM_config.m_wifiCredentials[i].wifi_pw);
				}
			}

			ESPAsync_wifiManager.getSTAStaticIPConfig(m_WM_STA_IPconfig);

			// clear force flag
			m_WM_config.m_forceAp = false;

			// read updated parameters
			strcpy(m_WM_config.m_hostName, customHostName.getValue());

			wifiSaveConfiguration();
		}

		unsigned long startedAt = millis();

		if (!m_initialConfig) {
			// Load stored data, the addAP ready for MultiWiFi reconnection
			if (!configDataLoaded)
				wifiLoadConfiguration();

			for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
				// Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
				if ((String(m_WM_config.m_wifiCredentials[i].wifi_ssid) != "") && (strlen(m_WM_config.m_wifiCredentials[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE)) {
					LOGERROR3(F("* Add SSID = "), m_WM_config.m_wifiCredentials[i].wifi_ssid, F(", PW = "), m_WM_config.m_wifiCredentials[i].wifi_pw);
					m_wifiMulti.addAP(m_WM_config.m_wifiCredentials[i].wifi_ssid, m_WM_config.m_wifiCredentials[i].wifi_pw);
				}
			}

			if (WiFi.status() != WL_CONNECTED) {
				LOG_PRINTF("ConnectMultiWiFi in setup\n");
				connectMultiWiFi();
			}
		}

		LOG_PRINTF("After waiting %f secs more in setup(), connection result is \n", (float)(millis() - startedAt) / 1000);

		if (WiFi.status() == WL_CONNECTED) {
			LOG_PRINTF("connected. Local IP: %s\n", WiFi.localIP().toString().c_str());
		}
		else {
			LOG_PRINTF(ESPAsync_wifiManager.getStatus(WiFi.status()));
		}

		// we can stop double reset detector
		if (m_drd) {
			m_drd->stop();
		}

		// from now on we are connected
		m_connected = true;
	}

	bool connected()
	{
		return m_connected;
	}

	// initiate reconfiguration and wait until it is finished
	bool reconfigure()
	{
		m_shallReconfigure = true;

		// wait until reconfiguration is finished
		while (m_shallReconfigure) {
			delay(100);
		}
		return true;
	}

	bool reset()
	{
		m_shallReset = true;

		// wait until reset is finished
		while (m_shallReset) {
			delay(100);
		}
		return true;
	}

	void loop()
	{
		while (1) {

			watchdogReset();

			//
			// if wifi reconfiguration was requested, execute it on this thread
			//

			if (m_shallReconfigure) {
				LOG_PRINTF("Reconfiguration initiated!\n");

				// force ap in settings
				m_WM_config.m_forceAp = true;
				wifiSaveConfiguration();

				// disconnect from wifi
				WiFi.disconnect();

				// and reboot the board
				watchdogScheduleReboot();

				// wait until it reboots
				while (1) {
					delay(100);
				}
			}

			if (m_shallReset) {
				// erase settings
				wifiEraseConfiguration();
				// disconnect from wifi and erase credentials
				WiFi.disconnect(false, true);

				// and reboot the board
				watchdogScheduleReboot();

				// wait until it reboots
				while (1) {
					delay(100);
				}
			}

			// Call the double reset detector loop method every so often,
			// so that it can recognise when the timeout expires.
			// You can also call drd.stop() when you wish to no longer
			// consider the next reset as a double reset.
			if (m_drd) {
				m_drd->loop();
			}

			wifiCheckStatus();
			delay(100);
		}
	}
};

static WifiContext g_wifiCtx;

void wifiTask(void *pvParameters __attribute__((unused)))
{
	// init
	watchdogEnable(false);
	g_wifiCtx.wifiSetup();
	watchdogEnable(true);

	// infinite loop
	g_wifiCtx.loop();
}

bool wifiReconfigure()
{
	return g_wifiCtx.reconfigure();
}

bool wifiReset()
{
	return g_wifiCtx.reset();;
}

bool wifiIsConnected()
{
	return g_wifiCtx.connected();
}

void wifiWaitForConnection()
{
	while (!wifiIsConnected()) {
		delay(500);
	}
}

String wifiHostName()
{
	const char *hostName = WiFi.getHostname();
	if (hostName) {
		LOG_PRINTF("Retrieved WiFi hostname: %s\n", hostName);
		return hostName;
	}
	LOG_PRINTF("Unable to retrieve WiFi hostname!\n");
	return "";
}
