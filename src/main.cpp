#include <Arduino.h>
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

void setup() {
    Serial.begin(115200);
    bool serialReady = serial_cmd_module::init();
    bool rtcReady = rtc_time_module::init();
    bool powerReady = power_task_module::init();
    bool storageReady = storage_module::init();
    hashchain_module::init();
    boot_monitor_module::init();
    boot_monitor_module::verifyBootChain();
    plausibility_module::init();
    bool relayReady = relay_module::init();
    sensor_task_module::init();
    fingerprint_module::init();
    QueueHandle_t sensorQueue = xQueueCreate(SENSOR_QUEUE_LENGTH, sizeof(ProovRecord));
    QueueHandle_t storageQueue = xQueueCreate(HASHCHAIN_QUEUE_LENGTH, sizeof(ChainRecord));
    Serial.printf("microSD status: %s\n", storageReady ? "ready" : "unavailable");
    network_task_module::init();
    sensor_task_module::registerSensorCommands();
    rtc_time_module::registerRtcCommands();
    power_task_module::registerPowerCommands();
    storage_module::registerStorageCommands();
    relay_module::registerRelayCommands();
    fingerprint_module::registerFingerprintCommands();
    network_task_module::registerNetworkCommands();
    boot_monitor_module::registerCommands();
    Serial.printf("Serial command service: %s\n", serialReady ? "ready" : "unavailable");
    Serial.printf("DS3231 status: %s\n", rtcReady ? "ready" : "unavailable");
    Serial.printf("INA219 status: %s\n", powerReady ? "ready" : "unavailable");
    Serial.printf("Relay status: %s\n", relayReady ? "ready" : "unavailable");
    boot_monitor_module::initializeWatchdog();
    fingerprint_module::startFingerprintTask();
    sensor_task_module::startSensorTask(sensorQueue);
    hashchain_module::startHashchainTask(sensorQueue, storageQueue);
    storage_module::startStorageTask(storageQueue);
    relay_module::startRelayTask();
    boot_monitor_module::startTask();
    power_task_module::startPowerTask();
    rtc_time_module::startRtcTimeTask();
    network_task_module::startNetworkTask();
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));
}
