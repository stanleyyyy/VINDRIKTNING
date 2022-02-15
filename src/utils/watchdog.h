#pragma once

void watchdogInit();
void watchdogReset();
void watchdogScheduleReboot();
void watchdogEnable(const bool &enable);

uint32_t timeToReset();

