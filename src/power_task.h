#ifndef PROOV_POWER_TASK_H
#define PROOV_POWER_TASK_H

#include <Arduino.h>

namespace power_task_module {
bool init();
int32_t readCurrent_mA();
int32_t readPower_mW();
bool calibrateIdleBaseline();
bool isEquipmentActive();
void registerPowerCommands();
void startPowerTask();
void powerTask(void* parameter);
}

#endif
