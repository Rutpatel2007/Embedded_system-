#ifndef HASH_CHAIN_H
#define HASH_CHAIN_H

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "mbedtls/sha256.h"
#include "sensor_sample.h"
#include "layer1_plausibility.h"
#include "layer2_fingerprint.h"
#include "sd_storage.h"
#include "../include/config.h"

// Operational / Diagnostic state of the Cryptographic Hash Chain Engine
enum class ChainState {
    CHAIN_UNINITIALIZED,     // MicroSD log file missing or empty; previous_hash set to GENESIS_HASH
    HASH_OK,                 // Operational state; record hashed, appended & flushed successfully
    CHAIN_CORRUPTED,         // Historical chain mismatch (record[n].previous_hash != record[n-1].hash)
    SD_WRITE_FAILED,         // MicroSD append or flush failed; previous_hash retained for retry
    SD_READ_FAILED,          // MicroSD unreadable or SPI bus error
    INVALID_PREVIOUS_HASH,   // Supplied previous_hash does not match expected H_{n-1}
    HASH_VERIFICATION_FAILED // Recalculated SHA-256 does not match stored digest
};

// Strongly defined canonical record payload struct
struct CanonicalRecord {
    char device_id[16];
    uint64_t timestamp;
    float gas_ppm;             // Development Proxy: gasVoltage
    float power_mW;
    char plausibility[16];     // "NORMAL", "PLAUSIBLE", "SUSPICIOUS"
    char fingerprint_status[32]; // "SENSOR_OK", "SENSOR_IDENTITY_MISMATCH"
    char previous_hash[65];    // 64 hex chars + null terminator
    char hash[65];             // 64 hex chars + null terminator
};

/*
 * DEVELOPMENT PROXY ONLY:
 * gas_ppm currently contains gas sensor voltage because physical
 * MQ-135 PPM calibration has not yet been validated.
 * This value MUST NOT be interpreted as calibrated ppm.
 */
inline float getCanonicalGasValue(const SensorSample& sample) {
    return sample.gasVoltage;
}

/*
 * The public contract currently lacks an UNKNOWN/INSUFFICIENT state.
 * Therefore the internal non-mismatch states are encoded as SENSOR_OK
 * for contract compatibility. This does NOT mean all such states are
 * equivalent from an engineering/diagnostic perspective.
 */
inline const char* mapFingerprintEnum(SensorIdentityStatus status) {
    switch (status) {
        case SensorIdentityStatus::SENSOR_OK:
        case SensorIdentityStatus::DEGRADED_REVIEW:
        case SensorIdentityStatus::INSUFFICIENT_FEATURES:
            return "SENSOR_OK";
        case SensorIdentityStatus::SENSOR_IDENTITY_MISMATCH:
        case SensorIdentityStatus::UNENROLLED:
        default:
            return "SENSOR_IDENTITY_MISMATCH";
    }
}

inline const char* mapPlausibilityEnum(PlausibilityStatus status) {
    switch (status) {
        case PlausibilityStatus::PLAUSIBLE:  return "PLAUSIBLE";
        case PlausibilityStatus::SUSPICIOUS: return "SUSPICIOUS";
        case PlausibilityStatus::NORMAL:
        default:                             return "NORMAL";
    }
}

// Standalone Hardware-Independent Cryptographic Hash Chain Engine
class HashChainEngine {
public:
    HashChainEngine(const char* deviceId = DEVICE_ID, const char* filePath = LOG_CHAIN_FILE_PATH);

    // Initialize engine, recover tail hash from SD card (or mock storage)
    bool begin(SDStorage* sdStorage = nullptr, bool mockMode = false);

    // Append record to cryptographic hash chain
    bool appendRecord(
        const SensorSample& sample,
        const Layer1Result& layer1,
        const Layer2Result& layer2
    );

    // Verify entire historical hash chain integrity from start to tail
    bool verifyChain();

    // Helper functions for canonical serialization & SHA-256 computation
    static bool serializeCanonicalString(
        const char* device_id,
        uint64_t timestamp,
        float gas_ppm,
        float power_mW,
        const char* plausibility,
        const char* fingerprint_status,
        const char* previous_hash,
        char* outBuf,
        size_t bufSize
    );

    static bool computeSHA256(const char* inputStr, size_t inputLen, char hexOut[65]);

    static bool serializeJSONRecord(const CanonicalRecord& rec, char* outBuf, size_t bufSize);

    static bool parseJSONRecord(const char* jsonLine, CanonicalRecord& rec);

    // State & diagnostic accessors
    ChainState getState() const { return _state; }
    const char* getPreviousHash() const { return _previousHash; }
    uint32_t getRecordCount() const { return _recordCount; }
    const char* getDeviceId() const { return _deviceId; }
    const char* getFilePath() const { return _filePath; }

    // Helper for test harness mock chain injection
    void setMockPreviousHash(const char* hashHex);

    // Expose mock log buffer accessors for testing
    const char* getMockLogBuffer() const { return _mockLogBuffer; }
    void clearMockLogBuffer();
    void appendMockLogLine(const char* line);

private:
    char _deviceId[16];
    char _filePath[64];
    char _previousHash[65];
    ChainState _state;
    uint32_t _recordCount;
    SDStorage* _sdStorage;
    bool _mockMode;

    // In-memory log buffer for mock mode testing without physical SD card
    char _mockLogBuffer[16384];
    size_t _mockLogLen;

    bool recoverTailHash();
};

#endif // HASH_CHAIN_H
