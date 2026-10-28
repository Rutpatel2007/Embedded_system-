#ifndef PROOV_SENSOR_TASK_H
#define PROOV_SENSOR_TASK_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "config.h"

struct GasState {
    uint32_t r0Ohms;
    uint32_t baselineWindow[BASELINE_WINDOW];
    uint64_t baselineSum;
    uint32_t baselinePpmX100;
    uint32_t windowCount;
    uint32_t windowIndex;
    uint32_t rawMilliVolts;
    uint32_t rsOhms;
    bool calibrated;
};

struct ProovRecord {
    uint32_t sequence;
    int32_t gasMilliUnits;
    int32_t powerMilliWatts;
    uint32_t timestamp;
    int32_t currentMilliAmps;
    uint8_t equipmentActive;
    uint8_t rtcLostPower;
    int32_t plausibilityFlag;
    int32_t fingerprintStatus;
};

namespace sensor_task_module {
void init();
uint32_t readRawADC();
bool calibrateBaseline(GasState& state);
uint32_t convertToPPM(uint32_t rsOhms, uint32_t r0Ohms);
void updateBaseline(GasState& state, uint32_t gasPpmX100);
uint32_t getBaseline_x100(const GasState& state);
uint32_t getQueueDropCount();
void registerSensorCommands();
void startSensorTask(QueueHandle_t outputQueue);
void sensorTask(void* parameter);
}

#endif
