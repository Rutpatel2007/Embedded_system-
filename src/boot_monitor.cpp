#include "boot_monitor.h"
#include "config.h"
#include "storage.h"
#include "hashchain.h"
#include "fingerprint.h"
#include "relay.h"
#include "network_task.h"
#include "sensor_task.h"
#include "plausibility.h"
#include "serial_cmd.h"
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <freertos/queue.h>
#include <strings.h>

enum MonitorRequestType : uint8_t { MONITOR_REQUEST_HEALTH, MONITOR_REQUEST_STATS, MONITOR_REQUEST_STATS_RESET };
struct MonitorRequest { MonitorRequestType type; };

static QueueHandle_t monitorQueue;
static bool verifyPassed;
static bool verifySkipped;
static uint32_t verifiedCount;
static int32_t verifyFailure = -1;
static TaskHandle_t watchdogTasks[3]{};
static portMUX_TYPE watchdogMux = portMUX_INITIALIZER_UNLOCKED;

static void handleHealth(const char* args) {
    static_cast<void>(args);
    MonitorRequest request{MONITOR_REQUEST_HEALTH};
    if (monitorQueue == nullptr || xQueueSend(monitorQueue, &request, 0) != pdPASS) Serial.println(SERIAL_HANDLER_QUEUE_FULL);
}

static void handleStats(const char* args) {
    MonitorRequest request{MONITOR_REQUEST_STATS};
    if (strcasecmp(args, "RESET") == 0) request.type = MONITOR_REQUEST_STATS_RESET;
    else if (args[0] != '\0') { Serial.println("Usage: STATS [RESET]"); return; }
    if (monitorQueue == nullptr || xQueueSend(monitorQueue, &request, 0) != pdPASS) Serial.println(SERIAL_HANDLER_QUEUE_FULL);
}

static void printHealth() {
    const char* verifyState = verifySkipped ? "SKIPPED" : (verifyPassed ? "OK" : "FAIL");
    Serial.printf("HEALTH uptime_ms=%lu boot_verify=%s boot_count=%lu boot_fail_index=%ld sd=%u wifi=%u fingerprint=%u relay=%u drops_sensor_hash=%lu drops_hash_sd=%lu drops_relay=%lu drops_fingerprint=%lu free_heap=%lu min_free_heap=%lu\n",
        static_cast<unsigned long>(millis()), verifyState, static_cast<unsigned long>(verifiedCount), static_cast<long>(verifyFailure),
        static_cast<unsigned int>(storage_module::isReady()), static_cast<unsigned int>(network_task_module::wifiConnected()),
        static_cast<unsigned int>(fingerprint_module::getStatus()), static_cast<unsigned int>(relay_module::getRelayState()),
        static_cast<unsigned long>(sensor_task_module::getQueueDropCount()), static_cast<unsigned long>(hashchain_module::getQueueDropCount()),
        static_cast<unsigned long>(relay_module::getDropCount()), static_cast<unsigned long>(fingerprint_module::getDropCount()),
        static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()));
}

namespace boot_monitor_module {
bool init() {
    monitorQueue = xQueueCreate(MONITOR_COMMAND_QUEUE_LENGTH, sizeof(MonitorRequest));
    return monitorQueue != nullptr;
}

bool verifyBootChain() {
    verifyPassed = false;
    verifySkipped = false;
    verifiedCount = 0;
    verifyFailure = -1;
    if (!storage_module::isReady()) {
        verifySkipped = true;
        Serial.println("BOOT VERIFY: SKIPPED no sd");
        return false;
    }
    ChainRecord records[BOOT_VERIFY_RECORDS];
    verifiedCount = storage_module::readLastNRecords(BOOT_VERIFY_RECORDS, records, BOOT_VERIFY_RECORDS);
    verifyFailure = hashchain_module::verifyChainSegment(records, verifiedCount);
    verifyPassed = verifyFailure < 0;
    if (verifyPassed) Serial.printf("BOOT VERIFY: OK n=%lu\n", static_cast<unsigned long>(verifiedCount));
    else Serial.printf("BOOT VERIFY: FAIL index=%ld\n", static_cast<long>(verifyFailure < 0 ? 0 : verifyFailure));
    return verifyPassed;
}

bool bootVerifyPassed() { return verifyPassed; }
uint32_t bootVerifyCount() { return verifiedCount; }
int32_t bootVerifyFailureIndex() { return verifyFailure; }

void registerCommands() {
    serial_cmd_module::registerCommand("HEALTH", handleHealth);
    serial_cmd_module::registerCommand("STATS", handleStats);
}

void startTask() { xTaskCreate(task, "monitor", STACK_SIZE_DEFAULT, nullptr, PRIORITY_NETWORK, nullptr); }

void task(void* parameter) {
    static_cast<void>(parameter);
    uint32_t lastDropsReport = millis();
    uint32_t lastStackReport = millis();
    for (;;) {
        reportTaskStack("monitor", lastStackReport);
        MonitorRequest request;
        if (xQueueReceive(monitorQueue, &request, pdMS_TO_TICKS(250)) == pdTRUE) {
            if (request.type == MONITOR_REQUEST_HEALTH) printHealth();
            else if (request.type == MONITOR_REQUEST_STATS_RESET) { plausibility_module::resetPlausibilityStats(); Serial.println("STATS reset"); }
            else {
                PlausibilityStats stats = plausibility_module::getPlausibilityStats();
                Serial.printf("STATS normal=%lu plausible=%lu suspicious=%lu\n", static_cast<unsigned long>(stats.normalCount), static_cast<unsigned long>(stats.plausibleCount), static_cast<unsigned long>(stats.suspiciousCount));
            }
        }
        if (millis() - lastDropsReport >= DROP_REPORT_INTERVAL_MS) {
            lastDropsReport = millis();
            uint32_t sensorDrops = sensor_task_module::getQueueDropCount();
            uint32_t hashDrops = hashchain_module::getQueueDropCount();
            uint32_t relayDrops = relay_module::getDropCount();
            uint32_t fingerprintDrops = fingerprint_module::getDropCount();
            if (sensorDrops != 0) Serial.printf("DROPS sensor->hashchain=%lu\n", static_cast<unsigned long>(sensorDrops));
            if (hashDrops != 0) Serial.printf("DROPS hashchain->sdwrite=%lu\n", static_cast<unsigned long>(hashDrops));
            if (relayDrops != 0) Serial.printf("DROPS relay_requests=%lu\n", static_cast<unsigned long>(relayDrops));
            if (fingerprintDrops != 0) Serial.printf("DROPS fingerprint_status=%lu\n", static_cast<unsigned long>(fingerprintDrops));
        }
    }
}

void initializeWatchdog() {
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
}

void feedWatchdog() {
    TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
    bool subscribed = false;
    int reservedSlot = -1;
    portENTER_CRITICAL(&watchdogMux);
    for (size_t index = 0; index < 3; ++index) subscribed = subscribed || watchdogTasks[index] == currentTask;
    if (!subscribed) {
        for (size_t index = 0; index < 3; ++index) {
            if (watchdogTasks[index] == nullptr) {
                watchdogTasks[index] = currentTask;
                reservedSlot = static_cast<int>(index);
                break;
            }
        }
    }
    portEXIT_CRITICAL(&watchdogMux);
    if (reservedSlot >= 0) {
        if (esp_task_wdt_add(nullptr) == ESP_OK) subscribed = true;
        else {
            portENTER_CRITICAL(&watchdogMux);
            watchdogTasks[reservedSlot] = nullptr;
            portEXIT_CRITICAL(&watchdogMux);
        }
    }
    if (subscribed) esp_task_wdt_reset();
}

void reportTaskStack(const char* name, uint32_t& lastReportMs) {
    uint32_t now = millis();
    if (now - lastReportMs >= STACK_REPORT_INTERVAL_MS) {
        lastReportMs = now;
        Serial.printf("STACK %s hwm=%u\n", name, static_cast<unsigned int>(uxTaskGetStackHighWaterMark(nullptr)));
    }
}
}
