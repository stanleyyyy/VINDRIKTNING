#pragma once

#include <Arduino.h>

#include "config/config.h"
#include "TelnetSpy.h"

// create instance of telnet/serial wrapper
extern TelnetSpy SerialAndTelnet;

#undef SERIAL
#define SERIAL  SerialAndTelnet

//
// printf macro
//

#define LOG_PRINTF(fmt, ...) printf_internal(PSTR(fmt), ##__VA_ARGS__)
void printf_internal(const char *fmt, ...);

void logInit();
char *msToTimeStr(uint64_t ms);

void longDelay(uint32_t ms);
