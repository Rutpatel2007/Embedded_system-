#ifndef TRUESENSE_FINGERPRINT_H
#define TRUESENSE_FINGERPRINT_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "config.h"

struct FingerprintSignature {
    uint32_t shape[FP_POINTS];
    uint32_t levelMilliVolts;
};

namespace fingerprint_module {
void init();
bool measureSignature(FingerprintSignature& signature);
uint8_t getStatus();
uint64_t getDistance(bool& available);
uint32_t getDropCount();
#ifdef TEST_MODE
bool isEnrolled();
#endif
QueueHandle_t getStatusQueue();
void registerFingerprintCommands();
void startFingerprintTask();
void fingerprintTask(void* parameter);
}

#endif
