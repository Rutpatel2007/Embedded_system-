#ifndef PROOV_STORAGE_H
#define PROOV_STORAGE_H

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
bool appendRecordToFile(const ChainRecord& record);
size_t readLastNRecords(size_t count, ChainRecord* records, size_t capacity);
bool tamperTodayLine(uint32_t lineNumber);
uint32_t countTodayRecords();
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
bool recoverPartialTodayTail();
#endif
void registerStorageCommands();
void startStorageTask(QueueHandle_t inputQueue);
void storageTask(void* parameter);
}

#endif
