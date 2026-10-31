#include "storage.h"
#include "config.h"
#include "hashchain.h"
#include "rtc_time.h"
#include "serial_cmd.h"
#include "boot_monitor.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <LittleFS.h>
#include <SPI.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

enum StorageCommandType : uint8_t {
    STORAGE_COMMAND_VERIFY,
    STORAGE_COMMAND_TAMPER,
    STORAGE_COMMAND_CHAIN,
    STORAGE_COMMAND_CHAIN_RESET,
    STORAGE_COMMAND_FS_FORMAT
};

struct StorageCommand {
    StorageCommandType type;
    uint32_t lineNumber;
};

static QueueHandle_t storageCommandQueue;
static SemaphoreHandle_t storageMutex;
static bool sdMounted;
static bool storageMounted;
static FS* activeFileSystem;
static uint8_t activeBackend;
static bool storageVerifyAvailable;
static bool storageVerifyPassed;
static int32_t storageVerifyFailure = -1;
static uint32_t todayRecordsCached;
static bool storageFullReported;

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
    if (strcasecmp(args, "RESET") == 0) {
        enqueueStorageCommand({STORAGE_COMMAND_CHAIN_RESET, 0});
        return;
    }
    if (args[0] != '\0') {
        Serial.println("CHAIN [RESET]");
        return;
    }
    enqueueStorageCommand({STORAGE_COMMAND_CHAIN, 0});
}

static void handleFsCommand(const char* args) {
    if (strcasecmp(args, "FORMAT") != 0) { Serial.println("Usage: FS FORMAT"); return; }
    enqueueStorageCommand({STORAGE_COMMAND_FS_FORMAT, 0});
}

static bool mountSd() {
    sdMounted = false;
    SD.end();
    bool mounted = SD.begin(MICROSD_CS_PIN, SPI, SD_FREQUENCY_HZ);
#if SD_FORMAT_ON_FAIL
    if (!mounted) {
        bool formatted = SD.begin(MICROSD_CS_PIN, SPI, SD_FREQUENCY_HZ, "/sd", 5, true);
        Serial.printf("SD FORMAT ATTEMPTED result=%u\n", static_cast<unsigned int>(formatted));
        mounted = formatted;
    }
#endif
    if (mounted && !activeFileSystem->exists(LOG_DIR)) {
        mounted = activeFileSystem->mkdir(LOG_DIR);
    }
    sdMounted = mounted;
    if (!sdMounted) SD.end();
    return mounted;
}

static bool mountCard() {
    storageMounted = false;
    activeFileSystem = nullptr;
    sdMounted = false;
#if STORAGE_BACKEND != 2
    activeFileSystem = &SD;
    if (mountSd()) {
        activeBackend = 1;
        storageMounted = true;
        return true;
    }
#endif
#if STORAGE_BACKEND != 1
    bool mounted = LittleFS.begin(false);
    if (!mounted) {
        mounted = LittleFS.begin(true);
        if (mounted) Serial.println("LITTLEFS FORMATTED");
    } else {
        Serial.println("LITTLEFS MOUNT OK");
    }
    if (mounted) {
        activeFileSystem = &LittleFS;
        storageMounted = true;
        if (!LittleFS.exists(LOG_DIR) && !LittleFS.mkdir(LOG_DIR)) {
            storageMounted = false;
            LittleFS.end();
            activeFileSystem = nullptr;
            Serial.println("LITTLEFS FAILED");
            return false;
        }
        activeBackend = 2;
        storageMounted = true;
        return true;
    }
#endif
    activeFileSystem = nullptr;
    storageMounted = false;
#if STORAGE_BACKEND != 1
    Serial.println("LITTLEFS FAILED");
#endif
    return false;
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
    if (!storageMounted) return false;
    File directory = activeFileSystem->open(LOG_DIR);
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
    if (!takeStorageLock()) return false;
    bool mounted = mountCard();
    giveStorageLock();
    if (mounted) todayRecordsCached = countTodayRecords();
    return mounted;
}

bool isReady() {
    if (!takeStorageLock()) {
        return false;
    }
    bool ready = storageMounted;
    giveStorageLock();
    return ready;
}

bool isSdReady() { return storageMounted && sdMounted; }
const char* backendName() { return activeBackend == 2 ? "flash" : activeBackend == 1 ? "sd" : "unavailable"; }

bool lastVerifyPassed() { return storageVerifyAvailable && storageVerifyPassed; }
bool hasLastVerify() { return storageVerifyAvailable; }
int32_t lastVerifyFailureIndex() { return storageVerifyAvailable ? storageVerifyFailure : -1; }
void clearVerifyState() { storageVerifyAvailable = false; storageVerifyPassed = false; storageVerifyFailure = -1; }
uint32_t cachedTodayRecordCount() { return todayRecordsCached; }

bool archiveLogFiles() {
    if (!takeStorageLock()) return false;
    if (!storageMounted) { giveStorageLock(); return false; }
    if (activeBackend == 2) {
        uint32_t totalBytes = LittleFS.totalBytes();
        uint32_t usedBytes = LittleFS.usedBytes();
        if (totalBytes > 0 && static_cast<uint64_t>(usedBytes) * 100U > static_cast<uint64_t>(totalBytes) * 90U) {
            String archiveDirectory = String(LOG_DIR) + "/old";
            File archived = activeFileSystem->open(archiveDirectory);
            String oldestPath;
            if (archived && archived.isDirectory()) {
                File entry = archived.openNextFile();
                while (entry) {
                    if (!entry.isDirectory()) {
                        String path = entry.name();
                        if (oldestPath.isEmpty() || path < oldestPath) oldestPath = path;
                    }
                    entry.close();
                    entry = archived.openNextFile();
                }
                archived.close();
            } else if (archived) {
                archived.close();
            }
            if (!oldestPath.isEmpty()) activeFileSystem->remove(oldestPath);
            usedBytes = LittleFS.usedBytes();
            if (static_cast<uint64_t>(usedBytes) * 100U > static_cast<uint64_t>(totalBytes) * 90U) {
                if (!storageFullReported) {
                    Serial.println("STORAGE FULL");
                    storageFullReported = true;
                }
                giveStorageLock();
                return false;
            }
        }
    }
    String oldDirectory = String(LOG_DIR) + "/old";
    if (!activeFileSystem->exists(oldDirectory) && !activeFileSystem->mkdir(oldDirectory)) {
        giveStorageLock();
        return false;
    }
    File directory = activeFileSystem->open(LOG_DIR);
    if (!directory || !directory.isDirectory()) {
        if (directory) directory.close();
        giveStorageLock();
        return false;
    }
    bool success = true;
    File entry = directory.openNextFile();
    while (entry) {
        String name = entry.name();
        bool eligible = !entry.isDirectory() && name.endsWith(".ndjson");
        entry.close();
        if (eligible) {
            const char* baseName = strrchr(name.c_str(), '/');
            baseName = baseName == nullptr ? name.c_str() : baseName + 1;
            String destination = oldDirectory + "/" + baseName;
            if (!activeFileSystem->rename(name, destination)) success = false;
        }
        entry = directory.openNextFile();
    }
    directory.close();
    if (success) todayRecordsCached = 0;
    giveStorageLock();
    return success;
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
    if (!storageMounted) {
        giveStorageLock();
        return false;
    }
    if (activeBackend == 2) {
        uint32_t total = LittleFS.totalBytes();
        uint32_t used = LittleFS.usedBytes();
        if (total == 0 || static_cast<uint64_t>(used) * 100U >= static_cast<uint64_t>(total) * 90U) {
            if (!storageFullReported) { Serial.println("STORAGE FULL"); storageFullReported = true; }
            giveStorageLock();
            return false;
        }
    }
    char path[STORAGE_PATH_BUFFER_SIZE];
    if (!getTodayFilePath(path, sizeof(path))) {
        giveStorageLock();
        return false;
    }
    File file = activeFileSystem->open(path, FILE_APPEND);
    if (!file) {
        giveStorageLock();
        return false;
    }
    bool written = serializeJsonRecord(record, file) && file.println() > 0;
    file.flush();
    file.close();
    if (written) ++todayRecordsCached;
    giveStorageLock();
    return written;
}

size_t readLastNRecords(size_t count, ChainRecord* records, size_t capacity) {
    if (records == nullptr || count == 0 || capacity == 0 || !takeStorageLock()) {
        return 0;
    }
    if (!storageMounted) {
        giveStorageLock();
        return 0;
    }
    size_t limit = count < capacity ? count : capacity;
    size_t found = 0;
    File directory = activeFileSystem->open(LOG_DIR);
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

bool verifyAllRecords(uint32_t& recordCount, int32_t& failingIndex) {
    recordCount = 0;
    failingIndex = -1;
    if (!takeStorageLock()) return false;
    if (!storageMounted) {
        giveStorageLock();
        return false;
    }
    String genesis = String(GENESIS_PREFIX) + DEVICE_ID;
    char expectedGenesis[HASH_HEX_LEN + 1]{};
    if (!hashchain_module::computeHash(genesis, expectedGenesis, sizeof(expectedGenesis))) {
        giveStorageLock();
        return false;
    }
    char previousHash[HASH_HEX_LEN + 1]{};
    bool hasPrevious = false;
    uint32_t searchDate = 0;
    bool inclusive = true;
    uint32_t fileDate = 0;
    char path[STORAGE_PATH_BUFFER_SIZE];
    bool found = findNextLogFile(searchDate, inclusive, fileDate, path, sizeof(path));
    while (found) {
        File file = activeFileSystem->open(path, FILE_READ);
        if (!file) {
            failingIndex = static_cast<int32_t>(recordCount + 1U);
            giveStorageLock();
            return false;
        }
        while (file.available()) {
            String line = file.readStringUntil('\n');
            ChainRecord record{};
            if (!parseRecord(line, record) || hashchain_module::verifyChainSegment(&record, 1) != -1 || (hasPrevious && strcmp(record.previousHash, previousHash) != 0) || (!hasPrevious && strcmp(record.previousHash, expectedGenesis) != 0)) {
                failingIndex = static_cast<int32_t>(recordCount + 1U);
                file.close();
                giveStorageLock();
                return true;
            }
            snprintf(previousHash, sizeof(previousHash), "%s", record.hash);
            hasPrevious = true;
            ++recordCount;
            vTaskDelay(pdMS_TO_TICKS(STORAGE_VERIFY_YIELD_MS));
        }
        file.close();
        searchDate = fileDate;
        inclusive = false;
        found = findNextLogFile(searchDate, inclusive, fileDate, path, sizeof(path));
    }
    giveStorageLock();
    return true;
}

bool tamperTodayLine(uint32_t lineNumber) {
    if (lineNumber == 0 || !takeStorageLock()) {
        return false;
    }
    if (!storageMounted) {
        giveStorageLock();
        return false;
    }
    char todayPath[STORAGE_PATH_BUFFER_SIZE];
    if (!getTodayFilePath(todayPath, sizeof(todayPath)) || !activeFileSystem->exists(todayPath)) {
        giveStorageLock();
        return false;
    }
    File input = activeFileSystem->open(todayPath, FILE_READ);
    File output = activeFileSystem->open(STORAGE_TEMP_PATH, FILE_WRITE);
    if (!input || !output) {
        if (input) input.close();
        if (output) output.close();
        activeFileSystem->remove(STORAGE_TEMP_PATH);
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
        activeFileSystem->remove(STORAGE_TEMP_PATH);
        giveStorageLock();
        return false;
    }
    activeFileSystem->remove(todayPath);
    bool renamed = activeFileSystem->rename(STORAGE_TEMP_PATH, todayPath);
    giveStorageLock();
    return renamed;
}

uint32_t countTodayRecords() {
    char path[STORAGE_PATH_BUFFER_SIZE];
    if (!takeStorageLock()) {
        return 0;
    }
    if (!storageMounted || !getTodayFilePath(path, sizeof(path))) {
        giveStorageLock();
        return 0;
    }
    File file = activeFileSystem->open(path, FILE_READ);
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
    if (!storageMounted) {
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
    if (!storageMounted) {
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
        File file = activeFileSystem->open(path, FILE_READ);
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
    if (!storageMounted) {
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
        File file = activeFileSystem->open(path, FILE_READ);
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

#ifdef TEST_MODE
uint32_t countTodayLines() { return countTodayRecords(); }

bool summarizeTodayFromLine(uint32_t firstLine, TestRecordSummary& summary) {
    summary = {};
    if (firstLine == 0 || !takeStorageLock()) return false;
    char path[STORAGE_PATH_BUFFER_SIZE];
    if (!storageMounted || !getTodayFilePath(path, sizeof(path))) {
        giveStorageLock();
        return false;
    }
    File file = activeFileSystem->open(path, FILE_READ);
    if (!file) {
        giveStorageLock();
        return false;
    }
    uint32_t lineNumber = 0;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        ++lineNumber;
        if (lineNumber < firstLine) continue;
        ChainRecord record{};
        if (!parseRecord(line, record)) continue;
        ++summary.records;
        if (record.plausibilityFlag == PLAUSIBILITY_NORMAL) ++summary.normal;
        else if (record.plausibilityFlag == PLAUSIBILITY_PLAUSIBLE) ++summary.plausible;
        else if (record.plausibilityFlag == PLAUSIBILITY_SUSPICIOUS) ++summary.suspicious;
        if (record.fingerprintStatus == FP_STATUS_MISMATCH) ++summary.fingerprintMismatch;
    }
    file.close();
    giveStorageLock();
    return true;
}

bool verifyTodayFile(uint32_t& failingIndex, uint32_t& recordCount) {
    failingIndex = UINT32_MAX;
    recordCount = 0;
    if (!takeStorageLock()) return false;
    char path[STORAGE_PATH_BUFFER_SIZE];
    if (!storageMounted || !getTodayFilePath(path, sizeof(path))) {
        giveStorageLock();
        return false;
    }
    if (!activeFileSystem->exists(path)) {
        giveStorageLock();
        return true;
    }
    File file = activeFileSystem->open(path, FILE_READ);
    if (!file) {
        giveStorageLock();
        return false;
    }
    ChainRecord previous{};
    bool hasPrevious = false;
    bool valid = true;
    uint32_t lineIndex = 0;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        ChainRecord current{};
        if (!parseRecord(line, current)) {
            failingIndex = lineIndex;
            valid = false;
            break;
        }
        ++recordCount;
        if ((hasPrevious && strcmp(current.previousHash, previous.hash) != 0) || hashchain_module::verifyChainSegment(&current, 1) != -1) {
            failingIndex = lineIndex;
            valid = false;
            break;
        }
        previous = current;
        hasPrevious = true;
        ++lineIndex;
    }
    file.close();
    giveStorageLock();
    return valid;
}

bool backupTodayFile() {
    if (!takeStorageLock()) return false;
    char path[STORAGE_PATH_BUFFER_SIZE];
    if (!storageMounted || !getTodayFilePath(path, sizeof(path)) || !activeFileSystem->exists(path)) {
        giveStorageLock();
        return false;
    }
    activeFileSystem->remove(TEST_BACKUP_PATH);
    File input = activeFileSystem->open(path, FILE_READ);
    File output = activeFileSystem->open(TEST_BACKUP_PATH, FILE_WRITE);
    if (!input || !output) {
        if (input) input.close();
        if (output) output.close();
        activeFileSystem->remove(TEST_BACKUP_PATH);
        giveStorageLock();
        return false;
    }
    uint8_t buffer[TEST_COPY_BUFFER_SIZE];
    bool copied = true;
    while (input.available()) {
        int bytesRead = input.read(buffer, sizeof(buffer));
        if (bytesRead <= 0 || output.write(buffer, static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
            copied = false;
            break;
        }
    }
    input.close();
    output.flush();
    output.close();
    if (!copied) activeFileSystem->remove(TEST_BACKUP_PATH);
    giveStorageLock();
    return copied;
}

bool restoreTodayFile() {
    if (!takeStorageLock()) return false;
    char path[STORAGE_PATH_BUFFER_SIZE];
    if (!storageMounted || !getTodayFilePath(path, sizeof(path)) || !activeFileSystem->exists(TEST_BACKUP_PATH)) {
        giveStorageLock();
        return false;
    }
    activeFileSystem->remove(path);
    bool restored = activeFileSystem->rename(TEST_BACKUP_PATH, path);
    giveStorageLock();
    return restored;
}
#endif

bool recoverPartialTodayTail() {
    if (!takeStorageLock()) return false;
    char path[STORAGE_PATH_BUFFER_SIZE];
    if (!storageMounted || !getTodayFilePath(path, sizeof(path)) || !activeFileSystem->exists(path)) {
        giveStorageLock();
        return false;
    }
    File input = activeFileSystem->open(path, FILE_READ);
    if (!input) {
        giveStorageLock();
        return false;
    }
    size_t fileSize = input.size();
    size_t lastCompleteBytes = 0;
    String trailing;
    while (input.available()) {
        int value = input.read();
        if (value < 0) break;
        if (value == '\n') {
            lastCompleteBytes = input.position();
            trailing = "";
        } else {
            trailing += static_cast<char>(value);
        }
    }
    input.close();
    ChainRecord finalRecord{};
    bool validTrailingRecord = trailing.length() > 0 && parseRecord(trailing, finalRecord);
    if (lastCompleteBytes == fileSize && !validTrailingRecord) {
        giveStorageLock();
        return true;
    }
    activeFileSystem->remove(STORAGE_TEMP_PATH);
    input = activeFileSystem->open(path, FILE_READ);
    File output = activeFileSystem->open(STORAGE_TEMP_PATH, FILE_WRITE);
    if (!input || !output) {
        if (input) input.close();
        if (output) output.close();
        activeFileSystem->remove(STORAGE_TEMP_PATH);
        giveStorageLock();
        return false;
    }
    size_t copyBytes = validTrailingRecord ? fileSize : lastCompleteBytes;
    uint8_t buffer[STORAGE_RECOVERY_BUFFER_SIZE];
    bool copied = input.seek(0);
    while (copied && copyBytes > 0) {
        size_t request = copyBytes < sizeof(buffer) ? copyBytes : sizeof(buffer);
        int bytesRead = input.read(buffer, request);
        if (bytesRead <= 0 || output.write(buffer, static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
            copied = false;
            break;
        }
        copyBytes -= static_cast<size_t>(bytesRead);
    }
    if (copied && validTrailingRecord) copied = output.println() > 0;
    input.close();
    output.flush();
    output.close();
    if (copied) {
        activeFileSystem->remove(path);
        copied = activeFileSystem->rename(STORAGE_TEMP_PATH, path);
    } else {
        activeFileSystem->remove(STORAGE_TEMP_PATH);
    }
    if (copied) {
        File recovered = activeFileSystem->open(path, FILE_READ);
        uint32_t lineCount = 0;
        if (recovered) {
            while (recovered.available()) {
                recovered.readStringUntil('\n');
                ++lineCount;
            }
            recovered.close();
            todayRecordsCached = lineCount;
        }
    }
    giveStorageLock();
    return copied;
}
static void verifyLastRecords() {
    uint32_t count = 0;
    int32_t failureIndex = -1;
    bool readable = verifyAllRecords(count, failureIndex);
    if (storageVerifyAvailable && !storageVerifyPassed) {
        Serial.printf("VERIFY FAIL index=%ld records=%u\n", static_cast<long>(storageVerifyFailure), static_cast<unsigned int>(count));
        return;
    }
    if (boot_monitor_module::bootVerifyCount() > 0 && !boot_monitor_module::bootVerifyPassed()) {
        storageVerifyAvailable = true;
        storageVerifyPassed = false;
        storageVerifyFailure = boot_monitor_module::bootVerifyFailureIndex();
        Serial.printf("VERIFY FAIL index=%ld records=%u\n", static_cast<long>(storageVerifyFailure), static_cast<unsigned int>(count));
        return;
    }
    storageVerifyAvailable = readable;
    storageVerifyPassed = readable && failureIndex < 0;
    storageVerifyFailure = failureIndex;
    if (storageVerifyPassed) {
        Serial.printf("VERIFY PASS records=%u\n", static_cast<unsigned int>(count));
    } else {
        Serial.printf("VERIFY FAIL index=%ld records=%u\n", static_cast<long>(failureIndex < 0 ? 0 : failureIndex), static_cast<unsigned int>(count));
    }
}

static void processStorageRequest(const StorageCommand& request, QueueHandle_t recordQueue, ChainRecord& pendingRecord, bool& pendingRecordValid) {
    if (request.type == STORAGE_COMMAND_VERIFY) {
        verifyLastRecords();
    } else if (request.type == STORAGE_COMMAND_TAMPER) {
        Serial.println(tamperTodayLine(request.lineNumber) ? "TAMPER complete" : "TAMPER failed");
    } else if (request.type == STORAGE_COMMAND_CHAIN) {
        char latestHash[HASH_HEX_LEN + 1]{};
        bool hasHash = hashchain_module::loadLatestHash(latestHash, sizeof(latestHash));
        Serial.printf("CHAIN last_hash=%s records_today=%lu drops=%lu\n", hasHash ? latestHash : "", static_cast<unsigned long>(countTodayRecords()), static_cast<unsigned long>(hashchain_module::loadDropCount()));
    } else if (request.type == STORAGE_COMMAND_CHAIN_RESET) {
        bool reset = hashchain_module::prepareRuntimeReset();
        if (reset && pendingRecordValid) {
            reset = appendRecordToFile(pendingRecord);
            if (reset) pendingRecordValid = false;
        }
        ChainRecord queuedRecord{};
        while (reset && xQueueReceive(recordQueue, &queuedRecord, 0) == pdTRUE) {
            if (!appendRecordToFile(queuedRecord)) {
                pendingRecord = queuedRecord;
                pendingRecordValid = true;
                reset = false;
            }
        }
        if (reset) reset = archiveLogFiles() && hashchain_module::finishRuntimeReset(true);
        if (!reset) hashchain_module::finishRuntimeReset(false);
        if (reset) {
            clearVerifyState();
            boot_monitor_module::clearBootVerifyState();
            Serial.println("CHAIN RESET OK");
        } else {
            Serial.println("CHAIN RESET FAILED");
        }
    } else if (request.type == STORAGE_COMMAND_FS_FORMAT) {
        bool formatted = false;
        if (takeStorageLock() && activeBackend == 2) {
            storageMounted = false;
            LittleFS.end();
            formatted = LittleFS.format() && LittleFS.begin(true);
            if (formatted) {
                activeFileSystem = &LittleFS;
                storageMounted = true;
                formatted = LittleFS.exists(LOG_DIR) || LittleFS.mkdir(LOG_DIR);
            }
            storageMounted = formatted;
            if (formatted) {
                todayRecordsCached = 0;
                clearVerifyState();
                boot_monitor_module::clearBootVerifyState();
                formatted = hashchain_module::resetChain();
            }
            giveStorageLock();
        }
        Serial.println(formatted ? "FS FORMAT OK" : "FS FORMAT FAILED");
    }
}

void registerStorageCommands() {
    serial_cmd_module::registerCommand("VERIFY", handleVerifyCommand);
    serial_cmd_module::registerCommand("TAMPER", handleTamperCommand);
    serial_cmd_module::registerCommand("CHAIN", handleChainCommand);
    serial_cmd_module::registerCommand("FS", handleFsCommand);
}

void startStorageTask(QueueHandle_t inputQueue) {
    if (inputQueue == nullptr || storageCommandQueue == nullptr || storageMutex == nullptr) return;
    xTaskCreate(storageTask, "sdwrite", STACK_SIZE_STORAGE, inputQueue, PRIORITY_SDWRITE, nullptr);
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
            bool mounted = false;
            if (takeStorageLock()) {
                mounted = mountCard();
                giveStorageLock();
            }
            if (mounted) todayRecordsCached = countTodayRecords();
        }
        StorageCommand request;
        if (xQueueReceive(storageCommandQueue, &request, 0) == pdTRUE) {
            processStorageRequest(request, recordQueue, pendingRecord, pendingRecordValid);
        }
        if (!pendingRecordValid && xQueueReceive(recordQueue, &pendingRecord, pdMS_TO_TICKS(STORAGE_POLL_MS)) == pdTRUE) {
            pendingRecordValid = true;
        }
        if (pendingRecordValid && appendRecordToFile(pendingRecord)) {
            pendingRecordValid = false;
        } else if (pendingRecordValid) {
#if DEBUG_VERBOSE
            Serial.println("sdwrite retry pending");
#endif
            vTaskDelay(pdMS_TO_TICKS(STORAGE_POLL_MS));
        }
    }
}
}
