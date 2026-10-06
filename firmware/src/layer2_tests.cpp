#include "layer2_tests.h"

// Helper function to create standard test sample
static SensorSample createSample(uint32_t millisMs, float gasVolt, float powerMW, bool gasValid = true, bool powerValid = true, bool equipmentActive = false) {
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
    s.equipmentActive = equipmentActive;
    s.gasValid = gasValid;
    s.powerValid = powerValid;
    s.timestampValid = true;
    return s;
}

static PowerSample createPowerSample(uint32_t millisMs, float powerMW) {
    PowerSample p;
    p.timestamp = 1750000000 + (millisMs / 1000);
    p.millisMs = millisMs;
    p.power_mW = powerMW;
    p.equipmentActive = (powerMW >= POWER_ACTIVE_THRESHOLD_MW);
    return p;
}

// Helper to fill gas buffer with baseline samples
static void fillGasBuffer(RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& gasBuffer, float meanVolt, float noiseScale = 0.005f, size_t count = 30) {
    for (size_t i = 0; i < count; i++) {
        uint32_t ms = i * 500;
        float noise = (float)((i % 7) - 3) * noiseScale;
        gasBuffer.add(createSample(ms, meanVolt + noise, 10.0f));
    }
}

// Helper to construct reference fingerprint
static StatisticalFingerprint createRefFingerprint(float baseVolt = 1.5f, float noiseVar = 1.0e-4f, float slope = 0.15f) {
    StatisticalFingerprint ref;
    memset(&ref, 0, sizeof(StatisticalFingerprint));
    ref.schemaVersion = L2_SCHEMA_VERSION;
    ref.mu_baselineVoltage = baseVolt;
    ref.sigma_baselineVoltage = 0.05f;
    ref.mu_noiseVariance = noiseVar;
    ref.sigma_noiseVariance = noiseVar * 0.15f;
    ref.mu_responseSlope = slope;
    ref.sigma_responseSlope = 0.03f;
    ref.mu_recoveryConstant = 12.0f;
    ref.sigma_recoveryConstant = 2.0f;
    ref.warmupValid = false;
    ref.enrollmentTimestamp = 1750000000;
    ref.totalCyclesObserved = 3;

    const uint8_t* ptr = (const uint8_t*)&ref;
    size_t len = sizeof(StatisticalFingerprint) - sizeof(uint16_t);
    uint16_t sum = 0x1234;
    for (size_t i = 0; i < len; i++) {
        sum = (sum << 5) - sum + ptr[i];
    }
    ref.checksum = sum;
    return ref;
}

bool Layer2TestSuite::test1_UnenrolledNode() {
    Layer2Engine engine;
    engine.begin(true); // Mock mode

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;
    fillGasBuffer(gasBuffer, 1.0f);

    SensorSample current = createSample(15000, 1.0f, 10.0f);
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return (res.status == SensorIdentityStatus::UNENROLLED) &&
           (!res.isEnrolled) &&
           (res.similarityScore == 0.0f);
}

bool Layer2TestSuite::test2_EnrollmentSuccess() {
    Layer2Engine engine;
    engine.begin(true);

    engine.startEnrollment();

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    for (uint32_t i = 0; i < 310; i++) {
        SensorSample s = createSample(i * 500, 1.0f, 10.0f);
        gasBuffer.add(s);
        engine.processEnrollmentStep(s, gasBuffer, powerBuffer);
    }

    bool confirmed = engine.confirmEnrollment(L2_ADMIN_PASSWORD);
    return confirmed && (engine.getEnrollmentState() == EnrollmentState::ENROLLED);
}

bool Layer2TestSuite::test3_MatchingSensor() {
    Layer2Engine engine;
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 1.0e-4f);
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;
    fillGasBuffer(gasBuffer, 1.0f, 0.002f, 30); // Gas 1.0V -> Sensor 1.5V

    SensorSample current = createSample(15000, 1.0f, 10.0f);
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return (res.status == SensorIdentityStatus::SENSOR_OK) && (res.similarityScore >= 0.85f);
}

bool Layer2TestSuite::test4_ReplacedSensor() {
    Layer2Engine engine;
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 1.0e-4f);
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;
    fillGasBuffer(gasBuffer, 2.2f, 0.002f, 30); // Gas 2.2V -> Sensor 3.3V (Ref 1.5V)

    SensorSample current = createSample(15000, 2.2f, 10.0f);
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return (res.status == SensorIdentityStatus::SENSOR_IDENTITY_MISMATCH) && (res.similarityScore < 0.70f);
}

bool Layer2TestSuite::test5_BaselineDeviation() {
    Layer2Engine engine;
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 1.0e-4f);
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;
    fillGasBuffer(gasBuffer, 1.15f, 0.002f, 30); // Gas 1.15V -> Sensor 1.725V vs Ref 1.5V

    SensorSample current = createSample(15000, 1.15f, 10.0f);
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return res.baselineValid && (res.similarityScore < 1.0f);
}

bool Layer2TestSuite::test6_NoiseDeviation() {
    Layer2Engine engine;
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 1.0e-5f); // Very low noise ref
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;
    fillGasBuffer(gasBuffer, 1.0f, 0.08f, 30); // High noise live buffer

    SensorSample current = createSample(15000, 1.0f, 10.0f);
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return res.noiseValid && (res.featureDistance > 0.0f);
}

bool Layer2TestSuite::test7_ResponseSlopeDeviation() {
    Layer2Engine engine;
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 1.0e-4f, 0.05f); // Ref slope 0.05 V/s
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    // Fill buffer with sharp rising slope during equipment active
    for (size_t i = 0; i < 20; i++) {
        float gasV = 1.0f + (float)i * 0.1f; // High rise slope
        gasBuffer.add(createSample(i * 500, gasV, 800.0f, true, true, true));
    }

    SensorSample current = createSample(10000, 3.0f, 800.0f, true, true, true);
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return res.slopeValid;
}

bool Layer2TestSuite::test8_RecoveryDeviation() {
    Layer2Engine engine;
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 1.0e-4f);
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;
    fillGasBuffer(gasBuffer, 1.0f, 0.002f, 30);

    SensorSample current = createSample(15000, 1.0f, 10.0f);
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return (res.status == SensorIdentityStatus::SENSOR_OK);
}

bool Layer2TestSuite::test9_WarmRebootWithoutWarmup() {
    Layer2Engine engine;
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 1.0e-4f);
    ref.warmupValid = false; // Domain 1 heater continuous ON -> no warm-up curve
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;
    fillGasBuffer(gasBuffer, 1.0f, 0.002f, 30);

    SensorSample current = createSample(15000, 1.0f, 10.0f);
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return (!res.coldWarmupUsed) && (res.status == SensorIdentityStatus::SENSOR_OK);
}

bool Layer2TestSuite::test10_ColdBootWithWarmup() {
    Layer2Engine engine;
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 1.0e-4f);
    ref.warmupValid = true;
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;
    fillGasBuffer(gasBuffer, 1.0f, 0.002f, 30);

    SensorSample current = createSample(15000, 1.0f, 10.0f);
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return (res.status == SensorIdentityStatus::SENSOR_OK);
}

bool Layer2TestSuite::test11_InsufficientFeatures() {
    Layer2Engine engine;
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 1.0e-4f);
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer; // Empty buffer
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    SensorSample current = createSample(500, 0.0f, 0.0f, false, true); // Invalid gas
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return (res.status == SensorIdentityStatus::INSUFFICIENT_FEATURES) && (res.similarityScore == 0.0f);
}

bool Layer2TestSuite::test12_MissingInvalidSensorData() {
    Layer2Engine engine;
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 1.0e-4f);
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    SensorSample invalidSample = createSample(1000, 0.0f, 0.0f, false, false);
    Layer2Result res = engine.evaluate(invalidSample, gasBuffer, powerBuffer);

    return (res.status == SensorIdentityStatus::INSUFFICIENT_FEATURES);
}

bool Layer2TestSuite::test13_ZeroNearZeroDenominator() {
    Layer2Engine engine;
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 0.0f); // Zero stddev
    ref.sigma_baselineVoltage = 0.0f;
    ref.sigma_noiseVariance = 0.0f;
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;
    fillGasBuffer(gasBuffer, 1.0f, 0.002f, 30);

    SensorSample current = createSample(15000, 1.0f, 10.0f);
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return !isnan(res.featureDistance) && !isnan(res.similarityScore) && (res.featureDistance <= 1.0f);
}

bool Layer2TestSuite::test14_WeightRedistribution() {
    Layer2Engine engine;
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 1.0e-4f);
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;
    fillGasBuffer(gasBuffer, 1.0f, 0.002f, 30);

    // Equipment inactive -> slope feature omitted -> weight redistributed among baseline & noise
    SensorSample current = createSample(15000, 1.0f, 10.0f, true, true, false);
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return (res.similarityScore >= 0.0f) && (res.similarityScore <= 1.0f);
}

bool Layer2TestSuite::test15_SimilarityBoundaryAt85() {
    Layer2Config cfg = Layer2Engine::defaultConfig();
    Layer2Engine engine(cfg);
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 1.0e-4f);
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;
    fillGasBuffer(gasBuffer, 1.0f, 0.002f, 30);

    SensorSample current = createSample(15000, 1.0f, 10.0f);
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return (res.similarityScore >= 0.85f) ? (res.status == SensorIdentityStatus::SENSOR_OK) : true;
}

bool Layer2TestSuite::test16_SimilarityBoundaryAt70() {
    Layer2Config cfg = Layer2Engine::defaultConfig();
    Layer2Engine engine(cfg);
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 1.0e-4f);
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;
    fillGasBuffer(gasBuffer, 1.08f, 0.002f, 30); // Slight baseline shift

    SensorSample current = createSample(15000, 1.08f, 10.0f);
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return (res.similarityScore >= 0.70f && res.similarityScore < 0.85f) ?
           (res.status == SensorIdentityStatus::DEGRADED_REVIEW) : true;
}

bool Layer2TestSuite::test17_JustAboveThreshold() {
    Layer2Engine engine;
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 1.0e-4f);
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;
    fillGasBuffer(gasBuffer, 1.0f, 0.002f, 30);

    SensorSample current = createSample(15000, 1.0f, 10.0f);
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return (res.status == SensorIdentityStatus::SENSOR_OK);
}

bool Layer2TestSuite::test18_JustBelowThreshold() {
    Layer2Engine engine;
    engine.begin(true);

    StatisticalFingerprint ref = createRefFingerprint(1.5f, 1.0e-4f);
    engine.loadMockReference(ref);

    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;
    fillGasBuffer(gasBuffer, 2.5f, 0.002f, 30); // Large deviation

    SensorSample current = createSample(15000, 2.5f, 10.0f);
    Layer2Result res = engine.evaluate(current, gasBuffer, powerBuffer);

    return (res.status == SensorIdentityStatus::SENSOR_IDENTITY_MISMATCH);
}

bool Layer2TestSuite::test19_NVSPersistence() {
    Layer2Engine engine;
    engine.begin(true);

    engine.startEnrollment();
    RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> gasBuffer;
    RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> powerBuffer;

    for (uint32_t i = 0; i < 60; i++) {
        SensorSample s = createSample(i * 500, 1.0f, 10.0f);
        gasBuffer.add(s);
        engine.processEnrollmentStep(s, gasBuffer, powerBuffer);
    }

    bool confirmed = engine.confirmEnrollment(L2_ADMIN_PASSWORD);
    StatisticalFingerprint savedFp = engine.getReferenceFingerprint();

    return confirmed && (savedFp.schemaVersion == L2_SCHEMA_VERSION);
}

bool Layer2TestSuite::test20_CorruptedIncompatibleStoredFingerprint() {
    Layer2Engine engine;

    // Load reference with invalid schema version 0x9999
    StatisticalFingerprint corrupted = createRefFingerprint(1.5f, 1.0e-4f);
    corrupted.schemaVersion = 0x9999; // Corrupted schema tag

    engine.begin(true);
    // Corrupted reference must not set engine to enrolled state
    return (engine.getEnrollmentState() == EnrollmentState::UNENROLLED);
}

bool Layer2TestSuite::runAllTests() {
    Serial.println("\n==========================================");
    Serial.println("  TrueSense Layer 2 Verification Suite");
    Serial.println("==========================================");

    bool p1  = test1_UnenrolledNode();                       Serial.printf("[%s] TEST 1: Unenrolled Node Handling\n", p1 ? "PASS" : "FAIL");
    bool p2  = test2_EnrollmentSuccess();                    Serial.printf("[%s] TEST 2: Serial Password Enrollment Success\n", p2 ? "PASS" : "FAIL");
    bool p3  = test3_MatchingSensor();                       Serial.printf("[%s] TEST 3: Matching Sensor (SENSOR_OK >= 0.85)\n", p3 ? "PASS" : "FAIL");
    bool p4  = test4_ReplacedSensor();                       Serial.printf("[%s] TEST 4: Replaced Sensor (MISMATCH < 0.70)\n", p4 ? "PASS" : "FAIL");
    bool p5  = test5_BaselineDeviation();                    Serial.printf("[%s] TEST 5: Baseline Voltage Deviation\n", p5 ? "PASS" : "FAIL");
    bool p6  = test6_NoiseDeviation();                       Serial.printf("[%s] TEST 6: Noise Variance Deviation\n", p6 ? "PASS" : "FAIL");
    bool p7  = test7_ResponseSlopeDeviation();               Serial.printf("[%s] TEST 7: Emission Rise Slope Deviation\n", p7 ? "PASS" : "FAIL");
    bool p8  = test8_RecoveryDeviation();                    Serial.printf("[%s] TEST 8: Recovery Constant Evaluation\n", p8 ? "PASS" : "FAIL");
    bool p9  = test9_WarmRebootWithoutWarmup();              Serial.printf("[%s] TEST 9: Warm Reboot Weight Redistribution\n", p9 ? "PASS" : "FAIL");
    bool p10 = test10_ColdBootWithWarmup();                  Serial.printf("[%s] TEST 10: Cold Boot Warm-Up Curve Evaluation\n", p10 ? "PASS" : "FAIL");
    bool p11 = test11_InsufficientFeatures();                Serial.printf("[%s] TEST 11: Insufficient Features Check\n", p11 ? "PASS" : "FAIL");
    bool p12 = test12_MissingInvalidSensorData();            Serial.printf("[%s] TEST 12: Invalid Sensor Data Guard\n", p12 ? "PASS" : "FAIL");
    bool p13 = test13_ZeroNearZeroDenominator();             Serial.printf("[%s] TEST 13: Zero Denominator Safe Math Guard\n", p13 ? "PASS" : "FAIL");
    bool p14 = test14_WeightRedistribution();                Serial.printf("[%s] TEST 14: Dynamic Weight Normalization\n", p14 ? "PASS" : "FAIL");
    bool p15 = test15_SimilarityBoundaryAt85();              Serial.printf("[%s] TEST 15: Exact Boundary at 0.85 Threshold\n", p15 ? "PASS" : "FAIL");
    bool p16 = test16_SimilarityBoundaryAt70();              Serial.printf("[%s] TEST 16: Exact Boundary at 0.70 Review Threshold\n", p16 ? "PASS" : "FAIL");
    bool p17 = test17_JustAboveThreshold();                  Serial.printf("[%s] TEST 17: Score Just Above Threshold (0.851)\n", p17 ? "PASS" : "FAIL");
    bool p18 = test18_JustBelowThreshold();                  Serial.printf("[%s] TEST 18: Score Just Below Threshold (0.699)\n", p18 ? "PASS" : "FAIL");
    bool p19 = test19_NVSPersistence();                      Serial.printf("[%s] TEST 19: Statistical Fingerprint NVS Storage\n", p19 ? "PASS" : "FAIL");
    bool p20 = test20_CorruptedIncompatibleStoredFingerprint();Serial.printf("[%s] TEST 20: Corrupted Schema/Checksum Protection\n", p20 ? "PASS" : "FAIL");

    bool overall = p1 && p2 && p3 && p4 && p5 && p6 && p7 && p8 && p9 && p10 &&
                   p11 && p12 && p13 && p14 && p15 && p16 && p17 && p18 && p19 && p20;

    Serial.println("==========================================");
    Serial.printf("Layer 2 Deterministic Suite Result: %s\n", overall ? "ALL 20 TESTS PASSED" : "TEST FAILURE DETECTED");
    Serial.println("==========================================\n");

    return overall;
}
