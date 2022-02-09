#pragma once

//
// task to handle NTP
//

void fetchTimeFromNTP(void *pvParameters __attribute__((unused)));
uint64_t compensatedMillis();
