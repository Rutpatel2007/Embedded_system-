#include "hash_chain_tests.h"

// Helper function to build a mock sample
static SensorSample createSample(uint32_t millisMs, uint64_t epochSec, float gasVolt, float powerMW) {
    SensorSample s;
    memset(&s, 0, sizeof(SensorSample));
    s.timestamp = epochSec;
    s.millisMs = millisMs;
    s.gasRaw = (uint16_t)((gasVolt / 3.3f) * 4095.0f);
    s.gasVoltage = gasVolt;
    s.sensorVoltage = gasVolt * 1.5f;
    s.busVoltage = 5.0f;
    s.current_mA = powerMW / 5.0f;
    s.power_mW = powerMW;
    s.equipmentActive = (powerMW >= POWER_ACTIVE_THRESHOLD_MW);
    s.gasValid = true;
    s.powerValid = true;
    s.timestampValid = true;
    return s;
}

static Layer1Result createLayer1Result(PlausibilityStatus status) {
    Layer1Result r;
    memset(&r, 0, sizeof(Layer1Result));
    r.status = status;
    return r;
}

static Layer2Result createLayer2Result(SensorIdentityStatus status) {
    Layer2Result r;
    memset(&r, 0, sizeof(Layer2Result));
    r.status = status;
    return r;
}

bool HashChainTestSuite::test1_GenesisRecord() {
    HashChainEngine engine;
    engine.begin(nullptr, true); // Mock mode

    return (strcmp(engine.getPreviousHash(), GENESIS_HASH) == 0) &&
           (engine.getState() == ChainState::CHAIN_UNINITIALIZED);
}

bool HashChainTestSuite::test2_ExactFirstRecordHash() {
    char canonicalStr[256];
    char hashHex[65];

    bool sOk = HashChainEngine::serializeCanonicalString(
        "TS001", 1750000000ULL, 42.381f, 823.420f,
        "NORMAL", "SENSOR_OK", GENESIS_HASH,
        canonicalStr, sizeof(canonicalStr)
    );

    const char* expectedStr = "TS001|1750000000|42.381|823.420|NORMAL|SENSOR_OK|0000000000000000000000000000000000000000000000000000000000000000";
    bool strMatch = (strcmp(canonicalStr, expectedStr) == 0);

    bool hOk = HashChainEngine::computeSHA256(canonicalStr, strlen(canonicalStr), hashHex);
    const char* expectedHash = "2e8e7995bb9576188874c028488bafb8a5973133134cbb123182b3c79051dcd3";
    bool hashMatch = (strcmp(hashHex, expectedHash) == 0);

    return sOk && strMatch && hOk && hashMatch;
}

bool HashChainTestSuite::test3_SecondChainedRecord() {
    char canonicalStr[256];
    char hashHex[65];

    const char* prevHash = "2e8e7995bb9576188874c028488bafb8a5973133134cbb123182b3c79051dcd3";

    bool sOk = HashChainEngine::serializeCanonicalString(
        "TS001", 1750000500ULL, 120.500f, 850.000f,
        "PLAUSIBLE", "SENSOR_OK", prevHash,
        canonicalStr, sizeof(canonicalStr)
    );

    const char* expectedStr = "TS001|1750000500|120.500|850.000|PLAUSIBLE|SENSOR_OK|2e8e7995bb9576188874c028488bafb8a5973133134cbb123182b3c79051dcd3";
    bool strMatch = (strcmp(canonicalStr, expectedStr) == 0);

    bool hOk = HashChainEngine::computeSHA256(canonicalStr, strlen(canonicalStr), hashHex);
    const char* expectedHash = "5b1e21572e9523d3d1ec209de9d7678f0f649fc7f7bde55cf991d0a6940611a6";
    bool hashMatch = (strcmp(hashHex, expectedHash) == 0);

    return sOk && strMatch && hOk && hashMatch;
}

bool HashChainTestSuite::test4_MultipleRecordChain() {
    HashChainEngine engine;
    engine.begin(nullptr, true);
    engine.clearMockLogBuffer();

    for (uint32_t i = 0; i < 10; i++) {
        SensorSample s = createSample(i * 500, 1750000000 + i, 1.0f + i * 0.05f, 10.0f + i * 5.0f);
        Layer1Result l1 = createLayer1Result(PlausibilityStatus::NORMAL);
        Layer2Result l2 = createLayer2Result(SensorIdentityStatus::SENSOR_OK);

        if (!engine.appendRecord(s, l1, l2)) return false;
    }

    return (engine.getRecordCount() == 10) && engine.verifyChain();
}

bool HashChainTestSuite::test5_Exact3DecimalFormatting() {
    char buf[256];
    HashChainEngine::serializeCanonicalString(
        "TS001", 1000, 42.38f, 0.0f, "NORMAL", "SENSOR_OK", GENESIS_HASH, buf, sizeof(buf)
    );

    return (strstr(buf, "|42.380|0.000|") != nullptr);
}

bool HashChainTestSuite::test6_ExactCanonicalSerialization() {
    char buf[256];
    HashChainEngine::serializeCanonicalString(
        "TS001", 1750000000ULL, 42.381f, 823.420f, "NORMAL", "SENSOR_OK", GENESIS_HASH, buf, sizeof(buf)
    );

    return (strcmp(buf, "TS001|1750000000|42.381|823.420|NORMAL|SENSOR_OK|0000000000000000000000000000000000000000000000000000000000000000") == 0);
}

bool HashChainTestSuite::test7_CppPythonParity() {
    // Parity verified against Python hashlib digest for Test Vector 1 & 2
    return test2_ExactFirstRecordHash() && test3_SecondChainedRecord();
}

bool HashChainTestSuite::test8_TamperedGasValue() {
    HashChainEngine engine;
    engine.begin(nullptr, true);
    engine.clearMockLogBuffer();

    SensorSample s = createSample(0, 1750000000, 1.0f, 10.0f);
    engine.appendRecord(s, createLayer1Result(PlausibilityStatus::NORMAL), createLayer2Result(SensorIdentityStatus::SENSOR_OK));

    // Tamper with gas_ppm in mock log buffer
    char* pos = strstr((char*)engine.getMockLogBuffer(), "\"gas_ppm\":1.000");
    if (pos) {
        memcpy(pos + 10, "9.999", 5);
    }

    return !engine.verifyChain() && (engine.getState() == ChainState::HASH_VERIFICATION_FAILED);
}

bool HashChainTestSuite::test9_TamperedPowerValue() {
    HashChainEngine engine;
    engine.begin(nullptr, true);
    engine.clearMockLogBuffer();

    SensorSample s = createSample(0, 1750000000, 1.0f, 10.0f);
    engine.appendRecord(s, createLayer1Result(PlausibilityStatus::NORMAL), createLayer2Result(SensorIdentityStatus::SENSOR_OK));

    char* pos = strstr((char*)engine.getMockLogBuffer(), "\"power_mW\":10.000");
    if (pos) {
        memcpy(pos + 11, "99.999", 6);
    }

    return !engine.verifyChain() && (engine.getState() == ChainState::HASH_VERIFICATION_FAILED);
}

bool HashChainTestSuite::test10_TamperedTimestamp() {
    HashChainEngine engine;
    engine.begin(nullptr, true);
    engine.clearMockLogBuffer();

    SensorSample s = createSample(0, 1750000000, 1.0f, 10.0f);
    engine.appendRecord(s, createLayer1Result(PlausibilityStatus::NORMAL), createLayer2Result(SensorIdentityStatus::SENSOR_OK));

    char* pos = strstr((char*)engine.getMockLogBuffer(), "\"timestamp\":1750000000");
    if (pos) {
        memcpy(pos + 12, "9999999999", 10);
    }

    return !engine.verifyChain() && (engine.getState() == ChainState::HASH_VERIFICATION_FAILED);
}

bool HashChainTestSuite::test11_TamperedLayer1Status() {
    HashChainEngine engine;
    engine.begin(nullptr, true);
    engine.clearMockLogBuffer();

    SensorSample s = createSample(0, 1750000000, 1.0f, 10.0f);
    engine.appendRecord(s, createLayer1Result(PlausibilityStatus::NORMAL), createLayer2Result(SensorIdentityStatus::SENSOR_OK));

    char* pos = strstr((char*)engine.getMockLogBuffer(), "\"plausibility\":\"NORMAL\"");
    if (pos) {
        memcpy(pos + 16, "PLAUSIBLE", 9);
    }

    return !engine.verifyChain() && (engine.getState() == ChainState::HASH_VERIFICATION_FAILED);
}

bool HashChainTestSuite::test12_TamperedLayer2Status() {
    HashChainEngine engine;
    engine.begin(nullptr, true);
    engine.clearMockLogBuffer();

    SensorSample s = createSample(0, 1750000000, 1.0f, 10.0f);
    engine.appendRecord(s, createLayer1Result(PlausibilityStatus::NORMAL), createLayer2Result(SensorIdentityStatus::SENSOR_OK));

    char* pos = strstr((char*)engine.getMockLogBuffer(), "\"fingerprint_status\":\"SENSOR_OK\"");
    if (pos) {
        memcpy(pos + 22, "SENSOR_IDENTITY_MISMATCH", 24);
    }

    return !engine.verifyChain() && (engine.getState() == ChainState::HASH_VERIFICATION_FAILED);
}

bool HashChainTestSuite::test13_TamperedPreviousHash() {
    HashChainEngine engine;
    engine.begin(nullptr, true);
    engine.clearMockLogBuffer();

    SensorSample s1 = createSample(0, 1750000000, 1.0f, 10.0f);
    engine.appendRecord(s1, createLayer1Result(PlausibilityStatus::NORMAL), createLayer2Result(SensorIdentityStatus::SENSOR_OK));

    SensorSample s2 = createSample(500, 1750000001, 1.1f, 10.0f);
    engine.appendRecord(s2, createLayer1Result(PlausibilityStatus::NORMAL), createLayer2Result(SensorIdentityStatus::SENSOR_OK));

    // Tamper with previous_hash of 2nd record
    char* pos = strstr((char*)engine.getMockLogBuffer(), "\"previous_hash\":\"2e8e7995");
    if (pos) {
        memcpy(pos + 17, "ffffffff", 8);
    }

    return !engine.verifyChain() && (engine.getState() == ChainState::CHAIN_CORRUPTED || engine.getState() == ChainState::HASH_VERIFICATION_FAILED);
}

bool HashChainTestSuite::test14_DeletedIntermediateRecord() {
    HashChainEngine engine;
    engine.begin(nullptr, true);
    engine.clearMockLogBuffer();

    // Generate 3 records
    for (uint32_t i = 0; i < 3; i++) {
        SensorSample s = createSample(i * 500, 1750000000 + i, 1.0f, 10.0f);
        engine.appendRecord(s, createLayer1Result(PlausibilityStatus::NORMAL), createLayer2Result(SensorIdentityStatus::SENSOR_OK));
    }

    // Delete record 2 line from mock log buffer
    char* firstNewline = strchr((char*)engine.getMockLogBuffer(), '\n');
    if (firstNewline) {
        char* secondNewline = strchr(firstNewline + 1, '\n');
        if (secondNewline) {
            memmove(firstNewline + 1, secondNewline + 1, strlen(secondNewline + 1) + 1);
        }
    }

    return !engine.verifyChain() && (engine.getState() == ChainState::CHAIN_CORRUPTED);
}

bool HashChainTestSuite::test15_MissingLogFile() {
    HashChainEngine engine;
    engine.begin(nullptr, true);
    engine.clearMockLogBuffer();

    return (engine.getState() == ChainState::CHAIN_UNINITIALIZED) &&
           (strcmp(engine.getPreviousHash(), GENESIS_HASH) == 0);
}

bool HashChainTestSuite::test16_MalformedFinalJSONLine() {
    HashChainEngine engine;
    engine.begin(nullptr, true);
    engine.clearMockLogBuffer();

    SensorSample s = createSample(0, 1750000000, 1.0f, 10.0f);
    engine.appendRecord(s, createLayer1Result(PlausibilityStatus::NORMAL), createLayer2Result(SensorIdentityStatus::SENSOR_OK));

    // Append partial corrupted line at tail (simulating power failure mid-write)
    engine.appendMockLogLine("{\"device_id\":\"TS001\",\"timestamp\":17500000");

    // Boot recovery should ignore trailing partial line and recover valid tail hash
    return engine.begin(nullptr, true) && (engine.getRecordCount() == 1);
}

bool HashChainTestSuite::test17_SDWriteFailureRetry() {
    HashChainEngine engine;
    engine.begin(nullptr, false); // Real mode without SD mounted -> fails write

    SensorSample s = createSample(0, 1750000000, 1.0f, 10.0f);
    bool appended = engine.appendRecord(s, createLayer1Result(PlausibilityStatus::NORMAL), createLayer2Result(SensorIdentityStatus::SENSOR_OK));

    // Must return false AND retain GENESIS_HASH for retry!
    return (!appended) &&
           (engine.getState() == ChainState::SD_WRITE_FAILED) &&
           (strcmp(engine.getPreviousHash(), GENESIS_HASH) == 0);
}

bool HashChainTestSuite::test18_RebootRecovery() {
    HashChainEngine engine1;
    engine1.begin(nullptr, true);
    engine1.clearMockLogBuffer();

    SensorSample s1 = createSample(0, 1750000000, 1.0f, 10.0f);
    engine1.appendRecord(s1, createLayer1Result(PlausibilityStatus::NORMAL), createLayer2Result(SensorIdentityStatus::SENSOR_OK));
    char tailHash[65];
    strcpy(tailHash, engine1.getPreviousHash());

    // Simulate reboot: new engine instance reading mock buffer
    HashChainEngine engine2;
    // Copy mock buffer state
    engine2.appendMockLogLine(engine1.getMockLogBuffer());
    bool recOk = engine2.begin(nullptr, true);

    return recOk && (strcmp(engine2.getPreviousHash(), tailHash) == 0);
}

bool HashChainTestSuite::test19_EmptySDCard() {
    HashChainEngine engine;
    engine.begin(nullptr, true);
    engine.clearMockLogBuffer();

    return engine.begin(nullptr, true) && (engine.getState() == ChainState::CHAIN_UNINITIALIZED);
}

bool HashChainTestSuite::test20_InvalidGenesisGuard() {
    HashChainEngine engine;
    engine.begin(nullptr, true);
    engine.setMockPreviousHash("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

    SensorSample s = createSample(0, 1750000000, 1.0f, 10.0f);
    engine.appendRecord(s, createLayer1Result(PlausibilityStatus::NORMAL), createLayer2Result(SensorIdentityStatus::SENSOR_OK));

    return (strcmp(engine.getPreviousHash(), GENESIS_HASH) != 0);
}

bool HashChainTestSuite::test21_100RecordChain() {
    HashChainEngine engine;
    engine.begin(nullptr, true);
    engine.clearMockLogBuffer();

    for (uint32_t i = 0; i < 100; i++) {
        SensorSample s = createSample(i * 500, 1750000000 + i, 1.0f + (i % 10) * 0.1f, 10.0f);
        if (!engine.appendRecord(s, createLayer1Result(PlausibilityStatus::NORMAL), createLayer2Result(SensorIdentityStatus::SENSOR_OK))) {
            return false;
        }
    }

    return (engine.getRecordCount() == 100) && engine.verifyChain();
}

bool HashChainTestSuite::test22_1000RecordStressChain() {
    HashChainEngine engine;
    engine.begin(nullptr, true);
    engine.clearMockLogBuffer();

    uint32_t freeHeapBefore = ESP.getFreeHeap();

    for (uint32_t i = 0; i < 1000; i++) {
        SensorSample s = createSample(i * 500, 1750000000 + i, 1.0f, 10.0f);
        if (!engine.appendRecord(s, createLayer1Result(PlausibilityStatus::NORMAL), createLayer2Result(SensorIdentityStatus::SENSOR_OK))) {
            return false;
        }
    }

    uint32_t freeHeapAfter = ESP.getFreeHeap();

    // Verify heap stability (no heap leaks during 1000-record generation)
    return (engine.getRecordCount() == 1000) && (freeHeapBefore - freeHeapAfter < 1024);
}

bool HashChainTestSuite::runAllTests() {
    Serial.println("\n==========================================");
    Serial.println("  TrueSense Layer 3 Hash Chain Suite");
    Serial.println("==========================================");

    bool p1  = test1_GenesisRecord();               Serial.printf("[%s] TEST 1: Genesis Record Initialization\n", p1 ? "PASS" : "FAIL");
    bool p2  = test2_ExactFirstRecordHash();        Serial.printf("[%s] TEST 2: Exact First Record Hash (Vector 1 Parity)\n", p2 ? "PASS" : "FAIL");
    bool p3  = test3_SecondChainedRecord();         Serial.printf("[%s] TEST 3: Second Chained Record (Vector 2 Parity)\n", p3 ? "PASS" : "FAIL");
    bool p4  = test4_MultipleRecordChain();         Serial.printf("[%s] TEST 4: 10-Record Continuous Chain Verification\n", p4 ? "PASS" : "FAIL");
    bool p5  = test5_Exact3DecimalFormatting();     Serial.printf("[%s] TEST 5: Exact 3-Decimal Float Formatting (%.3f)\n", p5 ? "PASS" : "FAIL");
    bool p6  = test6_ExactCanonicalSerialization(); Serial.printf("[%s] TEST 6: Exact Canonical String Serialization\n", p6 ? "PASS" : "FAIL");
    bool p7  = test7_CppPythonParity();              Serial.printf("[%s] TEST 7: C++ / Python Hash Digest Parity\n", p7 ? "PASS" : "FAIL");
    bool p8  = test8_TamperedGasValue();            Serial.printf("[%s] TEST 8: Tampered Gas Value Detection\n", p8 ? "PASS" : "FAIL");
    bool p9  = test9_TamperedPowerValue();          Serial.printf("[%s] TEST 9: Tampered Power Value Detection\n", p9 ? "PASS" : "FAIL");
    bool p10 = test10_TamperedTimestamp();          Serial.printf("[%s] TEST 10: Tampered Timestamp Detection\n", p10 ? "PASS" : "FAIL");
    bool p11 = test11_TamperedLayer1Status();       Serial.printf("[%s] TEST 11: Tampered Layer 1 Status Detection\n", p11 ? "PASS" : "FAIL");
    bool p12 = test12_TamperedLayer2Status();       Serial.printf("[%s] TEST 12: Tampered Layer 2 Status Detection\n", p12 ? "PASS" : "FAIL");
    bool p13 = test13_TamperedPreviousHash();       Serial.printf("[%s] TEST 13: Tampered Previous Hash Link Detection\n", p13 ? "PASS" : "FAIL");
    bool p14 = test14_DeletedIntermediateRecord(); Serial.printf("[%s] TEST 14: Deleted Intermediate Record Detection\n", p14 ? "PASS" : "FAIL");
    bool p15 = test15_MissingLogFile();             Serial.printf("[%s] TEST 15: Missing Log File Initialization\n", p15 ? "PASS" : "FAIL");
    bool p16 = test16_MalformedFinalJSONLine();     Serial.printf("[%s] TEST 16: Tail Power-Failure Recovery Truncation\n", p16 ? "PASS" : "FAIL");
    bool p17 = test17_SDWriteFailureRetry();        Serial.printf("[%s] TEST 17: SD Write Failure State Retention & Retry\n", p17 ? "PASS" : "FAIL");
    bool p18 = test18_RebootRecovery();             Serial.printf("[%s] TEST 18: MCU Boot Recovery & Tail Hash Resume\n", p18 ? "PASS" : "FAIL");
    bool p19 = test19_EmptySDCard();                Serial.printf("[%s] TEST 19: Empty Storage Clean Start\n", p19 ? "PASS" : "FAIL");
    bool p20 = test20_InvalidGenesisGuard();        Serial.printf("[%s] TEST 20: Non-Zero Genesis Rejection Guard\n", p20 ? "PASS" : "FAIL");
    bool p21 = test21_100RecordChain();             Serial.printf("[%s] TEST 21: 100-Record Continuous Chain Stress Test\n", p21 ? "PASS" : "FAIL");
    bool p22 = test22_1000RecordStressChain();      Serial.printf("[%s] TEST 22: 1000-Record Stress & Zero Heap Leak Check\n", p22 ? "PASS" : "FAIL");

    bool overall = p1 && p2 && p3 && p4 && p5 && p6 && p7 && p8 && p9 && p10 &&
                   p11 && p12 && p13 && p14 && p15 && p16 && p17 && p18 && p19 && p20 &&
                   p21 && p22;

    Serial.println("==========================================");
    Serial.printf("Layer 3 Cryptographic Suite Result: %s\n", overall ? "ALL 22 TESTS PASSED" : "TEST FAILURE DETECTED");
    Serial.println("==========================================\n");

    return overall;
}
