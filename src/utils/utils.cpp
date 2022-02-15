#include "Arduino.h"

#include <stdarg.h>
#include "utils.h"
#include "config.h"
#include "SerialAndTelnetInit.h"
#include "../tasks/ntpTask.h"

#define LOG_SIZE_MAX 512

void logInit()
{

}

void printf_internal(const char *fmt, ...)
{
	char buf[LOG_SIZE_MAX];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, LOG_SIZE_MAX, fmt, ap);
	va_end(ap);

	if (SerialAndTelnetInit::lock()) {
		SERIAL.print(msToTimeStr(compensatedMillis()));
		SERIAL.print(": ");
		SERIAL.print(buf);
		SerialAndTelnetInit::unlock();
	}
}

char *msToTimeStr(uint64_t ms)
{
	static char timeBuf[32];

	unsigned long s = ms / 1000;
	unsigned long h = ((s % 86400L) / 3600);
	unsigned long m = ((s % 3600) / 60);

	s = (s % 60);
	ms = ms % 1000;

	snprintf(timeBuf, sizeof(timeBuf) - 1, "%02lu:%02lu:%02lu.%03lu", h, m, s, (unsigned long)ms);
	return timeBuf;
}

void longDelay(uint32_t ms)
{
	uint32_t start = millis();

	while ((millis() - start) < ms) {
		uint32_t toSleep = ms - (millis() - start);
		delay(toSleep);
	}
}