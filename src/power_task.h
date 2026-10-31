#ifndef TRUESENSE_POWER_TASK_H
#define TRUESENSE_POWER_TASK_H

#include <Arduino.h>

namespace power_task_module {
bool init();
int32_t readCurrent_mA();
int32_t readPower_mW();
bool calibrateIdleBaseline();
bool isEquipmentActive(int32_t filteredPowerMilliWatts);
const char* equipmentModeName();
void requestEquipmentAuto();
uint32_t i2cErrorCount();
void registerPowerCommands();
void startPowerTask();
void powerTask(void* parameter);
}

#endif
