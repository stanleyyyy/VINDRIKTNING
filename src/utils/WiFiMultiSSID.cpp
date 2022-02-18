#include "WiFiMultiSSID.h"
#include <limits.h>
#include <string.h>
#include "utils.h"

WiFiMultiSSID::WiFiMultiSSID()
{
}

WiFiMultiSSID::~WiFiMultiSSID()
{
	m_apList.clear();
}

bool WiFiMultiSSID::addAP(const char *ssid, const char *passphrase)
{
	WiFiMultiSSID::Credentials newAP;

	if (!ssid || *ssid == 0x00 || strlen(ssid) > 31) {
		// fail SSID too long or missing!
		LOG_PRINTF("[WIFI][m_apListAdd] no ssid or ssid too long\n");
		return false;
	}

	if (passphrase && strlen(passphrase) > 63) {
		// fail passphrase too long!
		LOG_PRINTF("[WIFI][m_apListAdd] passphrase too long\n");
		return false;
	}

	strcpy(newAP.m_ssid, ssid);

	if (passphrase && *passphrase != 0x00) {
		strcpy(newAP.m_password, passphrase);
	} else {
		newAP.m_password[0] = 0;
	}

	m_apList.push_back(newAP);
	LOG_PRINTF("[WIFI][m_apListAdd] add SSID: %s\n", newAP.m_ssid);
	return true;
}

uint8_t WiFiMultiSSID::fastReconnect(const WiFiMultiSSID::LastParams &params, std::function<void(void)> periodicCb, uint32_t retries, uint32_t timeout)
{
	if (!params.m_credentials.m_ssid[0] || !params.m_credentials.m_password[0] || !params.m_bssid[0]) {
		LOG_PRINTF("[WIFI] fast reconnect not possible, parameters are invalid\n");
		return WL_CONNECT_FAILED;
	}

	// are we already connected?
	uint8_t status = WiFi.status();
	if (status == WL_CONNECTED) {
		// does the SSID we are currently connected to match one our requested
		// SSIDs?
		for (uint32_t x = 0; x < m_apList.size(); x++) {
			if (WiFi.SSID() == m_apList[x].m_ssid) {
				// it does, so we are connected
				return status;
			}
		}

		// no match found, disconnect
		WiFi.disconnect(false, false);

		// give it a bit of time and retrieve the Wifi status again
		delay(10);
		status = WiFi.status();
	}

	// try to connect as many times are specified
	while (retries--) {
		LOG_PRINTF("[WIFI] Connecting BSSID: %02X:%02X:%02X:%02X:%02X:%02X, SSID: %s, channel: %d\n", params.m_bssid[0], params.m_bssid[1], params.m_bssid[2], params.m_bssid[3], params.m_bssid[4], params.m_bssid[5], params.m_credentials.m_ssid, params.m_channel);

		WiFi.begin(params.m_credentials.m_ssid, params.m_credentials.m_password, params.m_channel, params.m_bssid);
		status = WiFi.status();

		auto startTime = millis();

		// wait for connection, fail, or timeout
		while (status != WL_CONNECTED && status != WL_NO_SSID_AVAIL && status != WL_CONNECT_FAILED && (millis() - startTime) <= timeout) {

			// call periodic callback
			if (periodicCb) {
				periodicCb();
			}

			delay(10);
			status = WiFi.status();
		}

		switch (status) {
		case WL_CONNECTED:
			LOG_PRINTF("[WIFI] Connecting done.\n");
			LOG_PRINTF("[WIFI] SSID: %s\n", WiFi.SSID().c_str());
			LOG_PRINTF("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
			LOG_PRINTF("[WIFI] MAC: %s\n", WiFi.BSSIDstr().c_str());
			LOG_PRINTF("[WIFI] Channel: %d\n", WiFi.channel());
			// we have been connected, we may leave
			return status;
		case WL_NO_SSID_AVAIL:
			LOG_PRINTF("[WIFI] Connection Failed, AP not found.\n");
			// hard error, SSID is not available, leave
			return status;
		case WL_CONNECT_FAILED:
			LOG_PRINTF("[WIFI] Connection Failed.\n");
			// failure, let's repeat the reconnection
			WiFi.disconnect(true);	// Disconnect from the network
			WiFi.mode(WIFI_OFF);	// Switch WiFi off
			WiFi.disconnect(false);	// Reconnect the network
			WiFi.mode(WIFI_STA);	// Switch WiFi off
			break;
		default:
			LOG_PRINTF("[WIFI] Connection Failed (%d).\n", status);
			// other error type, try reconnection
			break;
		}
	}

	return status;
}

uint8_t WiFiMultiSSID::connect(std::function<void(void)> periodicCb, uint32_t retries, uint32_t timeout)
{
	// are we already connected?
	uint8_t status = WiFi.status();
	if (status == WL_CONNECTED) {
		// does the SSID we are currently connected to match one our requested
		// SSIDs?
		for (uint32_t x = 0; x < m_apList.size(); x++) {
			if (WiFi.SSID() == m_apList[x].m_ssid) {
				// it does, so we are connected
				LOG_PRINTF("[WIFI]: currently connected SSID matches our requested SSID (%s)\n", m_apList[x].m_ssid);
				return status;
			}
		}

		// no match found, disconnect
		LOG_PRINTF("[WIFI]: no match for selected SSID found, disconnecting\n");
		WiFi.disconnect(false, false);

		// give it a bit of time and retrieve the Wifi status again
		delay(10);
		status = WiFi.status();
	}

	//
	// asynchronous scan for wifi networks
	//

	uint32_t startMillis = millis();
	LOG_PRINTF("[WIFI]: Initiating scan (timeout = %d ms)\n", timeout);
	int16_t scanResult = WiFi.scanNetworks(true, false, false);
	LOG_PRINTF("[WIFI]: scanNetworks() returned %d\n", scanResult);

	// polling wait until it finishes.
	// we will ignore WIFI_SCAN_FAILED erros as it may be incorrectly
	// triggered even when the scan is about to finish fine 
	while ((millis() - startMillis) < timeout) {
		scanResult = WiFi.scanComplete();
		if (scanResult >= 0) {
			// we got some results
			LOG_PRINTF("[WIFI]: scan finished, num results = %d\n", scanResult);
			break;
		}

		// let's check first if we have been connected in the meantime
		// (this can happen, there is a race condition between scan and connect)
		if (WiFi.status() == WL_CONNECTED) {
			LOG_PRINTF("[WIFI] connected in the meantime!\n");
			return WL_CONNECTED;
		}

		// call periodic callback
		if (periodicCb) {
			periodicCb();
		}
		delay(100);
	};

	// if we are still scanning, leave with error (it means that timeout has expired)
	if (scanResult == WIFI_SCAN_RUNNING) {
		// scan is running
		LOG_PRINTF("[WIFI]: scan is still running, timeout expired!\n");
		return WL_NO_SSID_AVAIL;
	} else if (scanResult < 0) {
		// we had some other error...
		if (WiFi.status() == WL_CONNECTED) {
			LOG_PRINTF("[WIFI] connected in the meantime!\n");
			return WL_CONNECTED;
		}

		LOG_PRINTF("[WIFI] scan failed\n");
		return scanResult;
	} else if (scanResult >= 0) {
		// scan done analyze
		Credentials bestNetwork;
		int bestNetworkDb = INT_MIN;
		uint8_t bestBSSID[6];
		int32_t bestChannel = 0;

		LOG_PRINTF("[WIFI] scan done\n");

		if (scanResult == 0) {
			LOG_PRINTF("[WIFI] no networks found\n");
		} else {
			LOG_PRINTF("[WIFI] %d networks found\n", scanResult);

			for (int8_t i = 0; i < scanResult; ++i) {
				String ssid_scan;
				int32_t rssi_scan;
				uint8_t sec_scan;
				uint8_t *BSSID_scan;
				int32_t chan_scan;

				WiFi.getNetworkInfo(i, ssid_scan, sec_scan, rssi_scan, BSSID_scan, chan_scan);

				bool known = false;
				for (uint32_t x = 0; x < m_apList.size(); x++) {
					Credentials entry = m_apList[x];

					if (ssid_scan == entry.m_ssid) {
						// SSID match
						known = true;

						if (rssi_scan > bestNetworkDb) {
							// best network
							if (sec_scan == WIFI_AUTH_OPEN || entry.m_password) {
								// check for passphrase if not open wlan
								bestNetworkDb = rssi_scan;
								bestChannel = chan_scan;
								memcpy((void *)&bestNetwork, (void *)&entry, sizeof(bestNetwork));
								memcpy((void *)&bestBSSID, (void *)BSSID_scan, sizeof(bestBSSID));
							}
						}
						break;
					}
				}

				if (known) {
					LOG_PRINTF("[WIFI]  --->   %02d: [%d][%02X:%02X:%02X:%02X:%02X:%02X] %s (%d) %c\n", i, chan_scan, BSSID_scan[0], BSSID_scan[1], BSSID_scan[2], BSSID_scan[3], BSSID_scan[4], BSSID_scan[5], ssid_scan.c_str(), rssi_scan, (sec_scan == WIFI_AUTH_OPEN) ? ' ' : '*');
				} else {
					LOG_PRINTF("[WIFI] 	   %02d: [%d][%02X:%02X:%02X:%02X:%02X:%02X] %s (%d) %c\n", i, chan_scan, BSSID_scan[0], BSSID_scan[1], BSSID_scan[2], BSSID_scan[3], BSSID_scan[4], BSSID_scan[5], ssid_scan.c_str(), rssi_scan, (sec_scan == WIFI_AUTH_OPEN) ? ' ' : '*');
				}
			}
		}

		// clean up ram
		WiFi.scanDelete();

		// did we find a ssid we have been looking for?
		if (bestNetwork.m_ssid[0]) {

			WiFiMultiSSID::LastParams params;
			strcpy(params.m_credentials.m_ssid, bestNetwork.m_ssid);
			strcpy(params.m_credentials.m_password, bestNetwork.m_password);
			memcpy(params.m_bssid, bestBSSID, sizeof(bestBSSID));
			params.m_channel = bestChannel;

			status = fastReconnect(params, periodicCb, retries, timeout);
		} else {
			LOG_PRINTF("[WIFI] no matching wifi found!\n");
		}
	}

	return status;
}
