#ifndef PROOV_SERIAL_CMD_H
#define PROOV_SERIAL_CMD_H

#include <Arduino.h>

namespace serial_cmd_module {
using CommandHandler = void (*)(const char* args);
bool init();
bool registerCommand(const char* name, CommandHandler handler);
void serialCommandTask(void* parameter);
}

#endif
