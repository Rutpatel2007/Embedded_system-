#include "hashchain.h"
#include "config.h"
#include "storage.h"
#include "boot_monitor.h"
#include <Preferences.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <string.h>

static uint32_t hashchainQueueDrops;
static portMUX_TYPE hashchainDropMux = portMUX_INITIALIZER_UNLOCKED;

namespace hashchain_module {
void init() {
    char latestHash[HASH_HEX_LEN + 1]{};
    if (storage_module::isReady() && recoverLatestHash(latestHash, sizeof(latestHash))) {
        storeLatestHash(latestHash);
    } else if (!loadLatestHash(latestHash, sizeof(latestHash))) {
        String genesis = String(GENESIS_PREFIX) + DEVICE_ID;
        if (computeHash(genesis, latestHash, sizeof(latestHash))) storeLatestHash(latestHash);
    }
}

String serializeRecord(const ChainRecord& record) {
    String serialized;
    serialized.reserve(160);
    serialized += record.deviceId;
    serialized += '|';
    serialized += String(record.timestamp);
    serialized += '|';
    serialized += String(record.gasPpmX100);
    serialized += '|';
    serialized += String(record.powerMw);
    serialized += '|';
    serialized += String(record.plausibilityFlag);
    serialized += '|';
    serialized += String(record.fingerprintStatus);
    serialized += '|';
    serialized += record.previousHash;
    return serialized;
}

bool computeHash(const String& serializedRecord, char* output, size_t outputSize) {
    if (output == nullptr || outputSize < HASH_HEX_LEN + 1) {
        return false;
    }
    uint8_t digest[32];
    if (mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(serializedRecord.c_str()), serializedRecord.length(), digest, 0) != 0) {
        return false;
    }
    for (size_t index = 0; index < sizeof(digest); ++index) {
        snprintf(output + index * 2, outputSize - index * 2, "%02x", digest[index]);
    }
    output[HASH_HEX_LEN] = '\0';
    return true;
}

bool appendToChain(const ProovRecord& input, const char* previousHash, ChainRecord& output) {
    memset(&output, 0, sizeof(output));
    snprintf(output.deviceId, sizeof(output.deviceId), "%s", DEVICE_ID);
    output.timestamp = input.timestamp;
    output.gasPpmX100 = input.gasMilliUnits > 0 ? static_cast<uint32_t>(input.gasMilliUnits) : 0;
    output.powerMw = input.powerMilliWatts;
    output.plausibilityFlag = input.plausibilityFlag;
    output.fingerprintStatus = input.fingerprintStatus;
    snprintf(output.previousHash, sizeof(output.previousHash), "%s", previousHash);
    String serialized = serializeRecord(output);
    if (!computeHash(serialized, output.hash, sizeof(output.hash))) {
        return false;
    }
    return storeLatestHash(output.hash);
}

int32_t verifyChainSegment(const ChainRecord* records, size_t count) {
    if (records == nullptr) {
        return count == 0 ? -1 : 0;
    }
    for (size_t index = 0; index < count; ++index) {
        char calculatedHash[HASH_HEX_LEN + 1];
        if (!computeHash(serializeRecord(records[index]), calculatedHash, sizeof(calculatedHash)) || strcmp(calculatedHash, records[index].hash) != 0) {
            return static_cast<int32_t>(index);
        }
        if (index > 0 && strcmp(records[index].previousHash, records[index - 1].hash) != 0) {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

bool loadLatestHash(char* output, size_t outputSize) {
    if (output == nullptr || outputSize < HASH_HEX_LEN + 1) {
        return false;
    }
    Preferences preferences;
    if (!preferences.begin(HASH_NVS_NAMESPACE, true)) {
        return false;
    }
    size_t copied = preferences.getString(HASH_NVS_LATEST_KEY, output, outputSize);
    preferences.end();
    return copied == HASH_HEX_LEN;
}

bool storeLatestHash(const char* hash) {
    if (hash == nullptr || strlen(hash) != HASH_HEX_LEN) {
        return false;
    }
    Preferences preferences;
    if (!preferences.begin(HASH_NVS_NAMESPACE, false)) {
        return false;
    }
    bool stored = preferences.putString(HASH_NVS_LATEST_KEY, hash) > 0;
    preferences.end();
    return stored;
}

uint32_t loadDropCount() {
    Preferences preferences;
    if (!preferences.begin(HASH_NVS_NAMESPACE, true)) {
        return 0;
    }
    uint32_t drops = preferences.getUInt(HASH_NVS_DROP_KEY, 0);
    preferences.end();
    return drops;
}

uint32_t incrementDropCount() {
    Preferences preferences;
    if (!preferences.begin(HASH_NVS_NAMESPACE, false)) {
        return 0;
    }
    uint32_t drops = preferences.getUInt(HASH_NVS_DROP_KEY, 0) + 1;
    preferences.putUInt(HASH_NVS_DROP_KEY, drops);
    preferences.end();
    return drops;
}

uint32_t getDropCount() { return loadDropCount(); }

uint32_t getQueueDropCount() {
    portENTER_CRITICAL(&hashchainDropMux);
    uint32_t drops = hashchainQueueDrops;
    portEXIT_CRITICAL(&hashchainDropMux);
    return drops;
}

bool recoverLatestHash(char* output, size_t outputSize) {
    if (output == nullptr || outputSize < HASH_HEX_LEN + 1) {
        return false;
    }
    ChainRecord records[CHAIN_VERIFY_DEFAULT_N];
    size_t count = storage_module::readLastNRecords(CHAIN_VERIFY_DEFAULT_N, records, CHAIN_VERIFY_DEFAULT_N);
    if (count == 0) {
        return false;
    }
    for (size_t index = count; index > 0; --index) {
        ChainRecord& record = records[index - 1];
        char calculatedHash[HASH_HEX_LEN + 1];
        if (computeHash(serializeRecord(record), calculatedHash, sizeof(calculatedHash)) && strcmp(calculatedHash, record.hash) == 0) {
            snprintf(output, outputSize, "%s", record.hash);
            return true;
        }
    }
    return false;
}

void startHashchainTask(QueueHandle_t inputQueue, QueueHandle_t outputQueue) {
    QueueHandle_t* queues = static_cast<QueueHandle_t*>(pvPortMalloc(2 * sizeof(QueueHandle_t)));
    if (queues == nullptr) {
        return;
    }
    queues[0] = inputQueue;
    queues[1] = outputQueue;
    if (xTaskCreate(hashchainTask, "hashchain", STACK_SIZE_DEFAULT, queues, PRIORITY_HASHCHAIN, nullptr) != pdPASS) {
        vPortFree(queues);
    }
}

void hashchainTask(void* parameter) {
    QueueHandle_t* queues = static_cast<QueueHandle_t*>(parameter);
    QueueHandle_t sourceQueue = queues[0];
    QueueHandle_t destinationQueue = queues[1];
    vPortFree(queues);
    char latestHash[HASH_HEX_LEN + 1]{};
    bool storageAvailableAtBoot = storage_module::isReady();
    bool loadedFromStorage = storageAvailableAtBoot && recoverLatestHash(latestHash, sizeof(latestHash));
    bool loadedFromNvs = storageAvailableAtBoot ? loadedFromStorage : loadLatestHash(latestHash, sizeof(latestHash));
    if (loadedFromStorage) {
        storeLatestHash(latestHash);
    }
    if (!loadedFromNvs) {
        String genesis = String(GENESIS_PREFIX) + DEVICE_ID;
        if (!computeHash(genesis, latestHash, sizeof(latestHash))) {
            vTaskDelete(nullptr);
        }
        storeLatestHash(latestHash);
    }
    ProovRecord input{};
    uint32_t lastStackReport = millis();
    for (;;) {
        boot_monitor_module::feedWatchdog();
        boot_monitor_module::reportTaskStack("hashchain", lastStackReport);
        if (xQueueReceive(sourceQueue, &input, pdMS_TO_TICKS(SAMPLE_INTERVAL_MS)) == pdTRUE) {
            ChainRecord output{};
            char previousHash[HASH_HEX_LEN + 1];
            snprintf(previousHash, sizeof(previousHash), "%s", latestHash);
            if (!appendToChain(input, previousHash, output)) {
                uint32_t drops = incrementDropCount();
                Serial.printf("hashchain error drops=%lu\n", static_cast<unsigned long>(drops));
                storeLatestHash(previousHash);
                continue;
            }
            if (xQueueSend(destinationQueue, &output, 0) == pdPASS) {
                snprintf(latestHash, sizeof(latestHash), "%s", output.hash);
            } else {
                storeLatestHash(previousHash);
                portENTER_CRITICAL(&hashchainDropMux);
                ++hashchainQueueDrops;
                portEXIT_CRITICAL(&hashchainDropMux);
                uint32_t drops = incrementDropCount();
                Serial.printf("hashchain queue full drops=%lu\n", static_cast<unsigned long>(drops));
            }
        }
    }
}
}
