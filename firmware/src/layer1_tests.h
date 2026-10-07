#ifndef LAYER1_TESTS_H
#define LAYER1_TESTS_H

#include <Arduino.h>
#include "layer1_plausibility.h"

// Independent Layer 1 Deterministic Software Verification Test Suite
class Layer1TestSuite {
public:
    static bool runAllTests();

    // Required Individual Test Cases
    static bool test1_StableNormal();
    static bool test2_PlausibleEvent();
    static bool test3_SuspiciousGasEvent();
    static bool test4_GasSensorInvalid();
    static bool test5_PowerSensorInvalid();
    static bool test6_EmptyHistoryFallback();
    static bool test7_WindowBoundaryExact();
    static bool test8_WindowBoundaryOutside();
    static bool test9_MultiplePowerEvents();
    static bool test10_LongGasElevation();
    static bool test11_MultipleGasSpikes();
    static bool test12_BufferBoundaries();
};

#endif // LAYER1_TESTS_H
