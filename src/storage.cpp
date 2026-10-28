#include "storage.h"
#include "config.h"
#include "hashchain.h"
#include "rtc_time.h"
#include "serial_cmd.h"
#include "boot_monitor.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

enum StorageCommandType : uint8_t {
    STORAGE_COMMAND_VERIFY,
    STORAGE_COMMAND_TAMPER,
    STORAGE_COMMAND_CHAIN
};

struct StorageCommand {
    StorageCommandType type;
    uint32_t lineNumber;
};

static QueueHandle_t storageCommandQueue;
static SemaphoreHandle_t storageMutex;

static bool takeStorageLock() {
    return storageMutex != nullptr && xSemaphoreTake(storageMutex, portMAX_DELAY) == pdTRUE;
}

static void giveStorageLock() {
    xSemaphoreGive(storageMutex);
}

static void enqueueStorageCommand(StorageCommand request) {
    if (storageCommandQueue == nullptr || xQueueSend(storageCommandQueue, &request, 0) != pdPASS) {
        Serial.println(SERIAL_HANDLER_QUEUE_FULL);
    }
}

static void handleVerifyCommand(const char* args) {
    if (args[0] != '\0') {
        Serial.println("VERIFY takes no arguments");
        return;
    }
    enqueueStorageCommand({STORAGE_COMMAND_VERIFY, 0});
}

static void handleTamperCommand(const char* args) {
    char* end = nullptr;
    unsigned long lineNumber = strtoul(args, &end, 10);
    if (args[0] == '\0' || end == args || *end != '\0' || lineNumber == 0) {
        Serial.println("Usage: TAMPER <line>");
        return;
    }
    enqueueStorageCommand({STORAGE_COMMAND_TAMPER, static_cast<uint32_t>(lineNumber)});
}

static void handleChainCommand(const char* args) {
    if (args[0] != '\0') {
        Serial.println("CHAIN takes no arguments");
        return;
    }
    enqueueStorageCommand({STORAGE_COMMAND_CHAIN, 0});
}

static bool mountCard() {
    bool mounted = SD.begin(MICROSD_CS_PIN, SPI, SD_FREQUENCY_HZ);
    if (mounted && !SD.exists(LOG_DIR)) {
        mounted = SD.mkdir(LOG_DIR);
    }
    return mounted;
}

static bool parseRecord(const String& line, ChainRecord& record) {
    JsonDocument document;
    if (deserializeJson(document, line) != DeserializationError::Ok) {
        return false;
    }
    snprintf(record.deviceId, sizeof(record.deviceId), "%s", document["device_id"] | "");
    record.timestamp = document["timestamp"] | 0U;
    record.gasPpmX100 = document["gas_ppm_x100"] | 0U;
    record.powerMw = document["power_mw"] | 0;
    record.plausibilityFlag = document["plausibility_flag"] | 0;
    record.fingerprintStatus = document["fingerprint_status"] | 0;
    snprintf(record.previousHash, sizeof(record.previousHash), "%s", document["previous_hash"] | "");
    snprintf(record.hash, sizeof(record.hash), "%s", document["hash"] | "");
    return strlen(record.deviceId) > 0 && strlen(record.previousHash) == HASH_HEX_LEN && strlen(record.hash) == HASH_HEX_LEN;
}

static bool getLogFileInfo(File& file, uint32_t& fileDate, char* path, size_t pathSize) {
    String name = file.name();
    const char* baseName = strrchr(name.c_str(), '/');
    baseName = baseName == nullptr ? name.c_str() : baseName + 1;
    if (strlen(baseName) != 15 || strcmp(baseName + 8, ".ndjson") != 0) {
        return false;
    }
    for (size_t index = 0; index < 8; ++index) {
        if (!isdigit(static_cast<unsigned char>(baseName[index]))) {
            return false;
        }
    }
    fileDate = static_cast<uint32_t>(strtoul(baseName, nullptr, 10));
    int written = snprintf(path, pathSize, LOG_DIR "/%s", baseName);
    return fileDate > 0 && written > 0 && static_cast<size_t>(written) < pathSize;
}

static bool findNextLogFile(uint32_t minimumDate, bool inclusive, uint32_t& selectedDate, char* selectedPath, size_t pathSize) {
    File directory = SD.open(LOG_DIR);
    if (!directory || !directory.isDirectory()) {
        return false;
    }
    bool found = false;
    File entry = directory.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            uint32_t fileDate;
            char path[STORAGE_PATH_BUFFER_SIZE];
            if (getLogFileInfo(entry, fileDate, path, sizeof(path)) && (inclusive ? fileDate >= minimumDate : fileDate > minimumDate) && (!found || fileDate < selectedDate)) {
                found = snprintf(selectedPath, pathSize, "%s", path) > 0 && strlen(path) < pathSize;
                selectedDate = fileDate;
            }
        }
        entry.close();
        entry = directory.openNextFile();
    }
    directory.close();
    return found;
}

static bool serializeJsonRecord(const ChainRecord& record, File& file) {
    JsonDocument document;
    document["device_id"] = record.deviceId;
    document["timestamp"] = record.timestamp;
    document["gas_ppm_x100"] = record.gasPpmX100;
    document["power_mw"] = record.powerMw;
    document["plausibility_flag"] = record.plausibilityFlag;
    document["fingerprint_status"] = record.fingerprintStatus;
    document["previous_hash"] = record.previousHash;
    document["hash"] = record.hash;
    return serializeJson(document, file) > 0;
}

static void sortRecords(ChainRecord* records, size_t count) {
    for (size_t index = 1; index < count; ++index) {
        ChainRecord item = records[index];
        size_t position = index;
        while (position > 0 && records[position - 1].timestamp > item.timestamp) {
            records[position] = records[position - 1];
            --position;
        }
        records[position] = item;
    }
}

static void insertRecentRecord(ChainRecord* records, size_t& count, size_t capacity, const ChainRecord& record) {
    if (capacity == 0) {
        return;
    }
    if (count < capacity) {
        records[count++] = record;
    } else if (record.timestamp > records[count - 1].timestamp) {
        records[count - 1] = record;
    } else {
        return;
    }
    sortRecords(records, count);
}

static bool writeTamperedLine(const String& line, File& output) {
    JsonDocument document;
    if (deserializeJson(document, line) != DeserializationError::Ok) {
        return false;
    }
    uint32_t value = document["gas_ppm_x100"] | 0U;
    document["gas_ppm_x100"] = value + 1U;
    return serializeJson(document, output) > 0 && output.println() > 0;
}

namespace storage_module {
bool init() {
    storageMutex = xSemaphoreCreateMutex();
    if (storageMutex == nullptr) {
        return false;
    }
    storageCommandQueue = xQueueCreate(SERIAL_REQUEST_QUEUE_LENGTH, sizeof(StorageCommand));
    SPI.begin(MICROSD_SCK_PIN, MICROSD_MISO_PIN, MICROSD_MOSI_PIN, MICROSD_CS_PIN);
    return mountCard();
}

bool isReady() {
    if (!takeStorageLock()) {
        return false;
    }
    bool ready = SD.cardType() != CARD_NONE;
    giveStorageLock();
    return ready;
}

bool getTodayFilePath(char* output, size_t outputSize) {
    if (output == nullptr || outputSize == 0) {
        return false;
    }
    time_t epoch = static_cast<time_t>(rtc_time_module::getTimestamp());
    struct tm dateParts;
    if (gmtime_r(&epoch, &dateParts) == nullptr) {
        return false;
    }
    int written = snprintf(output, outputSize, LOG_DIR "/%04d%02d%02d.ndjson", dateParts.tm_year + 1900, dateParts.tm_mon + 1, dateParts.tm_mday);
    return written > 0 && static_cast<size_t>(written) < outputSize;
}

bool appendRecordToFile(const ChainRecord& record) {
    if (!takeStorageLock()) {
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        giveStorageLock();
        return false;
    }
    char path[STORAGE_PATH_BUFFER_SIZE];
    if (!getTodayFilePath(path, sizeof(path))) {
        giveStorageLock();
        return false;
    }
    File file = SD.open(path, FILE_APPEND);
    if (!file) {
        giveStorageLock();
        return false;
    }
    bool written = serializeJsonRecord(record, file) && file.println() > 0;
    file.flush();
    file.close();
    giveStorageLock();
    return written;
}

size_t readLastNRecords(size_t count, ChainRecord* records, size_t capacity) {
    if (records == nullptr || count == 0 || capacity == 0 || !takeStorageLock()) {
        return 0;
    }
    if (SD.cardType() == CARD_NONE) {
        giveStorageLock();
        return 0;
    }
    size_t limit = count < capacity ? count : capacity;
    size_t found = 0;
    File directory = SD.open(LOG_DIR);
    if (!directory || !directory.isDirectory()) {
        giveStorageLock();
        return 0;
    }
    File file = directory.openNextFile();
    while (file) {
        String name = file.name();
        if (!file.isDirectory() && name.endsWith(".ndjson")) {
            while (file.available()) {
                String line = file.readStringUntil('\n');
                ChainRecord record{};
                if (parseRecord(line, record)) {
                    insertRecentRecord(records, found, limit, record);
                }
            }
        }
        file.close();
        file = directory.openNextFile();
    }
    directory.close();
    giveStorageLock();
    return found;
}

bool tamperTodayLine(uint32_t lineNumber) {
    if (lineNumber == 0 || !takeStorageLock()) {
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        giveStorageLock();
        return false;
    }
    char todayPath[STORAGE_PATH_BUFFER_SIZE];
    if (!getTodayFilePath(todayPath, sizeof(todayPath)) || !SD.exists(todayPath)) {
        giveStorageLock();
        return false;
    }
    File input = SD.open(todayPath, FILE_READ);
    File output = SD.open(STORAGE_TEMP_PATH, FILE_WRITE);
    if (!input || !output) {
        if (input) input.close();
        if (output) output.close();
        SD.remove(STORAGE_TEMP_PATH);
        giveStorageLock();
        return false;
    }
    uint32_t currentLine = 0;
    bool changed = false;
    while (input.available()) {
        String line = input.readStringUntil('\n');
        ++currentLine;
        bool lineWritten;
        if (currentLine == lineNumber) {
            lineWritten = writeTamperedLine(line, output);
            changed = lineWritten;
        } else {
            lineWritten = output.println(line) > 0;
        }
        if (!lineWritten) {
            changed = false;
            break;
        }
    }
    input.close();
    output.flush();
    output.close();
    if (!changed) {
        SD.remove(STORAGE_TEMP_PATH);
        giveStorageLock();
        return false;
    }
    SD.remove(todayPath);
    bool renamed = SD.rename(STORAGE_TEMP_PATH, todayPath);
    giveStorageLock();
    return renamed;
}

uint32_t countTodayRecords() {
    char path[STORAGE_PATH_BUFFER_SIZE];
    if (!takeStorageLock()) {
        return 0;
    }
    if (SD.cardType() == CARD_NONE || !getTodayFilePath(path, sizeof(path))) {
        giveStorageLock();
        return 0;
    }
    File file = SD.open(path, FILE_READ);
    if (!file) {
        giveStorageLock();
        return 0;
    }
    uint32_t count = 0;
    while (file.available()) {
        file.readStringUntil('\n');
        ++count;
    }
    file.close();
    giveStorageLock();
    return count;
}

bool getOldestLogCursor(SyncCursor& cursor) {
    if (!takeStorageLock()) {
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        giveStorageLock();
        return false;
    }
    uint32_t date = 0;
    char path[STORAGE_PATH_BUFFER_SIZE];
    bool found = findNextLogFile(0, true, date, path, sizeof(path));
    if (found) {
        cursor.fileDate = date;
        cursor.lineIndex = SYNC_CURSOR_FIRST_LINE;
    }
    giveStorageLock();
    return found;
}

bool readUnsyncedBatch(const SyncCursor& cursor, ChainRecord* records, size_t capacity, size_t& count, SyncCursor& nextCursor) {
    count = 0;
    nextCursor = cursor;
    if (records == nullptr || capacity == 0 || !takeStorageLock()) {
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        giveStorageLock();
        return false;
    }
    uint32_t searchDate = cursor.fileDate;
    bool inclusive = true;
    uint32_t fileDate = 0;
    char path[STORAGE_PATH_BUFFER_SIZE];
    bool found = findNextLogFile(searchDate, inclusive, fileDate, path, sizeof(path));
    bool firstFile = true;
    while (found) {
        File file = SD.open(path, FILE_READ);
        if (!file) {
            giveStorageLock();
            return false;
        }
        uint32_t physicalLine = 0;
        while (file.available()) {
            if (count >= capacity) {
                nextCursor.fileDate = fileDate;
                nextCursor.lineIndex = physicalLine + 1;
                file.close();
                giveStorageLock();
                return true;
            }
            String line = file.readStringUntil('\n');
            ++physicalLine;
            if (firstFile && cursor.fileDate != 0 && fileDate == cursor.fileDate && physicalLine < cursor.lineIndex) {
                continue;
            }
            if (!parseRecord(line, records[count])) {
                file.close();
                giveStorageLock();
                return false;
            }
            ++count;
        }
        file.close();
        nextCursor.fileDate = fileDate;
        nextCursor.lineIndex = physicalLine + 1;
        searchDate = fileDate;
        inclusive = false;
        firstFile = false;
        found = findNextLogFile(searchDate, inclusive, fileDate, path, sizeof(path));
        if (found && count == capacity) {
            nextCursor.fileDate = fileDate;
            nextCursor.lineIndex = SYNC_CURSOR_FIRST_LINE;
            giveStorageLock();
            return true;
        }
    }
    if (count == 0) {
        nextCursor = cursor;
    }
    giveStorageLock();
    return true;
}

uint32_t countUnsyncedRecords(const SyncCursor& cursor) {
    if (!takeStorageLock()) {
        return 0;
    }
    if (SD.cardType() == CARD_NONE) {
        giveStorageLock();
        return 0;
    }
    uint32_t total = 0;
    uint32_t searchDate = cursor.fileDate;
    bool inclusive = true;
    uint32_t fileDate = 0;
    char path[STORAGE_PATH_BUFFER_SIZE];
    bool found = findNextLogFile(searchDate, inclusive, fileDate, path, sizeof(path));
    bool firstFile = true;
    while (found) {
        File file = SD.open(path, FILE_READ);
        if (!file) {
            break;
        }
        uint32_t lineNumber = 0;
        while (file.available()) {
            file.readStringUntil('\n');
            ++lineNumber;
            if (!firstFile || cursor.fileDate == 0 || fileDate != cursor.fileDate || lineNumber >= cursor.lineIndex) {
                ++total;
            }
        }
        file.close();
        searchDate = fileDate;
        inclusive = false;
        firstFile = false;
        found = findNextLogFile(searchDate, inclusive, fileDate, path, sizeof(path));
    }
    giveStorageLock();
    return total;
}

static void verifyLastRecords() {
    ChainRecord records[CHAIN_VERIFY_DEFAULT_N];
    size_t count = readLastNRecords(CHAIN_VERIFY_DEFAULT_N, records, CHAIN_VERIFY_DEFAULT_N);
    int32_t failureIndex = hashchain_module::verifyChainSegment(records, count);
    if (failureIndex < 0 && count > 0) {
        Serial.printf("VERIFY PASS records=%u\n", static_cast<unsigned int>(count));
    } else {
        Serial.printf("VERIFY FAIL index=%ld records=%u\n", static_cast<long>(failureIndex < 0 ? 0 : failureIndex), static_cast<unsigned int>(count));
    }
}

static void processStorageRequest(const StorageCommand& request) {
    if (request.type == STORAGE_COMMAND_VERIFY) {
        verifyLastRecords();
    } else if (request.type == STORAGE_COMMAND_TAMPER) {
        Serial.println(tamperTodayLine(request.lineNumber) ? "TAMPER complete" : "TAMPER failed");
    } else if (request.type == STORAGE_COMMAND_CHAIN) {
        char latestHash[HASH_HEX_LEN + 1]{};
        bool hasHash = hashchain_module::loadLatestHash(latestHash, sizeof(latestHash));
        Serial.printf("CHAIN last_hash=%s records_today=%lu drops=%lu\n", hasHash ? latestHash : "", static_cast<unsigned long>(countTodayRecords()), static_cast<unsigned long>(hashchain_module::loadDropCount()));
    }
}

void registerStorageCommands() {
    serial_cmd_module::registerCommand("VERIFY", handleVerifyCommand);
    serial_cmd_module::registerCommand("TAMPER", handleTamperCommand);
    serial_cmd_module::registerCommand("CHAIN", handleChainCommand);
}

void startStorageTask(QueueHandle_t inputQueue) {
    xTaskCreate(storageTask, "sdwrite", STACK_SIZE_DEFAULT, inputQueue, PRIORITY_SDWRITE, nullptr);
}

void storageTask(void* parameter) {
    QueueHandle_t recordQueue = static_cast<QueueHandle_t>(parameter);
    ChainRecord pendingRecord{};
    bool pendingRecordValid = false;
    uint32_t lastMountAttempt = millis() - SD_RETRY_MS;
    uint32_t lastStackReport = millis();
    for (;;) {
        boot_monitor_module::feedWatchdog();
        boot_monitor_module::reportTaskStack("sdwrite", lastStackReport);
        if (!isReady() && millis() - lastMountAttempt >= SD_RETRY_MS) {
            lastMountAttempt = millis();
            if (takeStorageLock()) {
                mountCard();
                giveStorageLock();
            }
        }
        StorageCommand request;
        if (xQueueReceive(storageCommandQueue, &request, 0) == pdTRUE) {
            processStorageRequest(request);
        }
        if (!pendingRecordValid && xQueueReceive(recordQueue, &pendingRecord, pdMS_TO_TICKS(STORAGE_POLL_MS)) == pdTRUE) {
            pendingRecordValid = true;
        }
        if (pendingRecordValid && appendRecordToFile(pendingRecord)) {
            pendingRecordValid = false;
        } else if (pendingRecordValid) {
            Serial.println("sdwrite retry pending");
            vTaskDelay(pdMS_TO_TICKS(STORAGE_POLL_MS));
        }
    }
}
}
