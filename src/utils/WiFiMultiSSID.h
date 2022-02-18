#pragma once

#include "WiFi.h"
#include <vector>
#include <functional>
#include "config.h"

//
// WiFiMulti alternative with many improvements
//

class WiFiMultiSSID {
public:

	struct Credentials {
		char m_ssid[SSID_MAX_LEN] = {0};
		char m_password[PASS_MAX_LEN] = {0};
	} ;

	struct LastParams {
		Credentials m_credentials;
		uint8_t m_bssid[6] = {0};
		int m_channel = -1;
	} WiFiLastParams;

	WiFiMultiSSID();
	~WiFiMultiSSID();

	bool addAP(const char *ssid, const char *passphrase = NULL);

	uint8_t fastReconnect(const WiFiMultiSSID::LastParams &params, std::function<void(void)> periodicCb = 0, uint32_t retries = 1, uint32_t timeout = 5000);
	uint8_t connect(std::function<void(void)> periodicCb = 0, uint32_t retries = 1, uint32_t timeout = 5000);

private:
	std::vector<Credentials> m_apList;
};
