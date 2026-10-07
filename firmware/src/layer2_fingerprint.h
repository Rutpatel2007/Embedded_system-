#ifndef LAYER2_FINGERPRINT_H
#define LAYER2_FINGERPRINT_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <Arduino.h>
#include "sensor_sample.h"
#include "ring_buffer.h"
#include "../include/config.h"

// Layer 2 Internal Sensor Identity Classification Status
enum class SensorIdentityStatus {
    SENSOR_OK,               // Similarity >= similarityThreshold (0.85)
    DEGRADED_REVIEW,         // reviewThreshold (0.70) <= Similarity < similarityThreshold (0.85) [Internal Proposal]
    SENSOR_IDENTITY_MISMATCH,// Similarity < reviewThreshold (0.70)
    UNENROLLED,              // No reference fingerprint exists in NVS
    INSUFFICIENT_FEATURES    // Insufficient valid features available to compute identity (Not a security alarm)
};

// Enrollment Lifecycle State Machine
enum class EnrollmentState {
    UNENROLLED,
    ENROLLING_RESTING,
    ENROLLING_EQUIPMENT_CYCLES,
    ENROLLED
};

// Versioned Statistical Reference Profile stored in ESP32 NVS Flash
struct StatisticalFingerprint {
    uint16_t schemaVersion;          // Format version tag (0x0001) for NVS compatibility

    float mu_baselineVoltage;        // Resting baseline mean voltage (V)
    float sigma_baselineVoltage;     // Resting baseline std deviation (V)

    float mu_noiseVariance;          // High-frequency noise variance mean (V^2)
    float sigma_noiseVariance;       // High-frequency noise variance std dev (V^2)

    float mu_responseSlope;          // Gas rise rate during equipment activation (V/s)
    float sigma_responseSlope;       // Rise slope std deviation (V/s)

    float mu_recoveryConstant;       // Exponential decay time constant (s)
    float sigma_recoveryConstant;    // Decay constant std deviation (s)

    float warmupCurve[FINGERPRINT_POINTS]; // 20-point cold warm-up transient curve
    bool warmupValid;                // True ONLY if captured during cold 5V power-on

    uint32_t enrollmentTimestamp;    // Epoch timestamp of enrollment completion
    uint16_t totalCyclesObserved;    // Number of equipment test cycles averaged
    uint16_t checksum;               // Integrity checksum field
};

// Layer 2 Operational Configuration Parameters
// NOTE: All thresholds are UNVALIDATED DEVELOPMENT PARAMETERS. Must be physically calibrated later.
struct Layer2Config {
    float similarityThreshold;       // UNVALIDATED DEVELOPMENT PARAMETER (Default 0.85f -> SENSOR_OK)
    float reviewThreshold;           // UNVALIDATED DEVELOPMENT PARAMETER (Default 0.70f -> DEGRADED_REVIEW)

    float weightBaseline;            // Weight for baseline voltage distance (Default 0.35f)
    float weightNoise;               // Weight for noise variance distance (Default 0.25f)
    float weightSlope;               // Weight for emission rise slope distance (Default 0.20f)
    float weightWarmup;              // Weight for cold warm-up curve distance (Default 0.20f)

    uint32_t minEnrollmentSamples;   // Minimum resting samples required for baseline (Default 300 ~ 150s)
    uint8_t minEnrollmentCycles;     // Minimum equipment active cycles required (Default 3)
    uint8_t minValidFeatures;        // Minimum steady-state features required for match (Default 2)
    const char* adminPassword;       // Serial password for enrollment authorization
};

// Detailed Output Result produced by Layer 2 Sensor Identity Engine
struct Layer2Result {
    SensorIdentityStatus status;     // Classification status
    float similarityScore;           // Bounded similarity score [0.0, 1.0]. 0.0 when INSUFFICIENT_FEATURES
    float featureDistance;           // Bounded total feature distance D_fp [0.0, 1.0]
    uint8_t validFeatureCount;       // Number of valid features evaluated

    bool isEnrolled;                 // True if NVS reference fingerprint is active
    bool coldWarmupUsed;             // True if cold warm-up curve was evaluated

    bool baselineValid;              // Individual feature extraction validity flags
    bool noiseValid;
    bool slopeValid;
    bool recoveryValid;
};

// Standalone Hardware-Independent Layer 2 Sensor Identity Engine
class Layer2Engine {
public:
    Layer2Engine(const Layer2Config& config = defaultConfig());

    // Initialize NVS storage and check for existing reference fingerprint
    bool begin(bool mockMode = false);

    // Update configuration parameters
    void setConfig(const Layer2Config& config);
    const Layer2Config& getConfig() const { return _config; }

    // Evaluates current live sample against historical ring buffers and enrolled NVS reference
    Layer2Result evaluate(
        const SensorSample& currentSample,
        const RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& gasBuffer,
        const RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE>& powerBuffer
    );

    // Explicit Enrollment Procedure API
    bool startEnrollment();
    bool processEnrollmentStep(
        const SensorSample& sample,
        const RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& gasBuffer,
        const RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE>& powerBuffer
    );
    bool confirmEnrollment(const char* password);
    bool clearEnrollment(const char* password);

    // Force mock reference initialization for testing
    void loadMockReference(const StatisticalFingerprint& ref);

    EnrollmentState getEnrollmentState() const { return _enrollmentState; }
    const StatisticalFingerprint& getReferenceFingerprint() const { return _reference; }

    static Layer2Config defaultConfig();

private:
    Layer2Config _config;
    StatisticalFingerprint _reference;
    EnrollmentState _enrollmentState;
    bool _isEnrolled;
    bool _mockMode;

    // Temporary accumulation buffers during multi-observation enrollment
    uint32_t _enrollmentSampleCount;
    float _accumBaselineSum;
    float _accumNoiseSum;

    // Helper math functions
    float calculateNoiseVariance(const RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& gasBuffer) const;
    float calculateResponseSlope(const RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& gasBuffer) const;
    uint16_t calculateChecksum(const StatisticalFingerprint& fp) const;
};

#endif // LAYER2_FINGERPRINT_H
