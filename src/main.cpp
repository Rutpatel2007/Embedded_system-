#include <Arduino.h>
#include <esp_log.h>
#include "config.h"
#include "sensor_task.h"
#include "power_task.h"
#include "rtc_time.h"
#include "storage.h"
#include "hashchain.h"
#include "fingerprint.h"
#include "network_task.h"
#include "serial_cmd.h"
#include "plausibility.h"
#include "relay.h"
#include "boot_monitor.h"
#ifdef TEST_MODE
#include "test_harness.h"
#endif

void setup() {
    Serial.begin(115200);
    Serial.println("TrueSense");
    esp_log_level_set("Wire", ESP_LOG_NONE);
    esp_log_level_set("Preferences", ESP_LOG_NONE);
    esp_log_level_set("sd_diskio", ESP_LOG_ERROR);
    bool serialReady = serial_cmd_module::init();
    bool rtcReady = rtc_time_module::init();
    bool powerReady = power_task_module::init();
    bool storageReady = storage_module::init();
    storage_module::recoverPartialTodayTail();
    hashchain_module::init();
    boot_monitor_module::init();
    bool bootChainValid = boot_monitor_module::verifyBootChain();
    int32_t bootFailureIndex = boot_monitor_module::bootVerifyFailureIndex();
    if (!bootChainValid && bootFailureIndex >= 0) {
        Serial.printf("CHAIN BREAK DETECTED index=%ld\n", static_cast<long>(bootFailureIndex));
    }
    if (storageReady && bootChainValid && hashchain_module::latestPointerInconsistent()) {
        if (storage_module::archiveLogFiles() && hashchain_module::resetChain()) {
            boot_monitor_module::clearBootVerifyState();
            storage_module::clearVerifyState();
            Serial.println("CHAIN RESTARTED");
        }
    }
#ifdef TEST_MODE
    bool testHarnessReady = test_harness_module::init();
#endif
    plausibility_module::init();
    bool relayReady = relay_module::init();
    sensor_task_module::init();
    fingerprint_module::init();
    QueueHandle_t sensorQueue = xQueueCreate(SENSOR_QUEUE_LENGTH, sizeof(TrueSenseRecord));
    QueueHandle_t storageQueue = xQueueCreate(HASHCHAIN_QUEUE_LENGTH, sizeof(ChainRecord));
    Serial.printf("storage backend: %s\n", storage_module::backendName());
    Serial.printf("microSD status: %s\n", storage_module::isSdReady() ? "ready" : STORAGE_BACKEND == 2 ? "disabled" : "unavailable");
    network_task_module::init();
    sensor_task_module::registerSensorCommands();
    rtc_time_module::registerRtcCommands();
    power_task_module::registerPowerCommands();
    storage_module::registerStorageCommands();
    relay_module::registerRelayCommands();
    fingerprint_module::registerFingerprintCommands();
    network_task_module::registerNetworkCommands();
    boot_monitor_module::registerCommands();
#ifdef TEST_MODE
    test_harness_module::registerCommands();
#endif
    Serial.printf("Serial command service: %s\n", serialReady ? "ready" : "unavailable");
    Serial.printf("DS3231 status: %s\n", rtcReady ? "ready" : "unavailable");
    Serial.printf("INA219 status: %s\n", powerReady ? "ready" : "unavailable");
    Serial.printf("Relay status: %s\n", relayReady ? "ready" : "unavailable");
    boot_monitor_module::initializeWatchdog();
    fingerprint_module::startFingerprintTask();
    if (sensorQueue != nullptr) sensor_task_module::startSensorTask(sensorQueue);
    if (sensorQueue != nullptr && storageQueue != nullptr) hashchain_module::startHashchainTask(sensorQueue, storageQueue);
    if (storageQueue != nullptr) storage_module::startStorageTask(storageQueue);
    relay_module::startRelayTask();
    boot_monitor_module::startTask();
    power_task_module::startPowerTask();
    rtc_time_module::startRtcTimeTask();
    network_task_module::startNetworkTask();
#ifdef TEST_MODE
    Serial.printf("Test harness: %s\n", testHarnessReady ? "ready" : "unavailable");
    test_harness_module::startTask();
#endif
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));
}
