#include "serial_cmd.h"
#include "config.h"
#include "boot_monitor.h"
#include <ctype.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>

struct CommandEntry {
    char name[SERIAL_COMMAND_NAME_LENGTH];
    serial_cmd_module::CommandHandler handler;
};

static CommandEntry commandTable[SERIAL_MAX_COMMANDS]{};
static size_t commandCount;
static portMUX_TYPE commandTableMux = portMUX_INITIALIZER_UNLOCKED;

static char* trimWhitespace(char* value) {
    while (*value != '\0' && isspace(static_cast<unsigned char>(*value))) {
        ++value;
    }
    size_t length = strlen(value);
    while (length > 0 && isspace(static_cast<unsigned char>(value[length - 1]))) {
        value[--length] = '\0';
    }
    return value;
}

static void dispatchCommand(char* line) {
    char* trimmed = trimWhitespace(line);
    if (*trimmed == '\0') {
        return;
    }
    char* args = trimmed;
    while (*args != '\0' && !isspace(static_cast<unsigned char>(*args))) {
        ++args;
    }
    if (*args != '\0') {
        *args++ = '\0';
        args = trimWhitespace(args);
    }
    if (strcasecmp(trimmed, "HELP") == 0) {
        char names[SERIAL_MAX_COMMANDS][SERIAL_COMMAND_NAME_LENGTH]{};
        size_t count;
        portENTER_CRITICAL(&commandTableMux);
        count = commandCount;
        for (size_t index = 0; index < count; ++index) {
            snprintf(names[index], sizeof(names[index]), "%s", commandTable[index].name);
        }
        portEXIT_CRITICAL(&commandTableMux);
        static_cast<void>(names);
        static_cast<void>(count);
#ifdef TEST_MODE
        Serial.println("COMMANDS: STATS STATS RESET RELAY FP FP CLEAR ENROLL REENROLL VERIFY TAMPER SYNC SYNC NOW SYNC RESET HEALTH SETTIME CAL IDLE GAS CHAIN TEST TEST ALL TEST STATUS TEST SUMMARY TEST ABORT GO HELP");
#else
        Serial.println("COMMANDS: STATS STATS RESET RELAY FP FP CLEAR ENROLL REENROLL VERIFY TAMPER SYNC SYNC NOW SYNC RESET HEALTH SETTIME CAL IDLE GAS CHAIN HELP");
#endif
        return;
    }
    serial_cmd_module::CommandHandler handler = nullptr;
    portENTER_CRITICAL(&commandTableMux);
    for (size_t index = 0; index < commandCount; ++index) {
        if (strcasecmp(trimmed, commandTable[index].name) == 0) {
            handler = commandTable[index].handler;
            break;
        }
    }
    portEXIT_CRITICAL(&commandTableMux);
    if (handler != nullptr) {
        handler(args);
        return;
    }
    Serial.printf("UNKNOWN CMD: %s\n", trimmed);
}

namespace serial_cmd_module {
bool init() {
    return xTaskCreate(serialCommandTask, "serial_cmd", SERIAL_TASK_STACK, nullptr, SERIAL_TASK_PRIORITY, nullptr) == pdPASS;
}

bool registerCommand(const char* name, CommandHandler handler) {
    if (name == nullptr || handler == nullptr || commandCount >= SERIAL_MAX_COMMANDS || strlen(name) >= SERIAL_COMMAND_NAME_LENGTH) {
        return false;
    }
    portENTER_CRITICAL(&commandTableMux);
    for (size_t index = 0; index < commandCount; ++index) {
        if (strcasecmp(commandTable[index].name, name) == 0) {
            commandTable[index].handler = handler;
            portEXIT_CRITICAL(&commandTableMux);
            return true;
        }
    }
    snprintf(commandTable[commandCount].name, sizeof(commandTable[commandCount].name), "%s", name);
    commandTable[commandCount].handler = handler;
    ++commandCount;
    portEXIT_CRITICAL(&commandTableMux);
    return true;
}

void serialCommandTask(void* parameter) {
    static_cast<void>(parameter);
    char line[SERIAL_LINE_BUFFER_SIZE]{};
    size_t lineLength = 0;
    bool discardLine = false;
    uint32_t lastStackReport = millis();
    for (;;) {
        boot_monitor_module::reportTaskStack("serial_cmd", lastStackReport);
        while (Serial.available() > 0) {
            char input = static_cast<char>(Serial.read());
            if (input == '\n') {
                if (!discardLine) {
                    line[lineLength] = '\0';
                    dispatchCommand(line);
                }
                lineLength = 0;
                discardLine = false;
            } else if (input != '\r' && !discardLine) {
                if (lineLength + 1 < sizeof(line)) {
                    line[lineLength++] = input;
                } else {
                    lineLength = 0;
                    discardLine = true;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(SERIAL_COMMAND_POLL_MS));
    }
}
}
