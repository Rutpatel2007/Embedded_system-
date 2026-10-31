#ifndef TRUESENSE_RTC_TIME_H
#define TRUESENSE_RTC_TIME_H

#include <Arduino.h>

namespace rtc_time_module {
bool init();
uint32_t getTimestamp();
bool rtcLostPower();
bool setInitialTime(uint32_t epoch);
void registerRtcCommands();
void startRtcTimeTask();
void rtcTimeTask(void* parameter);
}

#endif
