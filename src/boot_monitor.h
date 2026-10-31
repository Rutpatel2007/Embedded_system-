#ifndef TRUESENSE_BOOT_MONITOR_H
#define TRUESENSE_BOOT_MONITOR_H

#include <Arduino.h>

namespace boot_monitor_module {
bool init();
bool verifyBootChain();
bool bootVerifyPassed();
uint32_t bootVerifyCount();
int32_t bootVerifyFailureIndex();
void clearBootVerifyState();
void registerCommands();
void startTask();
void task(void* parameter);
void initializeWatchdog();
void feedWatchdog();
void reportTaskStack(const char* name, uint32_t& lastReportMs);
}

#endif
