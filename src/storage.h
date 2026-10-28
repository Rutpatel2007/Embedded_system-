#ifndef PROOV_STORAGE_H
#define PROOV_STORAGE_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "hashchain.h"

struct SyncCursor {
    uint32_t fileDate;
    uint32_t lineIndex;
};

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
void registerStorageCommands();
void startStorageTask(QueueHandle_t inputQueue);
void storageTask(void* parameter);
}

#endif
