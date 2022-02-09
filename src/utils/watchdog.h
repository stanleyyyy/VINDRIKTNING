#pragma once

void watchdogInit();
void watchdogReset();
void watchdogScheduleReboot();
uint32_t timeToReset();

