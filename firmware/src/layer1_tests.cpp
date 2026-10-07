#include "layer1_tests.h"

// Helper function to construct a valid SensorSample
static SensorSample createSample(uint32_t millisMs, float gasVolt, float powerMW, bool gasValid = true, bool powerValid = true) {
    SensorSample s;
    memset(&s, 0, sizeof(SensorSample));
    s.timestamp = 1750000000 + (millisMs / 1000);
    s.millisMs = millisMs;
    s.gasRaw = (uint16_t)((gasVolt / 3.3f) * 4095.0f);
    s.gasVoltage = gasVolt;
    s.sensorVoltage = gasVolt * 1.5f;
    s.busVoltage = 5.0f;
    s.current_mA = powerMW / 5.0f;
    s.power_mW = powerMW;
    s.equipmentActive = (powerMW >= POWER_ACTIVE_THRESHOLD_MW);
    s.gasValid = gasValid;
    s.powerValid = powerValid;
    s.timestampValid = true;
    return s;
}

// Helper function to construct a PowerSample
static PowerSample createPowerSample(uint32_t millisMs, float powerMW) {
    PowerSample p;
    p.timestamp = 1750000000 + (millisMs / 1000);
    p.millisMs = millisMs;
    p.power_mW = powerMW;
    p.equipmentActive = (powerMW >= POWER_ACTIVE_THRESHOLD_MW);
    return p;
}

bool Layer1TestSuite::test1_StableNormal() {
    Layer1PlausibilityEngine engine;
    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    // Fill 20 samples of stable baseline gas (1.0V) and 0 mW power
    for (uint32_t ms = 0; ms < 10000; ms += 500) {
        SensorSample s = createSample(ms, 1.0f, 0.0f);
        gasBuffer.add(s);
        powerBuffer.add(createPowerSample(ms, 0.0f));
    }

    SensorSample current = createSample(10500, 1.0f, 0.0f);
    Layer1Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return (res.status == PlausibilityStatus::NORMAL) && !res.gasEventDetected;
}

bool Layer1TestSuite::test2_PlausibleEvent() {
    Layer1PlausibilityEngine engine;
    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    // Baseline steps (0 - 10000 ms): Gas 1.0V, Power 0 mW
    for (uint32_t ms = 0; ms <= 10000; ms += 500) {
        gasBuffer.add(createSample(ms, 1.0f, 0.0f));
        powerBuffer.add(createPowerSample(ms, 0.0f));
    }

    // Power turns ON at t = 10500 ms (850 mW)
    for (uint32_t ms = 10500; ms <= 17500; ms += 500) {
        gasBuffer.add(createSample(ms, 1.0f, 850.0f));
        powerBuffer.add(createPowerSample(ms, 850.0f));
    }

    // Gas rises to 2.5V at t = 18000 ms (7.5s after power activation <= 30s)
    SensorSample current = createSample(18000, 2.5f, 850.0f);
    Layer1Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return (res.status == PlausibilityStatus::PLAUSIBLE) && res.gasEventDetected && res.powerActivityDetected && res.temporalCorrelation;
}

bool Layer1TestSuite::test3_SuspiciousGasEvent() {
    Layer1PlausibilityEngine engine;
    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    // Baseline steps: Gas 1.0V, Power 0 mW
    for (uint32_t ms = 0; ms <= 10000; ms += 500) {
        gasBuffer.add(createSample(ms, 1.0f, 0.0f));
        powerBuffer.add(createPowerSample(ms, 0.0f));
    }

    // Gas spikes to 2.5V at t = 10500 ms with equipment OFF (0 mW)
    SensorSample current = createSample(10500, 2.5f, 0.0f);
    Layer1Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return (res.status == PlausibilityStatus::SUSPICIOUS) && res.gasEventDetected && !res.powerActivityDetected && !res.temporalCorrelation;
}

bool Layer1TestSuite::test4_GasSensorInvalid() {
    Layer1PlausibilityEngine engine;
    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    for (uint32_t ms = 0; ms <= 5000; ms += 500) {
        gasBuffer.add(createSample(ms, 1.0f, 0.0f));
        powerBuffer.add(createPowerSample(ms, 0.0f));
    }

    // Current sample has gasValid = false
    SensorSample current = createSample(5500, 3.5f, 0.0f, false, true);
    Layer1Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return !res.gasSensorValid && !res.gasEventDetected && (res.status == PlausibilityStatus::NORMAL);
}

bool Layer1TestSuite::test5_PowerSensorInvalid() {
    Layer1PlausibilityEngine engine;
    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    for (uint32_t ms = 0; ms <= 5000; ms += 500) {
        gasBuffer.add(createSample(ms, 1.0f, 0.0f));
        powerBuffer.add(createPowerSample(ms, 0.0f));
    }

    // Current sample has powerValid = false
    SensorSample current = createSample(5500, 1.0f, 850.0f, true, false);
    Layer1Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return !res.powerSensorValid && !res.powerActivityDetected && (res.status == PlausibilityStatus::NORMAL);
}

bool Layer1TestSuite::test6_EmptyHistoryFallback() {
    Layer1PlausibilityEngine engine;
    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> emptyGasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> emptyPowerBuffer;

    SensorSample current = createSample(500, 1.5f, 0.0f);
    Layer1Result res = engine.evaluate(current, emptyGasBuffer, emptyPowerBuffer);

    // Fallback baseline = current voltage (1.5 * 1.5 = 2.25V), ratio = 1.0, status = NORMAL
    return (res.status == PlausibilityStatus::NORMAL) && !res.gasEventDetected && (fabsf(res.gasChangeRatio - 1.0f) < 0.01f);
}

bool Layer1TestSuite::test7_WindowBoundaryExact() {
    Layer1PlausibilityEngine engine;
    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    // Power event at t = 0 ms (850 mW)
    powerBuffer.add(createPowerSample(0, 850.0f));

    // Gas baseline samples (0 to 29500 ms)
    for (uint32_t ms = 0; ms < 30000; ms += 500) {
        gasBuffer.add(createSample(ms, 1.0f, 0.0f));
    }

    // Gas event at t = 30000 ms (exact response window boundary <= 30000 ms)
    SensorSample current = createSample(30000, 2.5f, 0.0f);
    Layer1Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return res.temporalCorrelation && (res.timeSincePowerActivityMs == 30000) && (res.status == PlausibilityStatus::PLAUSIBLE);
}

bool Layer1TestSuite::test8_WindowBoundaryOutside() {
    Layer1PlausibilityEngine engine;
    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    // Power event at t = 0 ms (850 mW)
    powerBuffer.add(createPowerSample(0, 850.0f));

    // Gas baseline samples (0 to 30000 ms)
    for (uint32_t ms = 0; ms <= 30000; ms += 500) {
        gasBuffer.add(createSample(ms, 1.0f, 0.0f));
    }

    // Gas event at t = 30001 ms (just outside response window > 30000 ms)
    SensorSample current = createSample(30001, 2.5f, 0.0f);
    Layer1Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return !res.temporalCorrelation && (res.timeSincePowerActivityMs == 30001) && (res.status == PlausibilityStatus::SUSPICIOUS);
}

bool Layer1TestSuite::test9_MultiplePowerEvents() {
    Layer1PlausibilityEngine engine;
    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    // Event 1: Power active at t = 0 ms
    powerBuffer.add(createPowerSample(0, 850.0f));

    // Event 2: Power active at t = 40000 ms
    for (uint32_t ms = 500; ms <= 40000; ms += 500) {
        float pwr = (ms == 40000) ? 850.0f : 0.0f;
        gasBuffer.add(createSample(ms, 1.0f, pwr));
        powerBuffer.add(createPowerSample(ms, pwr));
    }

    // Gas event at t = 45000 ms (5000 ms after Event 2)
    SensorSample current = createSample(45000, 2.5f, 0.0f);
    Layer1Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    // Should measure time since MOST RECENT power activity (5000 ms)
    return (res.timeSincePowerActivityMs == 5000) && res.temporalCorrelation && (res.status == PlausibilityStatus::PLAUSIBLE);
}

bool Layer1TestSuite::test10_LongGasElevation() {
    Layer1PlausibilityEngine engine;
    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    // Fill baseline 1.0V (0 to 10000 ms)
    for (uint32_t ms = 0; ms <= 10000; ms += 500) {
        gasBuffer.add(createSample(ms, 1.0f, 0.0f));
        powerBuffer.add(createPowerSample(ms, 0.0f));
    }

    // Equipment ON at t = 10500 ms
    powerBuffer.add(createPowerSample(10500, 850.0f));

    // Gas rises and remains elevated at 2.5V for 20 samples (11000 to 21000 ms)
    bool allPlausible = true;
    for (uint32_t ms = 11000; ms <= 21000; ms += 500) {
        SensorSample s = createSample(ms, 2.5f, 850.0f);
        Layer1Result res = engine.evaluate(s, gasBuffer, powerBuffer);
        if (res.status != PlausibilityStatus::PLAUSIBLE) {
            allPlausible = false;
        }
        gasBuffer.add(s);
        powerBuffer.add(createPowerSample(ms, 850.0f));
    }

    return allPlausible;
}

bool Layer1TestSuite::test11_MultipleGasSpikes() {
    Layer1PlausibilityEngine engine;
    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    // Spike 1: Equipment ON + Gas Spike -> PLAUSIBLE
    powerBuffer.add(createPowerSample(1000, 850.0f));
    for (uint32_t ms = 0; ms < 5000; ms += 500) {
        gasBuffer.add(createSample(ms, 1.0f, (ms == 1000) ? 850.0f : 0.0f));
    }

    SensorSample spike1 = createSample(5000, 2.5f, 0.0f);
    Layer1Result res1 = engine.evaluate(spike1, gasBuffer, powerBuffer);
    gasBuffer.add(spike1);

    // Return to baseline for 10000 ms
    for (uint32_t ms = 5500; ms <= 15000; ms += 500) {
        gasBuffer.add(createSample(ms, 1.0f, 0.0f));
        powerBuffer.add(createPowerSample(ms, 0.0f));
    }

    // Spike 2: Equipment OFF + Gas Spike -> SUSPICIOUS
    SensorSample spike2 = createSample(15500, 2.5f, 0.0f);
    Layer1Result res2 = engine.evaluate(spike2, gasBuffer, powerBuffer);

    return (res1.status == PlausibilityStatus::PLAUSIBLE) && (res2.status == PlausibilityStatus::SUSPICIOUS);
}

bool Layer1TestSuite::test12_BufferBoundaries() {
    Layer1PlausibilityEngine engine;
    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    // Fill gasBuffer to 2N capacity (240 samples > 120 capacity)
    for (uint32_t i = 0; i < 240; i++) {
        uint32_t ms = i * 500;
        gasBuffer.add(createSample(ms, 1.0f, 0.0f));
        powerBuffer.add(createPowerSample(ms, 0.0f));
    }

    // Buffer count must cap at 120 and 180 respectively
    bool countCorrect = (gasBuffer.count() == 120) && (powerBuffer.count() == 180);

    // Evaluate current sample
    SensorSample current = createSample(120000, 1.0f, 0.0f);
    Layer1Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return countCorrect && (res.status == PlausibilityStatus::NORMAL);
}

bool Layer1TestSuite::runAllTests() {
    Serial.println("\n==========================================");
    Serial.println("  TrueSense Layer 1 Verification Suite");
    Serial.println("==========================================");

    bool p1  = test1_StableNormal();        Serial.printf("[%s] TEST 1: Stable Normal Scenario\n", p1 ? "PASS" : "FAIL");
    bool p2  = test2_PlausibleEvent();     Serial.printf("[%s] TEST 2: Plausible Event Scenario\n", p2 ? "PASS" : "FAIL");
    bool p3  = test3_SuspiciousGasEvent(); Serial.printf("[%s] TEST 3: Suspicious Event Scenario\n", p3 ? "PASS" : "FAIL");
    bool p4  = test4_GasSensorInvalid();   Serial.printf("[%s] TEST 4: Gas Sensor Invalid Handling\n", p4 ? "PASS" : "FAIL");
    bool p5  = test5_PowerSensorInvalid(); Serial.printf("[%s] TEST 5: Power Sensor Invalid Handling\n", p5 ? "PASS" : "FAIL");
    bool p6  = test6_EmptyHistoryFallback();Serial.printf("[%s] TEST 6: Empty History Baseline Fallback\n", p6 ? "PASS" : "FAIL");
    bool p7  = test7_WindowBoundaryExact();Serial.printf("[%s] TEST 7: Temporal Window Boundary (Exact <= 30s)\n", p7 ? "PASS" : "FAIL");
    bool p8  = test8_WindowBoundaryOutside();Serial.printf("[%s] TEST 8: Temporal Window Boundary (Outside > 30s)\n", p8 ? "PASS" : "FAIL");
    bool p9  = test9_MultiplePowerEvents();Serial.printf("[%s] TEST 9: Multiple Power Events (Most Recent Search)\n", p9 ? "PASS" : "FAIL");
    bool p10 = test10_LongGasElevation();  Serial.printf("[%s] TEST 10: Sustained Gas Elevation Behavior\n", p10 ? "PASS" : "FAIL");
    bool p11 = test11_MultipleGasSpikes(); Serial.printf("[%s] TEST 11: Multiple Gas Spikes State Consistency\n", p11 ? "PASS" : "FAIL");
    bool p12 = test12_BufferBoundaries();   Serial.printf("[%s] TEST 12: Ring Buffer Overwrite Boundaries\n", p12 ? "PASS" : "FAIL");

    bool overall = p1 && p2 && p3 && p4 && p5 && p6 && p7 && p8 && p9 && p10 && p11 && p12;
    Serial.println("==========================================");
    Serial.printf("Layer 1 Deterministic Suite Result: %s\n", overall ? "ALL 12 TESTS PASSED" : "TEST FAILURE DETECTED");
    Serial.println("==========================================\n");

    return overall;
}
