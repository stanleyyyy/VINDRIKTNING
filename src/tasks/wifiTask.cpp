#include <Arduino.h>
#include "config.h"
#include "utils.h"
#include "watchdog.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include "wifiTask.h"
#include "display.h"
#include "hsvToRgb.h"
#include "driver/adc.h"
#include "WiFiMultiSSID.h"

#include <ESPAsync_WiFiManager.h>
#include <ESP_DoubleResetDetector.h>

#if PRINT_PASSWORDS
#define PASSWORD_STR(str) (str && str[0]) ? str : "<empty>"
#else
#define PASSWORD_STR(str) (str && str[0]) ? "********" : "<empty>"
#endif

#define MAX(a, b) ((a) > (b)) ? (a) : (b)

typedef struct {
	// stored wifi credentials
	WiFiMultiSSID::Credentials m_credentials[NUM_WIFI_CREDENTIALS];
	// configured host name
	char m_hostName[HOST_NAME_LEN];
	// force access point mode flag
	bool m_forceAp;
	// structure checksum
	uint16_t m_checksum;
} WiFiManagerConfig;

class WiFiContext {
public:
	// persistent wifi configuration data
	WiFiManagerConfig m_managerConfig;

	AsyncWebServer m_httpServer;

	// client mode IP configuration
	WiFi_STA_IPConfig m_clientConfig;

	// last connection params
	WiFiMultiSSID::LastParams m_lastWiFiParams;

	// multi SSID wifi connection helper
	WiFiMultiSSID m_wifiMulti;

	// double reset detector
	DoubleResetDetector *m_drd;

	// SSID and PW for Config Portal
	String m_ssid;
	String m_password;

	#if (!USING_ESP32_S2 && !USING_ESP32_C3)
	DNSServer m_dnsServer;
	#endif

	#if USE_CUSTOM_AP_IP
	// ap ip config
	IPAddress m_apIpAddress;
	IPAddress m_apGateway;
	IPAddress m_apMask;
	#endif

	wifi_event_id_t m_wifiEventId;

	volatile bool m_shallReconfigure;
	volatile bool m_shallReset;
	volatile bool m_connected;

	static WiFiContext &instance()
	{
		static WiFiContext *ctx = nullptr;
		if (!ctx) {
			ctx = new WiFiContext();
		}
		return *ctx;
	}

	WiFiContext()
		: m_httpServer(HTTP_PORT)
	{
		m_ssid = "ESP_" + String((uint32_t)ESP.getEfuseMac(), HEX);
		m_drd = NULL;
		m_shallReconfigure = false;
		m_shallReset = false;
		m_connected = false;
		m_wifiEventId = 0;

		// default client mode config
		m_clientConfig._sta_static_ip = IPAddress(0, 0, 0, 0);
		m_clientConfig._sta_static_gw = IPAddress(192, 168, 2, 1);
		m_clientConfig._sta_static_sn = IPAddress(255, 255, 255, 0);
		m_clientConfig._sta_static_dns1 = m_clientConfig._sta_static_gw;
		m_clientConfig._sta_static_dns2 = IPAddress(8, 8, 8, 8);

		#if USE_CUSTOM_AP_IP
		// default ap mode config
		m_apIpAddress = IPAddress(192, 168, 100, 1);
		m_apGateway = IPAddress(192, 168, 100, 1);
		m_apMask = IPAddress(255, 255, 255, 0);
		#endif
	}

	void displayClientConfig()
	{
		LOG_PRINTF("Client IP configuration:\n");
		LOG_PRINTF("IP           = %s\n", m_clientConfig._sta_static_ip.toString().c_str());
		LOG_PRINTF("Gateway      = %s\n", m_clientConfig._sta_static_gw.toString().c_str());
		LOG_PRINTF("Network mask = %s\n", m_clientConfig._sta_static_sn.toString().c_str());
		LOG_PRINTF("DNS1         = %s\n", m_clientConfig._sta_static_dns1.toString().c_str());
		LOG_PRINTF("DNS2         = %s\n", m_clientConfig._sta_static_dns2.toString().c_str());
	}

	void displayCredentials()
	{
		for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
			LOG_PRINTF("Credentials #%d:\n", i);
			LOG_PRINTF("SSID: %s\n", m_managerConfig.m_credentials[i].m_ssid[0] ? m_managerConfig.m_credentials[i].m_ssid : "<empty>");
			LOG_PRINTF("PASS: %s\n", PASSWORD_STR(m_managerConfig.m_credentials[i].m_password));
		}
	}

	void displayLastWifiParams(WiFiMultiSSID::LastParams &params)
	{
		LOG_PRINTF("Last WiFi parameters:\n");
		LOG_PRINTF("SSID : %s\n", params.m_credentials.m_ssid[0] ? params.m_credentials.m_ssid : "<empty>");
		LOG_PRINTF("PASS : %s\n", PASSWORD_STR(params.m_credentials.m_password));
		LOG_PRINTF("BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n", params.m_bssid[0], params.m_bssid[1], params.m_bssid[2], params.m_bssid[3], params.m_bssid[4], params.m_bssid[5]);
		LOG_PRINTF("CHAN : %d\n", params.m_channel);
	}

	uint8_t connectMultiWiFi()
	{
		uint8_t status;
		LOG_PRINTF("Connecting to WiFi\n");

		//
		// Set static IP, Gateway, Subnetmask, DNS1 and DNS2
		//

		WiFi.config(
			m_clientConfig._sta_static_ip,
			m_clientConfig._sta_static_gw,
			m_clientConfig._sta_static_sn,
			m_clientConfig._sta_static_dns1,
			m_clientConfig._sta_static_dns2);

		// first try to connect quickly using previous parameters
		status = m_wifiMulti.fastReconnect(
			m_lastWiFiParams,
			[=] {
				// periodically reset watchdog
				watchdogReset();
			},
			WIFI_RETRIES,
			WIFI_TIMEOUT);

		// if the fast reconnect failed, do a full featured connect with scan:
		if (status != WL_CONNECTED) {
			// attempt connection to all specified WiFi networks, with maximum of WIFI_RETRIES retries
			status = m_wifiMulti.connect(
				[=] {
					// periodically reset watchdog
					watchdogReset();
				},
				WIFI_RETRIES,
				WIFI_TIMEOUT);

			if (status == WL_CONNECTED) {
				LOG_PRINTF("WiFi connected\n");
				LOG_PRINTF("SSID: %s, RSSI = %d\n", WiFi.SSID().c_str(), WiFi.RSSI());
				LOG_PRINTF("Channel: %d, IP address: %s\n", WiFi.channel(), WiFi.localIP().toString().c_str());
			} else {
				LOG_PRINTF("WiFi connection failed, rebooting...\n");
				delay(5000);
				// To avoid unnecessary DRD
				m_drd->stop();
				// now restart
				ESP.restart();
			}
		}
		return status;
	}

	void wifiReconnectIfNeeded()
	{
		if ((WiFi.status() != WL_CONNECTED)) {
			// clear connection status
			m_connected = false;

			LOG_PRINTF("\nWiFi lost. Call connectMultiWiFi in loop\n");
			connectMultiWiFi();

			// notify waiting tasks that we are successfully connected
			m_connected = true;
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

		memset((void *)&m_managerConfig, 0, sizeof(m_managerConfig));
		memset((void *)&m_clientConfig, 0, sizeof(m_clientConfig));

		if (file) {
			file.readBytes((char *)&m_managerConfig, sizeof(m_managerConfig));
			file.readBytes((char *)&m_clientConfig, sizeof(m_clientConfig));
			file.close();

			if (m_managerConfig.m_checksum != calcChecksum((uint8_t *)&m_managerConfig, sizeof(m_managerConfig) - sizeof(m_managerConfig.m_checksum))) {
				LOG_PRINTF("Config checksum failed!\n");
				return false;
			} else {
				LOG_PRINTF("Config loaded correctly\n");
			}

			displayClientConfig();
			displayCredentials();
			return true;
		} else {
			LOG_PRINTF("Loading of config failed!\n");
			return false;
		}
	}

	bool wifiLoadLastParams()
	{
		File file = SPIFFS.open(LAST_PARAMS_FILENAME, "r");
		LOG_PRINTF("Loading last params...\n");

		memset((void *)&m_lastWiFiParams, 0, sizeof(m_lastWiFiParams));

		if (file) {
			file.readBytes((char *)&m_lastWiFiParams, sizeof(m_lastWiFiParams));
			file.close();
			LOG_PRINTF("Last params loading succeeded\n");

			// sometimes it can happen that last params don't contain any password
			// (this can happen when the connection is completed before the AP wizard finishes)
			// In that case try to fill the password by matching the SSID:
			if (!m_lastWiFiParams.m_credentials.m_password[0]) {
				for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
					if (String(m_managerConfig.m_credentials[i].m_ssid) == String(m_lastWiFiParams.m_credentials.m_ssid)) {
						strcpy(m_lastWiFiParams.m_credentials.m_password, m_managerConfig.m_credentials[i].m_password);
						break;
					}
				}
			}

			displayLastWifiParams(m_lastWiFiParams);
			return true;
		} else {
			LOG_PRINTF("Last params loading failed!\n");
			return false;
		}
	}

	void wifiEraseConfiguration()
	{
		LOG_PRINTF("Erasing config...\n");
		memset((void *)&m_managerConfig, 0, sizeof(m_managerConfig));
		memset((void *)&m_clientConfig, 0, sizeof(m_clientConfig));
		wifiSaveConfiguration();

		LOG_PRINTF("Erasing last params...\n");
		memset((void *)&m_lastWiFiParams, 0, sizeof(m_lastWiFiParams));
		wifiSaveLastParams();
	}

	void wifiSaveConfiguration()
	{
		File file = SPIFFS.open(CONFIG_FILENAME, "w");
		LOG_PRINTF("Saving config...\n");

		if (file) {
			m_managerConfig.m_checksum = calcChecksum((uint8_t *)&m_managerConfig, sizeof(m_managerConfig) - sizeof(m_managerConfig.m_checksum));
			file.write((uint8_t *)&m_managerConfig, sizeof(m_managerConfig));

			displayClientConfig();
			displayCredentials();

			file.write((uint8_t *)&m_clientConfig, sizeof(m_clientConfig));
			file.close();
			LOG_PRINTF("Config saved successfully\n");
		} else {
			LOG_PRINTF("Failed to save config!\n");
		}
	}

	void wifiSaveLastParams()
	{
		File file = SPIFFS.open(LAST_PARAMS_FILENAME, "w");
		LOG_PRINTF("Saving last params...\n");

		if (file) {
			file.write((uint8_t *)&m_lastWiFiParams, sizeof(m_lastWiFiParams));
			file.close();
			LOG_PRINTF("Last params saved successfully\n");
		} else {
			LOG_PRINTF("Failed to save last params!\n");
		}
	}

	void onStationConnected(WiFiEvent_t event, WiFiEventInfo_t info)
	{
		WiFiMultiSSID::LastParams lastParams;

		// copy ssid
		memset(lastParams.m_credentials.m_ssid, 0, sizeof(lastParams.m_credentials.m_ssid));
		memcpy(lastParams.m_credentials.m_ssid, info.connected.ssid, MAX(info.connected.ssid_len, sizeof(lastParams.m_credentials.m_ssid) - 1));

		// look for password - find a match for given SSID in our AP configuration
		for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
			if (String(m_managerConfig.m_credentials[i].m_ssid) == String(lastParams.m_credentials.m_ssid)) {
				strcpy(lastParams.m_credentials.m_password, m_managerConfig.m_credentials[i].m_password);
				break;
			}
		}

		// copy bssid
		memcpy(lastParams.m_bssid, info.connected.bssid, sizeof(info.connected.bssid));

		// keep current channel number
		lastParams.m_channel = info.connected.channel;

		LOG_PRINTF("Station connected!\n");

		// check if the configuration is different than the one currently cached
		if (memcmp(&lastParams, &m_lastWiFiParams, sizeof(lastParams)) != 0) {
			LOG_PRINTF("Last WiFi params have changed!\n");
			// copy the new configuration and save it
			memcpy(&m_lastWiFiParams, &lastParams, sizeof(lastParams));
			wifiSaveLastParams();	
		} else {
			LOG_PRINTF("Got the same WiFi params again\n");
		}

		displayLastWifiParams(lastParams);
	}

	void wifiSetup()
	{
		LOG_PRINTF("Starting Wifi Manager using SPIFFS on %s %s %s\n", ARDUINO_BOARD, ESP_ASYNC_WIFIMANAGER_VERSION, ESP_DOUBLE_RESET_DETECTOR_VERSION);

		// disable watchdgog as formatting may take a long time
		watchdogEnable(false);
		if (!SPIFFS.begin(true)) {
			LOG_PRINTF("SPIFFS/LittleFS failed! Already tried formatting.\n");

			if (!SPIFFS.begin()) {
				// prevents debug info from the library to hide err message.
				delay(100);
				LOG_PRINTF("SPIFFS failed!. Please use LittleFS or EEPROM. Stay forever\n");
				watchdogEnable(true);

				while (true) {
					delay(1);
				}
			}
		}
		// re-enable watchdog
		watchdogEnable(true);

		File root = SPIFFS.open("/");
		File file = root.openNextFile();

		while (file) {
			String fileName = file.name();
			size_t fileSize = file.size();
			LOG_PRINTF("FS File: %s, size: %f kB\n", fileName.c_str(), fileSize / 1024.0);
			file = root.openNextFile();
		}

		LOG_PRINTF("\n");

		m_drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);

		if (!m_drd)
			LOG_PRINTF("Can't instantiate. Disable DRD feature\n");

		// start manager now
		wifiStartManager();
	}

	void wifiStartManager()
	{
		m_connected = false;

		bool shallRunAccessPoint = false;

		//
		// load saved configuration
		//

		bool configDataLoaded = false;
		if (wifiLoadConfiguration() && wifiLoadLastParams()) {
			configDataLoaded = true;
			LOG_PRINTF("Got stored WiFiMultiSSID::Credentials. Timeout 120s for Config Portal\n");
		} else {
			// Enter CP only if no stored SSID on flash and file
			LOG_PRINTF("Open Config Portal without Timeout: No stored WiFiMultiSSID::Credentials.\n");
			shallRunAccessPoint = true;
		}

		//
		// set default host name if explicit hostname was provided
		//

		if (!m_managerConfig.m_hostName[0]) {
			strcpy(m_managerConfig.m_hostName, m_ssid.c_str());
		}

		LOG_PRINTF("Using host name %s\n", m_managerConfig.m_hostName);

		//
		// create instance of WiFi manager
		//

		#if (USING_ESP32_S2 || USING_ESP32_C3)
		ESPAsync_WiFiManager manager(&m_httpServer, NULL, m_managerConfig.m_hostName);
		#else
		DNSServer m_dnsServer;
		ESPAsync_WiFiManager manager(&m_httpServer, &m_dnsServer, m_managerConfig.m_hostName);
		#endif

		#if USE_CUSTOM_AP_IP
		// set custom ip for portal
		manager.setAPStaticIPConfig(m_apIpAddress, m_apGateway, m_apMask);
		#endif

		manager.setMinimumSignalQuality(-1);			// no minimum signal quality
		manager.setConfigPortalChannel(0);				// Set config portal channel, default = 1. Use 0 => random channel from 1-13

		#if USING_CORS_FEATURE
		manager.setCORSHeader("Your Access-Control-Allow-Origin");
		#endif

		// SSID to uppercase
		m_ssid.toUpperCase();
		m_password = "";

		// if we have been previously connected to some network, specify 2 minute timeout for AP mode
		if ((manager.WiFi_SSID() != "") || configDataLoaded) {
			manager.setConfigPortalTimeout(120);
			LOG_PRINTF("Got ESP Self-Stored WiFiMultiSSID::Credentials. Timeout 120s for Config Portal\n");
		}

		if (m_managerConfig.m_forceAp) {
			LOG_PRINTF("AP forced\n");
			shallRunAccessPoint = true;
		}

		// if we don't have valid credentials, force AP too
		if (!m_managerConfig.m_credentials->m_ssid[0]) {
			LOG_PRINTF("No valid WiFi credentials stored, AP forced\n");
			shallRunAccessPoint = true;
		}

		if (m_drd->detectDoubleReset()) {
			// DRD, disable timeout.
			manager.setConfigPortalTimeout(0);
			LOG_PRINTF("Open Config Portal without Timeout: Double Reset Detected\n");
			shallRunAccessPoint = true;
		}

		ESPAsync_WMParameter customHostName("hostName",  "host name", m_managerConfig.m_hostName,  HOST_NAME_LEN);
		manager.addParameter(&customHostName);

		// register event handler for SYSTEM_EVENT_STA_CONNECTED message
		// (so we can cache details about the last connection)
		if (!m_wifiEventId) {
			m_wifiEventId = WiFi.onEvent(
				[](system_event_id_t event, system_event_info_t info) -> void {
					WiFiContext::instance().onStationConnected(event, info);
				},
				SYSTEM_EVENT_STA_CONNECTED);
		}

		//
		// shall we run AccesPoint?
		//

		if (shallRunAccessPoint) {

			// show orange color indicating we are in setup mode
			Display::instance().highPriorityColor(utils::HSVtoRGB(30, 100, BRIGHTNESS), true);

			// clear last wifi params
			memset((void *)&m_lastWiFiParams, 0, sizeof(m_lastWiFiParams));
			wifiSaveLastParams();

			#if USE_CUSTOM_AP_IP
			LOG_PRINTF("Starting configuration portal @%s\n", m_apIpAddress);
			#else
			LOG_PRINTF("Starting configuration portal @%s\n", "192.168.4.1");
			#endif

			// configure our stored static ip config if available
			manager.setSTAStaticIPConfig(m_clientConfig);
			LOG_PRINTF("SSID = %s, PWD = %s\n", m_ssid.c_str(), m_password.length() ? m_password.c_str() : "<none>");

			// Starts an access point (with disabled watchdog)
			watchdogOverride([&]{
				if (!manager.startConfigPortal((const char *)m_ssid.c_str(), m_password.c_str())){
					LOG_PRINTF("Not connected to WiFi but continuing anyway.\n");
				} else {
					LOG_PRINTF("WiFi connected\n");
				}
			});

			// copy WiFi configuration from manager to our local structures
			for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
				String tempSSID = manager.getSSID(i);
				String tempPW = manager.getPW(i);

				if (tempSSID.length()) {
					LOG_PRINTF("Updating WiFi credentials %d:\n", i);
					LOG_PRINTF("SSID: %s -> %s\n", m_managerConfig.m_credentials[i].m_ssid[0] ? m_managerConfig.m_credentials[i].m_ssid : "<empty>", tempSSID.length() ? tempSSID.c_str() : "<empty>");
					LOG_PRINTF("PASS: %s -> %s\n\n", PASSWORD_STR(m_managerConfig.m_credentials[i].m_password), PASSWORD_STR(tempPW.c_str()));

					if (strlen(tempSSID.c_str()) < sizeof(m_managerConfig.m_credentials[i].m_ssid) - 1)
						strcpy(m_managerConfig.m_credentials[i].m_ssid, tempSSID.c_str());
					else
						strncpy(m_managerConfig.m_credentials[i].m_ssid, tempSSID.c_str(), sizeof(m_managerConfig.m_credentials[i].m_ssid) - 1);

					if (strlen(tempPW.c_str()) < sizeof(m_managerConfig.m_credentials[i].m_password) - 1)
						strcpy(m_managerConfig.m_credentials[i].m_password, tempPW.c_str());
					else
						strncpy(m_managerConfig.m_credentials[i].m_password, tempPW.c_str(), sizeof(m_managerConfig.m_credentials[i].m_password) - 1);
				} else {
					LOG_PRINTF("No new credentials configured at position %d\n", i);
				}
			}

			// read static IP address configuration from manager
			manager.getSTAStaticIPConfig(m_clientConfig);

			// read new host name from manager
			strcpy(m_managerConfig.m_hostName, customHostName.getValue());

			// clear force flag
			m_managerConfig.m_forceAp = false;

			// store selected configuration
			wifiSaveConfiguration();

			// hide AP notification
			Display::instance().highPriorityColor(utils::HSVtoRGB(30, 100, BRIGHTNESS), false);
		}

		//
		// if needed, load WiFi configuration from persistent memory
		//

		if (!configDataLoaded) {
			wifiLoadConfiguration();
			wifiLoadLastParams();
		}

		//
		// add all configured access points
		//

		for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
			// Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
			if ((String(m_managerConfig.m_credentials[i].m_ssid) != "") && (strlen(m_managerConfig.m_credentials[i].m_password) >= MIN_AP_PASSWORD_SIZE)) {
				LOG_PRINTF("* Add SSID = %s, pw = %s\n", m_managerConfig.m_credentials[i].m_ssid, PASSWORD_STR(m_managerConfig.m_credentials[i].m_password));
				m_wifiMulti.addAP(m_managerConfig.m_credentials[i].m_ssid, m_managerConfig.m_credentials[i].m_password);
			}
		}

		//
		// connect to configured WiFi network
		//

		unsigned long startedAt = millis();

		if (WiFi.status() != WL_CONNECTED) {
			LOG_PRINTF("ConnectMultiWiFi in setup\n");
			connectMultiWiFi();
		}

		LOG_PRINTF("After waiting %f secs more in setup(), connection result is \n", (float)(millis() - startedAt) / 1000);

		// we can stop double reset detector
		if (m_drd) {
			m_drd->stop();
		}

		if (WiFi.status() == WL_CONNECTED) {
			// from now on we are connected
			m_connected = true;
			LOG_PRINTF("Connected. Local IP: %s\n", WiFi.localIP().toString().c_str());
		}
		else {
			LOG_PRINTF(manager.getStatus(WiFi.status()));
		}
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

	AsyncWebServer *httpServer()
	{
		return &m_httpServer;
	}

	void loop()
	{
		while (1) {

			watchdogReset();

			//
			// if wifi reconfiguration was requested, execute it on this thread
			//

			if (m_shallReconfigure) {
				LOG_PRINTF("WiFi reconfiguration initiated!\n");

				// force ap in settings
				m_managerConfig.m_forceAp = true;
				wifiSaveConfiguration();

				// disconnect from wifi
				WiFi.disconnect();
#if RESET_WHEN_RECONFIGURING_WIFI
				// and reboot the board
				watchdogScheduleReboot();

				// wait until it reboots
				while (1) {
					delay(100);
				}
#else
				wifiStartManager();
#endif
				LOG_PRINTF("WiFi reconfiguration finished\n");
				m_shallReconfigure = false;
			}

			if (m_shallReset) {
				LOG_PRINTF("WiFi reset initiated!\n");

				// erase settings
				wifiEraseConfiguration();

				// disconnect from wifi and erase credentials
				WiFi.disconnect(false, true);
#if RESET_WHEN_RECONFIGURING_WIFI
				// and reboot the board
				watchdogScheduleReboot();

				// wait until it reboots
				while (1) {
					delay(100);
				}
#else
				wifiStartManager();
#endif
				LOG_PRINTF("WiFi reset finished\n");
				m_shallReset = false;
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

void wifiTask(void *pvParameters __attribute__((unused)))
{
	// init
	WiFiContext::instance().wifiSetup();

	// infinite loop
	WiFiContext::instance().loop();
}

bool wifiReconfigure()
{
	return WiFiContext::instance().reconfigure();
}

bool wifiReset()
{
	return WiFiContext::instance().reset();
}

bool wifiIsConnected()
{
	return WiFiContext::instance().connected();
}

void wifiWaitForConnection()
{
	while (!wifiIsConnected()) {
		delay(500);
	}
}

AsyncWebServer *wifiGetHttpServer()
{
	return WiFiContext::instance().httpServer();
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
