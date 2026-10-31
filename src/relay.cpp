#include "relay.h"
#include "config.h"
#include "serial_cmd.h"
#include "boot_monitor.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <strings.h>

static QueueHandle_t relayRequestQueue;
static bool relayState;
static uint32_t relayDropCount;
static portMUX_TYPE relayMux = portMUX_INITIALIZER_UNLOCKED;

static void handleRelayCommand(const char* args) {
    bool enabled;
    if (strcasecmp(args, "ON") == 0) {
        enabled = true;
    } else if (strcasecmp(args, "OFF") == 0) {
        enabled = false;
    } else {
        Serial.println("Usage: RELAY ON|OFF");
        return;
    }
    if (!relay_module::requestRelay(enabled)) {
        portENTER_CRITICAL(&relayMux);
        ++relayDropCount;
        portEXIT_CRITICAL(&relayMux);
        Serial.println(SERIAL_HANDLER_QUEUE_FULL);
    }
}

namespace relay_module {
bool init() {
    pinMode(RELAY_PIN, OUTPUT);
    relayState = false;
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
    relayRequestQueue = xQueueCreate(RELAY_REQUEST_QUEUE_LENGTH, sizeof(bool));
    if (relayRequestQueue == nullptr) {
        return false;
    }
    return relayRequestQueue != nullptr;
}

void setRelay(bool enabled) {
    relayState = enabled;
    bool outputHigh = RELAY_ACTIVE_LOW ? !enabled : enabled;
    digitalWrite(RELAY_PIN, outputHigh ? HIGH : LOW);
}

bool requestRelay(bool enabled) {
    if (relayRequestQueue != nullptr && xQueueSend(relayRequestQueue, &enabled, 0) == pdPASS) return true;
    portENTER_CRITICAL(&relayMux);
    ++relayDropCount;
    portEXIT_CRITICAL(&relayMux);
    return false;
}

bool getRelayState() {
    return relayState;
}

uint32_t getDropCount() {
    portENTER_CRITICAL(&relayMux);
    uint32_t drops = relayDropCount;
    portEXIT_CRITICAL(&relayMux);
    return drops;
}

void startRelayTask() { if (relayRequestQueue != nullptr) xTaskCreate(relayTask, "relay_task", RELAY_TASK_STACK, nullptr, RELAY_TASK_PRIORITY, nullptr); }

void registerRelayCommands() {
    serial_cmd_module::registerCommand("RELAY", handleRelayCommand);
}

void relayTask(void* parameter) {
    static_cast<void>(parameter);
    uint32_t lastStackReport = millis();
    bool requestedState;
    for (;;) {
        boot_monitor_module::reportTaskStack("relay", lastStackReport);
        if (xQueueReceive(relayRequestQueue, &requestedState, pdMS_TO_TICKS(500)) == pdTRUE) {
            setRelay(requestedState);
            Serial.printf("RELAY %s\n", requestedState ? "ON" : "OFF");
        }
    }
}
}
