#include "hash_chain.h"

HashChainEngine::HashChainEngine(const char* deviceId, const char* filePath)
    : _state(ChainState::CHAIN_UNINITIALIZED), _recordCount(0), _sdStorage(nullptr), _mockMode(false), _mockLogLen(0) {
    strncpy(_deviceId, deviceId != nullptr ? deviceId : DEVICE_ID, sizeof(_deviceId) - 1);
    _deviceId[sizeof(_deviceId) - 1] = '\0';

    strncpy(_filePath, filePath != nullptr ? filePath : LOG_CHAIN_FILE_PATH, sizeof(_filePath) - 1);
    _filePath[sizeof(_filePath) - 1] = '\0';

    strncpy(_previousHash, GENESIS_HASH, sizeof(_previousHash) - 1);
    _previousHash[sizeof(_previousHash) - 1] = '\0';

    _mockLogBuffer[0] = '\0';
}

void HashChainEngine::clearMockLogBuffer() {
    _mockLogBuffer[0] = '\0';
    _mockLogLen = 0;
}

void HashChainEngine::appendMockLogLine(const char* line) {
    size_t lineLen = strlen(line);
    if (_mockLogLen + lineLen < sizeof(_mockLogBuffer) - 1) {
        strcpy(_mockLogBuffer + _mockLogLen, line);
        _mockLogLen += lineLen;
        _mockLogBuffer[_mockLogLen] = '\0';
    }
}

void HashChainEngine::setMockPreviousHash(const char* hashHex) {
    if (hashHex != nullptr && strlen(hashHex) == 64) {
        strncpy(_previousHash, hashHex, sizeof(_previousHash) - 1);
        _previousHash[sizeof(_previousHash) - 1] = '\0';
        _state = ChainState::HASH_OK;
    }
}

bool HashChainEngine::begin(SDStorage* sdStorage, bool mockMode) {
    _sdStorage = sdStorage;
    _mockMode = mockMode;
    _recordCount = 0;
    strncpy(_previousHash, GENESIS_HASH, sizeof(_previousHash) - 1);
    _previousHash[sizeof(_previousHash) - 1] = '\0';

    if (_mockMode) {
        if (_mockLogLen == 0) {
            _state = ChainState::CHAIN_UNINITIALIZED;
        } else {
            recoverTailHash();
        }
        return true;
    }

    if (_sdStorage == nullptr || !_sdStorage->isInitialized()) {
        _state = ChainState::SD_READ_FAILED;
        return false;
    }

    return recoverTailHash();
}

bool HashChainEngine::serializeCanonicalString(
    const char* device_id,
    uint64_t timestamp,
    float gas_ppm,
    float power_mW,
    const char* plausibility,
    const char* fingerprint_status,
    const char* previous_hash,
    char* outBuf,
    size_t bufSize
) {
    if (outBuf == nullptr || bufSize < 128) return false;

    int written = snprintf(
        outBuf, bufSize,
        "%s|%llu|%.3f|%.3f|%s|%s|%s",
        device_id,
        (unsigned long long)timestamp,
        gas_ppm,
        power_mW,
        plausibility,
        fingerprint_status,
        previous_hash
    );

    return (written > 0 && (size_t)written < bufSize);
}

bool HashChainEngine::computeSHA256(const char* inputStr, size_t inputLen, char hexOut[65]) {
    if (inputStr == nullptr || hexOut == nullptr) return false;

    unsigned char hashBin[32];
    mbedtls_sha256((const unsigned char*)inputStr, inputLen, hashBin, 0);

    for (size_t i = 0; i < 32; i++) {
        snprintf(hexOut + (i * 2), 3, "%02x", hashBin[i]);
    }
    hexOut[64] = '\0';
    return true;
}

bool HashChainEngine::serializeJSONRecord(const CanonicalRecord& rec, char* outBuf, size_t bufSize) {
    if (outBuf == nullptr || bufSize < 256) return false;

    int written = snprintf(
        outBuf, bufSize,
        "{\"device_id\":\"%s\",\"timestamp\":%llu,\"gas_ppm\":%.3f,\"power_mW\":%.3f,\"plausibility\":\"%s\",\"fingerprint_status\":\"%s\",\"previous_hash\":\"%s\",\"hash\":\"%s\"}\n",
        rec.device_id,
        (unsigned long long)rec.timestamp,
        rec.gas_ppm,
        rec.power_mW,
        rec.plausibility,
        rec.fingerprint_status,
        rec.previous_hash,
        rec.hash
    );

    return (written > 0 && (size_t)written < bufSize);
}

// Zero-heap manual string scanner for single-line JSON parsing
bool HashChainEngine::parseJSONRecord(const char* jsonLine, CanonicalRecord& rec) {
    if (jsonLine == nullptr || strlen(jsonLine) < 50) return false;

    memset(&rec, 0, sizeof(CanonicalRecord));

    auto extractStringField = [](const char* src, const char* key, char* dest, size_t maxLen) -> bool {
        const char* p = strstr(src, key);
        if (!p) return false;
        p += strlen(key);
        if (*p != '"') return false;
        p++; // Skip quote
        const char* end = strchr(p, '"');
        if (!end) return false;
        size_t len = end - p;
        if (len >= maxLen) len = maxLen - 1;
        strncpy(dest, p, len);
        dest[len] = '\0';
        return true;
    };

    auto extractNumberField = [](const char* src, const char* key) -> const char* {
        const char* p = strstr(src, key);
        if (!p) return nullptr;
        p += strlen(key);
        return p;
    };

    if (!extractStringField(jsonLine, "\"device_id\":", rec.device_id, sizeof(rec.device_id))) return false;

    const char* tsPtr = extractNumberField(jsonLine, "\"timestamp\":");
    if (!tsPtr) return false;
    rec.timestamp = (uint64_t)strtoull(tsPtr, nullptr, 10);

    const char* gasPtr = extractNumberField(jsonLine, "\"gas_ppm\":");
    if (!gasPtr) return false;
    rec.gas_ppm = strtof(gasPtr, nullptr);

    const char* pwrPtr = extractNumberField(jsonLine, "\"power_mW\":");
    if (!pwrPtr) return false;
    rec.power_mW = strtof(pwrPtr, nullptr);

    if (!extractStringField(jsonLine, "\"plausibility\":", rec.plausibility, sizeof(rec.plausibility))) return false;
    if (!extractStringField(jsonLine, "\"fingerprint_status\":", rec.fingerprint_status, sizeof(rec.fingerprint_status))) return false;
    if (!extractStringField(jsonLine, "\"previous_hash\":", rec.previous_hash, sizeof(rec.previous_hash))) return false;
    if (!extractStringField(jsonLine, "\"hash\":", rec.hash, sizeof(rec.hash))) return false;

    return (strlen(rec.previous_hash) == 64 && strlen(rec.hash) == 64);
}

bool HashChainEngine::appendRecord(
    const SensorSample& sample,
    const Layer1Result& layer1,
    const Layer2Result& layer2
) {
    // 1. Construct Canonical Record Struct
    CanonicalRecord rec;
    memset(&rec, 0, sizeof(CanonicalRecord));

    strncpy(rec.device_id, _deviceId, sizeof(rec.device_id) - 1);
    rec.timestamp = sample.timestamp;
    rec.gas_ppm = getCanonicalGasValue(sample);
    rec.power_mW = sample.power_mW;

    strncpy(rec.plausibility, mapPlausibilityEnum(layer1.status), sizeof(rec.plausibility) - 1);
    strncpy(rec.fingerprint_status, mapFingerprintEnum(layer2.status), sizeof(rec.fingerprint_status) - 1);
    strncpy(rec.previous_hash, _previousHash, sizeof(rec.previous_hash) - 1);

    // 2. Serialize exact Canonical String
    char canonicalStr[256];
    if (!serializeCanonicalString(
            rec.device_id, rec.timestamp, rec.gas_ppm, rec.power_mW,
            rec.plausibility, rec.fingerprint_status, rec.previous_hash,
            canonicalStr, sizeof(canonicalStr))) {
        return false;
    }

    // 3. Compute SHA-256 digest
    if (!computeSHA256(canonicalStr, strlen(canonicalStr), rec.hash)) {
        return false;
    }

    // 4. Serialize single-line JSON record
    char jsonLine[512];
    if (!serializeJSONRecord(rec, jsonLine, sizeof(jsonLine))) {
        return false;
    }

    // 5. Attempt complete persistence
    bool writeOk = false;
    if (_mockMode) {
        appendMockLogLine(jsonLine);
        writeOk = true;
    } else if (_sdStorage != nullptr && _sdStorage->isInitialized()) {
        writeOk = _sdStorage->appendFile(_filePath, jsonLine);
    }

    // 6. CRITICAL HASH STATE RULE:
    // ONLY advance _previousHash AFTER successful persistence!
    if (writeOk) {
        strncpy(_previousHash, rec.hash, sizeof(_previousHash) - 1);
        _previousHash[sizeof(_previousHash) - 1] = '\0';
        _recordCount++;
        _state = ChainState::HASH_OK;
        return true;
    } else {
        // SD write failed; _previousHash retains H_{n-1} for retry on next sample
        _state = ChainState::SD_WRITE_FAILED;
        return false;
    }
}

bool HashChainEngine::recoverTailHash() {
    _recordCount = 0;
    strncpy(_previousHash, GENESIS_HASH, sizeof(_previousHash) - 1);
    _previousHash[sizeof(_previousHash) - 1] = '\0';

    if (_mockMode) {
        if (_mockLogLen == 0) {
            _state = ChainState::CHAIN_UNINITIALIZED;
            return true;
        }

        // Scan mock buffer line by line
        char expectedPrev[65];
        strncpy(expectedPrev, GENESIS_HASH, 65);

        const char* ptr = _mockLogBuffer;
        uint32_t count = 0;
        bool corruptionDetected = false;

        while (*ptr != '\0') {
            const char* nextNewline = strchr(ptr, '\n');
            if (!nextNewline) {
                // Partial line at tail (power cut simulation)
                break;
            }

            char lineBuf[512];
            size_t len = nextNewline - ptr;
            if (len >= sizeof(lineBuf)) len = sizeof(lineBuf) - 1;
            strncpy(lineBuf, ptr, len);
            lineBuf[len] = '\0';

            CanonicalRecord rec;
            if (parseJSONRecord(lineBuf, rec)) {
                // Check chain link
                if (strcmp(rec.previous_hash, expectedPrev) != 0) {
                    _state = ChainState::CHAIN_CORRUPTED;
                    corruptionDetected = true;
                    break;
                }

                // Verify hash calculation
                char canonicalStr[256];
                char calcHash[65];
                serializeCanonicalString(rec.device_id, rec.timestamp, rec.gas_ppm, rec.power_mW,
                                         rec.plausibility, rec.fingerprint_status, rec.previous_hash,
                                         canonicalStr, sizeof(canonicalStr));
                computeSHA256(canonicalStr, strlen(canonicalStr), calcHash);

                if (strcmp(calcHash, rec.hash) != 0) {
                    _state = ChainState::HASH_VERIFICATION_FAILED;
                    corruptionDetected = true;
                    break;
                }

                strncpy(expectedPrev, rec.hash, 65);
                count++;
            }

            ptr = nextNewline + 1;
        }

        if (corruptionDetected) {
            return false;
        }

        if (count == 0) {
            _state = ChainState::CHAIN_UNINITIALIZED;
        } else {
            strncpy(_previousHash, expectedPrev, 65);
            _recordCount = count;
            _state = ChainState::HASH_OK;
        }
        return true;
    }

    // Physical SD Card Recovery
    if (!_sdStorage->exists(_filePath) || _sdStorage->getFileSize(_filePath) == 0) {
        _state = ChainState::CHAIN_UNINITIALIZED;
        return true;
    }

    String content = _sdStorage->readFile(_filePath);
    if (content.length() == 0) {
        _state = ChainState::CHAIN_UNINITIALIZED;
        return true;
    }

    char expectedPrev[65];
    strncpy(expectedPrev, GENESIS_HASH, 65);
    uint32_t count = 0;

    int startIdx = 0;
    while (startIdx < (int)content.length()) {
        int newlineIdx = content.indexOf('\n', startIdx);
        if (newlineIdx == -1) break; // Partial line at tail

        String line = content.substring(startIdx, newlineIdx);
        CanonicalRecord rec;
        if (parseJSONRecord(line.c_str(), rec)) {
            if (strcmp(rec.previous_hash, expectedPrev) != 0) {
                _state = ChainState::CHAIN_CORRUPTED;
                return false;
            }

            char canonicalStr[256];
            char calcHash[65];
            serializeCanonicalString(rec.device_id, rec.timestamp, rec.gas_ppm, rec.power_mW,
                                     rec.plausibility, rec.fingerprint_status, rec.previous_hash,
                                     canonicalStr, sizeof(canonicalStr));
            computeSHA256(canonicalStr, strlen(canonicalStr), calcHash);

            if (strcmp(calcHash, rec.hash) != 0) {
                _state = ChainState::HASH_VERIFICATION_FAILED;
                return false;
            }

            strncpy(expectedPrev, rec.hash, 65);
            count++;
        }

        startIdx = newlineIdx + 1;
    }

    if (count == 0) {
        _state = ChainState::CHAIN_UNINITIALIZED;
    } else {
        strncpy(_previousHash, expectedPrev, 65);
        _recordCount = count;
        _state = ChainState::HASH_OK;
    }

    return true;
}

bool HashChainEngine::verifyChain() {
    return recoverTailHash();
}
