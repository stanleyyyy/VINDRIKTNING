#pragma once

void sensorTask(void *pvParameters __attribute__((unused)));

#if (USE_CO2_SENSOR == 1)
bool lastSensorData(uint16_t &pm2_5, float &temperature, float &humidity, uint16_t &co2);
#elif (USE_ENV_SENSOR == 1)
bool lastSensorData(uint16_t &pm2_5, float &temperature, float &humidity, float &pressure);
#else
bool lastSensorData(uint16_t &pm2_5);
#endif
