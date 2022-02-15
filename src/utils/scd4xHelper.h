#pragma once

#include <Arduino.h>

#include "config.h"
#include "utils.h"
#include "watchdog.h"

#if (USE_CO2_SENSOR == 1)

#include <SensirionI2CScd4x.h>
#include <Wire.h>

class Scd4xHelper {
private:
	// CO2 sensor
	SensirionI2CScd4x m_scd4x;

	float m_temperature;
	float m_humidity;
	uint16_t m_co2;

public:

	Scd4xHelper()
	: m_temperature(0)
	, m_humidity(0)
	, m_co2(0)
	{
	}

	bool init(const int &sda, const int &scl)
	{
		// Initialize I2C
		Wire.begin(sda, scl);

		//
		// initialize CO2 sensor
		//

		m_scd4x.begin(Wire);

		// stop potentially previously started measurement
		uint16_t error = m_scd4x.stopPeriodicMeasurement();
		if (error) {
			LOG_PRINTF("Error trying to execute stopPeriodicMeasurement(): ");
			char errorMessage[256];
			errorToString(error, errorMessage, 256);
			LOG_PRINTF("%s\n", errorMessage);
			return false;
		}

		uint16_t serial0;
		uint16_t serial1;
		uint16_t serial2;
		error = m_scd4x.getSerialNumber(serial0, serial1, serial2);
		if (error) {
			LOG_PRINTF("Error trying to execute getSerialNumber(): ");
			char errorMessage[256];
			errorToString(error, errorMessage, 256);
			LOG_PRINTF("%s\n", errorMessage);
			return false;
		} else {
			LOG_PRINTF("Serial: 0x%2x.%2x.%2x\n", serial0, serial1, serial2);
		}

		// configure sensor altitude
		error = m_scd4x.setSensorAltitude(ALTITUDE);
		if (error) {
			LOG_PRINTF("Error trying to execute setSensorAltitude(): ");
			char errorMessage[256];
			errorToString(error, errorMessage, 256);
			LOG_PRINTF("%s\n", errorMessage);
			return false;
		} else {
			LOG_PRINTF("sensor altitude was successfully set to %d\n", ALTITUDE);
		}

		// Start Measurement
		error = m_scd4x.startPeriodicMeasurement();
		if (error) {
			LOG_PRINTF("Error trying to execute startPeriodicMeasurement(): ");
			char errorMessage[256];
			errorToString(error, errorMessage, 256);
			LOG_PRINTF("%s\n", errorMessage);
			return false;
		}

		return true;
	}

	bool getSensorData(float &temperature, float &humidity, uint16_t &co2)
	{
		// Read Measurement
		uint16_t error = m_scd4x.readMeasurement(co2, temperature, humidity);
		if (error) {
			char errorMessage[256];
			errorToString(error, errorMessage, 256);
			LOG_PRINTF("Error trying to execute readMeasurement(): %s\n", errorMessage);
			return false;
		} else if (co2 == 0) {
			LOG_PRINTF("Invalid sample detected, skipping.\n");
			return false;
		} else {
			// update internal values
			m_co2 = co2;
			m_temperature = temperature;
			m_humidity = humidity;
			return true;
		}
	}

	void setAmbientPressure(uint16_t pressure)
	{
		uint16_t error = m_scd4x.setAmbientPressure(AIR_PRESSURE);
		if (error) {
			LOG_PRINTF("Error trying to execute setAmbientPressure(): ");
			char errorMessage[256];
			errorToString(error, errorMessage, 256);
			LOG_PRINTF("%s\n", errorMessage);
		} else {
			LOG_PRINTF("sensor ambient pressure was successfully set to %d kPa\n", AIR_PRESSURE);
		}
	}

	int16_t forceRecalibration(const uint16_t &targetCo2Concentration)
	{
		int16_t frcCorrection = 0;

		// stop potentially previously started measurement
		uint16_t error = m_scd4x.stopPeriodicMeasurement();
		if (error) {
			LOG_PRINTF("Error trying to execute stopPeriodicMeasurement(): ");
			char errorMessage[256];
			errorToString(error, errorMessage, 256);
			LOG_PRINTF("%s\n", errorMessage);
		}

		// wait at least 500ms
		delay(500);

		uint16_t frc = 0;
		error = m_scd4x.performForcedRecalibration(targetCo2Concentration, frc);
		if (error) {
			LOG_PRINTF("Error trying to execute performForcedRecalibration(): ");
			char errorMessage[256];
			errorToString(error, errorMessage, 256);
			LOG_PRINTF("%s\n", errorMessage);
		} else {
			// calculate FRC correction based on datasheet
			if (frc != 0xFFFF) {
				frcCorrection = (int)frc - 0x8000;
			}
			LOG_PRINTF("forced recalibartion to %u ppm succeeded, FRC correction = %d ppm\n", targetCo2Concentration, frcCorrection);
		}

		// re-start measurements
		error = m_scd4x.startPeriodicMeasurement();
		if (error) {
			LOG_PRINTF("Error trying to execute startPeriodicMeasurement(): ");
			char errorMessage[256];
			errorToString(error, errorMessage, 256);
			LOG_PRINTF("%s\n", errorMessage);
		}

		return frcCorrection;
	}

	void enableAutomaticSelfCalibration(const bool &calibration)
	{
		// stop potentially previously started measurement
		uint16_t error = m_scd4x.stopPeriodicMeasurement();
		if (error) {
			LOG_PRINTF("Error trying to execute stopPeriodicMeasurement(): ");
			char errorMessage[256];
			errorToString(error, errorMessage, 256);
			LOG_PRINTF("%s\n", errorMessage);
		}

		error = m_scd4x.setAutomaticSelfCalibration(calibration);
		if (error) {
			LOG_PRINTF("Error trying to execute setAutomaticSelfCalibration(): ");
			char errorMessage[256];
			errorToString(error, errorMessage, 256);
			LOG_PRINTF("%s\n", errorMessage);
		} else {
			LOG_PRINTF("automatic self calibration was successfully configured to '%s'\n", calibration ? "true" : "false");
		}

		// re-start measurements
		error = m_scd4x.startPeriodicMeasurement();
		if (error) {
			LOG_PRINTF("Error trying to execute startPeriodicMeasurement(): ");
			char errorMessage[256];
			errorToString(error, errorMessage, 256);
			LOG_PRINTF("%s\n", errorMessage);
		}
	}
};

#endif // USE_CO2_SENSOR
