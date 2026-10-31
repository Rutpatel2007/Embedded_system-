#ifndef TRUESENSE_TEST_HARNESS_H
#define TRUESENSE_TEST_HARNESS_H

#ifdef TEST_MODE
#include <Arduino.h>
#include "config.h"

struct TestScenarioResult {
    uint8_t complete;
    uint8_t passed;
    uint32_t elapsedMs;
    char detail[TEST_RESULT_DETAIL_LENGTH];
};

namespace test_harness_module {
bool init();
bool startScenario(uint8_t scenario);
bool runAll();
void getResults();
void registerCommands();
void startTask();
void task(void* parameter);
}
#endif

#endif
