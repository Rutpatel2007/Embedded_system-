#ifndef TRUESENSE_HASHCHAIN_H
#define TRUESENSE_HASHCHAIN_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "sensor_task.h"
#include "config.h"

struct ChainRecord {
    char deviceId[24];
    uint32_t timestamp;
    uint32_t gasPpmX100;
    int32_t powerMw;
    int32_t plausibilityFlag;
    int32_t fingerprintStatus;
    char previousHash[HASH_HEX_LEN + 1];
    char hash[HASH_HEX_LEN + 1];
};

namespace hashchain_module {
void init();
String serializeRecord(const ChainRecord& record);
bool computeHash(const String& serializedRecord, char* output, size_t outputSize);
bool appendToChain(const TrueSenseRecord& input, const char* previousHash, ChainRecord& output);
int32_t verifyChainSegment(const ChainRecord* records, size_t count);
bool loadLatestHash(char* output, size_t outputSize);
bool storeLatestHash(const char* hash);
uint32_t loadDropCount();
uint32_t incrementDropCount();
uint32_t getDropCount();
uint32_t getQueueDropCount();
bool recoverLatestHash(char* output, size_t outputSize);
bool resetChain();
bool latestPointerInconsistent();
bool prepareRuntimeReset();
bool finishRuntimeReset(bool commit);
void startHashchainTask(QueueHandle_t inputQueue, QueueHandle_t outputQueue);
void hashchainTask(void* parameter);
}

#endif
