#ifdef TEST_MODE
#include "test_harness.h"
#include "config.h"
#include "storage.h"
#include "hashchain.h"
#include "plausibility.h"
#include "fingerprint.h"
#include "relay.h"
#include "rtc_time.h"
#include "boot_monitor.h"
#include "serial_cmd.h"
#include <Preferences.h>
#include <esp_system.h>
#include <freertos/queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum TestRequestType : uint8_t { TEST_REQUEST_ONE, TEST_REQUEST_ALL, TEST_REQUEST_STATUS, TEST_REQUEST_SUMMARY, TEST_REQUEST_GO, TEST_REQUEST_ABORT };
enum ScenarioRunResult : uint8_t { SCENARIO_COMPLETE, SCENARIO_WAIT_REBOOT, SCENARIO_ABORTED };

struct TestRequest { TestRequestType type; uint8_t scenario; };
struct HarnessState {
    uint8_t scenario;
    uint8_t phase;
    bool runAll;
    uint32_t startLine;
    uint32_t startEpoch;
    TestScenarioResult results[TEST_SCENARIO_COUNT];
};

static QueueHandle_t testRequestQueue;
static HarnessState state{};
static bool testActive;
static bool abortRequested;
static bool goReceived;
static uint32_t lastStackReport;

static const char* scenarioName(uint8_t scenario) {
    switch (scenario) {
        case 1: return "NORMAL_LOGGING";
        case 2: return "PLAUSIBLE_DETECT";
        case 3: return "SUSPICIOUS_DETECT";
        case 4: return "TAMPER_DETECT";
        case 5: return "POWER_LOSS_RECOVERY";
        case 6: return "SENSOR_SWAP";
        default: return "UNKNOWN";
    }
}

static bool storeState() {
    Preferences preferences;
    if (!preferences.begin(TEST_NVS_NAMESPACE, false)) return false;
    bool stored = preferences.putUChar(TEST_NVS_SCENARIO_KEY, state.scenario) > 0;
    stored = preferences.putUChar(TEST_NVS_PHASE_KEY, state.phase) > 0 && stored;
    stored = preferences.putBool(TEST_NVS_RUN_ALL_KEY, state.runAll) && stored;
    stored = preferences.putUInt(TEST_NVS_START_LINE_KEY, state.startLine) > 0 && stored;
    stored = preferences.putUInt(TEST_NVS_START_EPOCH_KEY, state.startEpoch) > 0 && stored;
    stored = preferences.putBytes(TEST_NVS_RESULTS_KEY, state.results, sizeof(state.results)) == sizeof(state.results) && stored;
    preferences.end();
    return stored;
}

static void loadState() {
    Preferences preferences;
    if (!preferences.begin(TEST_NVS_NAMESPACE, true)) return;
    state.scenario = preferences.getUChar(TEST_NVS_SCENARIO_KEY, 0);
    state.phase = preferences.getUChar(TEST_NVS_PHASE_KEY, 0);
    state.runAll = preferences.getBool(TEST_NVS_RUN_ALL_KEY, false);
    state.startLine = preferences.getUInt(TEST_NVS_START_LINE_KEY, 0);
    state.startEpoch = preferences.getUInt(TEST_NVS_START_EPOCH_KEY, 0);
    preferences.getBytes(TEST_NVS_RESULTS_KEY, state.results, sizeof(state.results));
    preferences.end();
}

static bool enqueue(TestRequestType type, uint8_t scenario = 0) {
    if (testRequestQueue == nullptr) return false;
    TestRequest request{type, scenario};
    return xQueueSend(testRequestQueue, &request, 0) == pdPASS;
}

static void printSummary() {
    uint32_t passed = 0;
    Serial.println("SCENARIO | NAME | RESULT | TIME_MS | DETAIL");
    for (uint8_t index = 0; index < TEST_SCENARIO_COUNT; ++index) {
        const TestScenarioResult& result = state.results[index];
        const char* outcome = result.complete ? (result.passed ? "PASS" : "FAIL") : "NOT_RUN";
        if (result.complete && result.passed) ++passed;
        Serial.printf("%u | %s | %s | %lu | %s\n", static_cast<unsigned int>(index + 1), scenarioName(index + 1), outcome, static_cast<unsigned long>(result.elapsedMs), result.complete ? result.detail : "pending");
    }
    Serial.printf("TOTAL: %lu/%u\n", static_cast<unsigned long>(passed), static_cast<unsigned int>(TEST_SCENARIO_COUNT));
}

static void printStatus() {
    Serial.printf("TEST STATUS scenario=%u name=%s phase=%u active=%u run_all=%u\n", static_cast<unsigned int>(state.scenario), scenarioName(state.scenario), static_cast<unsigned int>(state.phase), static_cast<unsigned int>(testActive), static_cast<unsigned int>(state.runAll));
}

static void handleTestCommand(const char* args) {
    if (strcasecmp(args, "ALL") == 0) {
        if (!test_harness_module::runAll()) Serial.println(SERIAL_HANDLER_QUEUE_FULL);
    } else if (strcasecmp(args, "STATUS") == 0) {
        if (!enqueue(TEST_REQUEST_STATUS)) Serial.println(SERIAL_HANDLER_QUEUE_FULL);
    } else if (strcasecmp(args, "SUMMARY") == 0) {
        test_harness_module::getResults();
    } else if (strcasecmp(args, "ABORT") == 0) {
        if (!enqueue(TEST_REQUEST_ABORT)) Serial.println(SERIAL_HANDLER_QUEUE_FULL);
    } else {
        char* end = nullptr;
        unsigned long scenario = strtoul(args, &end, 10);
        if (args[0] == '\0' || end == args || *end != '\0' || scenario < 1 || scenario > 6) {
            Serial.println("Usage: TEST <1-6|ALL|STATUS|SUMMARY|ABORT>");
            return;
        }
        if (!test_harness_module::startScenario(static_cast<uint8_t>(scenario))) Serial.println(SERIAL_HANDLER_QUEUE_FULL);
    }
}

static void handleGoCommand(const char* args) {
    if (args[0] != '\0') {
        Serial.println("GO takes no arguments");
        return;
    }
    if (!enqueue(TEST_REQUEST_GO)) Serial.println(SERIAL_HANDLER_QUEUE_FULL);
}

static void serviceRequest(const TestRequest& request) {
    if (request.type == TEST_REQUEST_GO) goReceived = true;
    else if (request.type == TEST_REQUEST_ABORT) abortRequested = true;
    else if (request.type == TEST_REQUEST_STATUS) printStatus();
    else if (request.type == TEST_REQUEST_SUMMARY) printSummary();
    else if (request.type == TEST_REQUEST_ONE || request.type == TEST_REQUEST_ALL) Serial.println("TEST BUSY");
}

static void serviceRequests() {
    boot_monitor_module::reportTaskStack("test_harness", lastStackReport);
    TestRequest request{};
    while (xQueueReceive(testRequestQueue, &request, 0) == pdTRUE) serviceRequest(request);
}

static bool waitForGo(const char* instruction) {
    goReceived = false;
    abortRequested = false;
    Serial.printf("ACTION: %s\n", instruction);
    uint32_t started = millis();
    while (millis() - started < TEST_ACTION_TIMEOUT_MS) {
        serviceRequests();
        if (abortRequested) return false;
        if (goReceived) return true;
        vTaskDelay(pdMS_TO_TICKS(TEST_ACTION_POLL_MS));
    }
    return false;
}

static bool waitDuration(uint32_t durationMs) {
    uint32_t started = millis();
    while (millis() - started < durationMs) {
        serviceRequests();
        if (abortRequested) return false;
        vTaskDelay(pdMS_TO_TICKS(TEST_RECORD_POLL_MS));
    }
    return true;
}

static uint32_t scenarioElapsed(uint8_t scenario, uint32_t localStart) {
    if ((scenario == 5 || scenario == 6) && state.startEpoch > 0) {
        uint32_t now = rtc_time_module::getTimestamp();
        if (now >= state.startEpoch) return (now - state.startEpoch) * MILLISECONDS_PER_SECOND;
    }
    return millis() - localStart;
}

static void completeScenario(uint8_t scenario, bool passed, uint32_t localStart, const char* detail) {
    TestScenarioResult& result = state.results[scenario - 1];
    result.complete = 1;
    result.passed = passed ? 1 : 0;
    result.elapsedMs = scenarioElapsed(scenario, localStart);
    snprintf(result.detail, sizeof(result.detail), "%s", detail);
    Serial.printf("TEST %u %s %s %lums %s\n", static_cast<unsigned int>(scenario), scenarioName(scenario), passed ? "PASS" : "FAIL", static_cast<unsigned long>(result.elapsedMs), result.detail);
    state.scenario = 0;
    state.phase = 0;
    state.startLine = 0;
    state.startEpoch = 0;
    if (strcmp(detail, "aborted") == 0 || strcmp(detail, "action timeout") == 0) state.runAll = false;
    storeState();
}

static bool readLastRecord(ChainRecord& record) {
    return storage_module::readLastNRecords(1, &record, 1) == 1;
}

static bool verifyRecentRecords(uint32_t& count, int32_t& failure) {
    ChainRecord records[BOOT_VERIFY_RECORDS];
    count = storage_module::readLastNRecords(BOOT_VERIFY_RECORDS, records, BOOT_VERIFY_RECORDS);
    failure = hashchain_module::verifyChainSegment(records, count);
    return failure < 0;
}

static ScenarioRunResult runScenario(uint8_t scenario, bool resume) {
    testActive = true;
    abortRequested = false;
    if (!resume) {
        state.scenario = scenario;
        state.phase = 0;
        state.startLine = storage_module::countTodayLines();
        state.startEpoch = rtc_time_module::getTimestamp();
        storeState();
    }
    uint32_t localStart = millis();
    char detail[TEST_RESULT_DETAIL_LENGTH]{};
    bool passed = false;
    if (scenario == 1) {
        uint32_t startLine = state.startLine + 1;
        bool waited = waitDuration(TEST_NORMAL_DURATION_MS);
        TestRecordSummary summary{};
        bool summaryOk = storage_module::summarizeTodayFromLine(startLine, summary);
        uint32_t recordCount = 0;
        uint32_t failure = UINT32_MAX;
        bool verified = storage_module::verifyTodayFile(failure, recordCount);
        uint32_t expected = TEST_NORMAL_DURATION_MS / SAMPLE_INTERVAL_MS;
        uint32_t minimum = expected * TEST_NORMAL_PASS_PERCENT / 100;
        passed = waited && summaryOk && summary.records >= minimum && summary.suspicious == 0 && verified;
        snprintf(detail, sizeof(detail), "records=%lu file_records=%lu min=%lu suspicious=%lu verify=%s index=%lu", static_cast<unsigned long>(summary.records), static_cast<unsigned long>(recordCount), static_cast<unsigned long>(minimum), static_cast<unsigned long>(summary.suspicious), verified ? "PASS" : "FAIL", static_cast<unsigned long>(failure));
    } else if (scenario == 2 || scenario == 3) {
        bool plausibleTest = scenario == 2;
        bool relayRequested = relay_module::requestRelay(plausibleTest);
        uint32_t relayWaitStart = millis();
        while (relayRequested && relay_module::getRelayState() != plausibleTest && millis() - relayWaitStart < TEST_ACTION_TIMEOUT_MS) vTaskDelay(pdMS_TO_TICKS(TEST_ACTION_POLL_MS));
        uint32_t startLine = storage_module::countTodayLines() + 1;
        const char* instruction = plausibleTest ? "Apply gas stimulus near the MQ-135, then send GO" : "Apply the same gas stimulus near the MQ-135 with relay OFF, then send GO";
        bool actionAccepted = relayRequested && relay_module::getRelayState() == plausibleTest && waitForGo(instruction);
        bool waited = actionAccepted && waitDuration(TEST_STIMULUS_TIMEOUT_MS);
        TestRecordSummary summary{};
        bool summaryOk = storage_module::summarizeTodayFromLine(startLine, summary);
        relay_module::requestRelay(false);
        if (plausibleTest) {
            passed = waited && summaryOk && summary.plausible >= TEST_MIN_FLAGGED_RECORDS && summary.suspicious == 0;
            snprintf(detail, sizeof(detail), "plausible=%lu suspicious=%lu minimum=%u", static_cast<unsigned long>(summary.plausible), static_cast<unsigned long>(summary.suspicious), static_cast<unsigned int>(TEST_MIN_FLAGGED_RECORDS));
        } else {
            passed = waited && summaryOk && summary.suspicious >= TEST_MIN_FLAGGED_RECORDS && summary.plausible == 0;
            snprintf(detail, sizeof(detail), "suspicious=%lu plausible=%lu minimum=%u sd_records=%lu", static_cast<unsigned long>(summary.suspicious), static_cast<unsigned long>(summary.plausible), static_cast<unsigned int>(TEST_MIN_FLAGGED_RECORDS), static_cast<unsigned long>(summary.records));
        }
        if (!actionAccepted) snprintf(detail, sizeof(detail), "%s", abortRequested ? "aborted" : "action timeout");
    } else if (scenario == 4) {
        uint32_t targetLine = storage_module::countTodayLines();
        bool backedUp = targetLine > 0 && storage_module::backupTodayFile();
        bool tampered = backedUp && storage_module::tamperTodayLine(targetLine);
        uint32_t failureIndex = UINT32_MAX;
        uint32_t records = 0;
        bool verifyReturned = storage_module::verifyTodayFile(failureIndex, records);
        bool expectedFailure = tampered && !verifyReturned && failureIndex == targetLine - 1;
        bool restored = backedUp && storage_module::restoreTodayFile();
        uint32_t restoredFailure = UINT32_MAX;
        uint32_t restoredRecords = 0;
        bool restoredVerify = restored && storage_module::verifyTodayFile(restoredFailure, restoredRecords);
        passed = expectedFailure && restoredVerify;
        snprintf(detail, sizeof(detail), "tampered_index=%lu fail_index=%lu restored=%u verify=%s", static_cast<unsigned long>(targetLine > 0 ? targetLine - 1 : 0), static_cast<unsigned long>(failureIndex), static_cast<unsigned int>(restored), restoredVerify ? "PASS" : "FAIL");
    } else if (scenario == 5 && !resume) {
        if (!waitDuration(TEST_NORMAL_DURATION_MS)) {
            completeScenario(scenario, false, localStart, "aborted");
            testActive = false;
            return SCENARIO_ABORTED;
        }
        state.phase = 1;
        storeState();
        bool actionAccepted = waitForGo("Pull power during logging, then restore power to resume this scenario");
        if (!actionAccepted) {
            completeScenario(scenario, false, localStart, abortRequested ? "aborted" : "action timeout");
            testActive = false;
            return SCENARIO_ABORTED;
        }
        state.phase = 2;
        storeState();
        Serial.println("POWER_LOSS_RECOVERY armed");
        testActive = false;
        return SCENARIO_WAIT_REBOOT;
    } else if (scenario == 5) {
        bool bootAccepted = boot_monitor_module::bootVerifyPassed() || (boot_monitor_module::bootVerifyCount() > 0 && boot_monitor_module::bootVerifyFailureIndex() == static_cast<int32_t>(boot_monitor_module::bootVerifyCount() - 1));
        uint32_t beforeLines = storage_module::countTodayLines();
        bool waited = waitDuration(TEST_REBOOT_LOG_CONFIRM_MS);
        uint32_t afterLines = storage_module::countTodayLines();
        ChainRecord latest{};
        char pointer[HASH_HEX_LEN + 1]{};
        bool pointerLoaded = hashchain_module::loadLatestHash(pointer, sizeof(pointer));
        bool latestRead = readLastRecord(latest);
        bool pointerMatches = pointerLoaded && latestRead && strcmp(pointer, latest.hash) == 0;
        uint32_t verifiedCount = 0;
        int32_t verifyFailure = -1;
        bool chainVerified = verifyRecentRecords(verifiedCount, verifyFailure);
        uint32_t fileFailure = UINT32_MAX;
        uint32_t fileCount = 0;
        bool fileVerified = storage_module::verifyTodayFile(fileFailure, fileCount);
        passed = bootAccepted && waited && afterLines > beforeLines && pointerMatches && chainVerified && fileVerified;
        snprintf(detail, sizeof(detail), "boot=%s pointer=%s new_records=%lu chain=%s index=%ld", boot_monitor_module::bootVerifyPassed() ? "OK" : (bootAccepted ? "FINAL_PARTIAL" : "FAIL"), pointerMatches ? "OK" : "FAIL", static_cast<unsigned long>(afterLines > beforeLines ? afterLines - beforeLines : 0), chainVerified && fileVerified ? "PASS" : "FAIL", static_cast<long>(chainVerified ? fileFailure : verifyFailure));
    } else if (scenario == 6 && !resume) {
        if (!fingerprint_module::isEnrolled()) {
            completeScenario(scenario, false, localStart, "not enrolled");
            testActive = false;
            return SCENARIO_COMPLETE;
        }
        state.phase = 1;
        storeState();
        bool actionAccepted = waitForGo("Disconnect or swap the MQ-135, then cold power-cycle the ESP32");
        if (!actionAccepted) {
            completeScenario(scenario, false, localStart, abortRequested ? "aborted" : "action timeout");
            testActive = false;
            return SCENARIO_ABORTED;
        }
        state.phase = 2;
        storeState();
        Serial.println("SENSOR_SWAP armed");
        testActive = false;
        return SCENARIO_WAIT_REBOOT;
    } else if (scenario == 6) {
        uint32_t waitStart = millis();
        while (fingerprint_module::getStatus() == FP_STATUS_PENDING && millis() - waitStart < TEST_FINGERPRINT_WAIT_MS) {
            serviceRequests();
            if (abortRequested) break;
            vTaskDelay(pdMS_TO_TICKS(TEST_RECORD_POLL_MS));
        }
        uint8_t status = fingerprint_module::getStatus();
        uint32_t startLine = storage_module::countTodayLines() + 1;
        bool waited = !abortRequested && status == FP_STATUS_MISMATCH && waitDuration(TEST_LOG_CONFIRM_MS);
        TestRecordSummary summary{};
        bool summaryOk = storage_module::summarizeTodayFromLine(startLine, summary);
        passed = waited && summaryOk && summary.records > 0 && summary.fingerprintMismatch > 0;
        snprintf(detail, sizeof(detail), "fingerprint=%u mismatch_records=%lu logging=%s", static_cast<unsigned int>(status), static_cast<unsigned long>(summary.fingerprintMismatch), passed ? "PASS" : "FAIL");
    }
    relay_module::requestRelay(false);
    if (abortRequested) {
        completeScenario(scenario, false, localStart, "aborted");
        testActive = false;
        return SCENARIO_ABORTED;
    }
    completeScenario(scenario, passed, localStart, detail);
    testActive = false;
    return SCENARIO_COMPLETE;
}

static void clearResults() {
    memset(state.results, 0, sizeof(state.results));
    state.scenario = 0;
    state.phase = 0;
    state.runAll = false;
    state.startLine = 0;
    state.startEpoch = 0;
    storeState();
}

static void cancelScenario() {
    state.scenario = 0;
    state.phase = 0;
    state.runAll = false;
    state.startLine = 0;
    state.startEpoch = 0;
    storeState();
}

static void runSequence(uint8_t firstScenario) {
    for (uint8_t scenario = firstScenario; scenario <= TEST_SCENARIO_COUNT; ++scenario) {
        ScenarioRunResult result = runScenario(scenario, false);
        if (result != SCENARIO_COMPLETE) return;
    }
    state.runAll = false;
    storeState();
}

namespace test_harness_module {
bool init() {
    testRequestQueue = xQueueCreate(TEST_COMMAND_QUEUE_LENGTH, sizeof(TestRequest));
    loadState();
    return testRequestQueue != nullptr;
}

bool startScenario(uint8_t scenario) {
    if (scenario < 1 || scenario > 6) return false;
    return enqueue(TEST_REQUEST_ONE, scenario);
}

bool runAll() { return enqueue(TEST_REQUEST_ALL); }

void getResults() {
    if (!enqueue(TEST_REQUEST_SUMMARY)) Serial.println(SERIAL_HANDLER_QUEUE_FULL);
}

void registerCommands() {
    serial_cmd_module::registerCommand("TEST", handleTestCommand);
    serial_cmd_module::registerCommand("GO", handleGoCommand);
}

void startTask() { xTaskCreate(task, "test_harness", TEST_TASK_STACK, nullptr, TEST_TASK_PRIORITY, nullptr); }

void task(void* parameter) {
    static_cast<void>(parameter);
    lastStackReport = millis();
    if ((state.scenario == 5 || state.scenario == 6) && state.phase == 2) {
        uint8_t resumedScenario = state.scenario;
        ScenarioRunResult result = runScenario(resumedScenario, true);
        if (result == SCENARIO_COMPLETE && state.runAll && resumedScenario < TEST_SCENARIO_COUNT) runSequence(resumedScenario + 1);
        else if (result == SCENARIO_COMPLETE && state.runAll) { state.runAll = false; storeState(); }
    } else if (state.runAll) {
        uint8_t first = 1;
        while (first <= TEST_SCENARIO_COUNT && state.results[first - 1].complete) ++first;
        if (first <= TEST_SCENARIO_COUNT) runSequence(first);
        else { state.runAll = false; storeState(); }
    }
    for (;;) {
        boot_monitor_module::reportTaskStack("test_harness", lastStackReport);
        TestRequest request{};
        if (xQueueReceive(testRequestQueue, &request, portMAX_DELAY) != pdTRUE) continue;
        if (request.type == TEST_REQUEST_STATUS) printStatus();
        else if (request.type == TEST_REQUEST_SUMMARY) printSummary();
        else if (request.type == TEST_REQUEST_ABORT) {
            abortRequested = true;
            relay_module::requestRelay(false);
            cancelScenario();
            Serial.println("TEST ABORTED");
        } else if (request.type == TEST_REQUEST_ONE) {
            if (testActive) Serial.println("TEST BUSY");
            else {
                clearResults();
                runScenario(request.scenario, false);
            }
        } else if (request.type == TEST_REQUEST_ALL) {
            if (testActive) Serial.println("TEST BUSY");
            else {
                clearResults();
                state.runAll = true;
                storeState();
                runSequence(1);
            }
        }
    }
}
}
#endif
