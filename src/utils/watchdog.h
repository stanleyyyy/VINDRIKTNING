#pragma once
#include <functional>

void watchdogInit();
void watchdogReset();
void watchdogScheduleReboot();
bool watchdogEnable(const bool &enable);
void watchdogOverride(std::function<void(void)> fn);

uint32_t watchdogTimeToReset();

