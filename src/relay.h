#ifndef TRUESENSE_RELAY_H
#define TRUESENSE_RELAY_H

#include <Arduino.h>

namespace relay_module {
bool init();
void setRelay(bool enabled);
bool requestRelay(bool enabled);
bool getRelayState();
uint32_t getDropCount();
void startRelayTask();
void registerRelayCommands();
void relayTask(void* parameter);
}

#endif
