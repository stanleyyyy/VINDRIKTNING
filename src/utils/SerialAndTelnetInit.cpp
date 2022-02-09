#include "SerialAndTelnetInit.h"

// actual instance of TelnetSpy class
TelnetSpy SerialAndTelnet;

SemaphoreHandle_t SerialAndTelnetInit::m_mutex = NULL;
