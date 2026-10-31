#ifndef TRUESENSE_STORAGE_H
#define TRUESENSE_STORAGE_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "hashchain.h"

struct SyncCursor {
    uint32_t fileDate;
    uint32_t lineIndex;
};

#ifdef TEST_MODE
struct TestRecordSummary {
    uint32_t records;
    uint32_t normal;
    uint32_t plausible;
    uint32_t suspicious;
    uint32_t fingerprintMismatch;
};
#endif

namespace storage_module {
bool init();
bool isReady();
bool isSdReady();
const char* backendName();
bool archiveLogFiles();
bool lastVerifyPassed();
bool hasLastVerify();
int32_t lastVerifyFailureIndex();
void clearVerifyState();
bool appendRecordToFile(const ChainRecord& record);
size_t readLastNRecords(size_t count, ChainRecord* records, size_t capacity);
bool verifyAllRecords(uint32_t& recordCount, int32_t& failingIndex);
bool tamperTodayLine(uint32_t lineNumber);
uint32_t countTodayRecords();
uint32_t cachedTodayRecordCount();
bool getTodayFilePath(char* output, size_t outputSize);
bool getOldestLogCursor(SyncCursor& cursor);
bool readUnsyncedBatch(const SyncCursor& cursor, ChainRecord* records, size_t capacity, size_t& count, SyncCursor& nextCursor);
uint32_t countUnsyncedRecords(const SyncCursor& cursor);
#ifdef TEST_MODE
uint32_t countTodayLines();
bool summarizeTodayFromLine(uint32_t firstLine, TestRecordSummary& summary);
bool verifyTodayFile(uint32_t& failingIndex, uint32_t& recordCount);
bool backupTodayFile();
bool restoreTodayFile();
#endif
bool recoverPartialTodayTail();
void registerStorageCommands();
void startStorageTask(QueueHandle_t inputQueue);
void storageTask(void* parameter);
}

#endif
