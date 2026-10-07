#include "layer2_fingerprint.h"
#include <Preferences.h>

Layer2Config Layer2Engine::defaultConfig() {
    Layer2Config config;
    // NOTE: Thresholds are UNVALIDATED DEVELOPMENT PARAMETERS
    config.similarityThreshold = FINGERPRINT_SIM_THRESHOLD;    // 0.85f -> SENSOR_OK
    config.reviewThreshold     = FINGERPRINT_REVIEW_THRESHOLD; // 0.70f -> DEGRADED_REVIEW

    config.weightBaseline  = 0.35f;
    config.weightNoise     = 0.25f;
    config.weightSlope     = 0.20f;
    config.weightWarmup    = 0.20f;

    config.minEnrollmentSamples = 300; // 300 resting samples (~150s)
    config.minEnrollmentCycles  = 3;   // 3 equipment test cycles
    config.minValidFeatures     = 2;   // Minimum 2 valid steady-state features required
    config.adminPassword        = L2_ADMIN_PASSWORD;

    return config;
}

Layer2Engine::Layer2Engine(const Layer2Config& config)
    : _config(config), _enrollmentState(EnrollmentState::UNENROLLED), _isEnrolled(false), _mockMode(false),
      _enrollmentSampleCount(0), _accumBaselineSum(0.0f), _accumNoiseSum(0.0f) {
    memset(&_reference, 0, sizeof(StatisticalFingerprint));
}

uint16_t Layer2Engine::calculateChecksum(const StatisticalFingerprint& fp) const {
    const uint8_t* ptr = (const uint8_t*)&fp;
    size_t len = sizeof(StatisticalFingerprint) - sizeof(uint16_t);
    uint16_t sum = 0x1234;
    for (size_t i = 0; i < len; i++) {
        sum = (sum << 5) - sum + ptr[i];
    }
    return sum;
}

bool Layer2Engine::begin(bool mockMode) {
    _mockMode = mockMode;
    _enrollmentState = EnrollmentState::UNENROLLED;
    _isEnrolled = false;

    if (_mockMode) {
        return true;
    }

    // Attempt to load reference fingerprint from ESP32 NVS Flash
    Preferences prefs;
    if (prefs.begin(NVS_L2_NAMESPACE, true)) { // Read-only mode
        size_t size = prefs.getBytesLength(NVS_L2_KEY_FINGERPRINT);
        if (size == sizeof(StatisticalFingerprint)) {
            prefs.getBytes(NVS_L2_KEY_FINGERPRINT, &_reference, sizeof(StatisticalFingerprint));
            if (_reference.schemaVersion == L2_SCHEMA_VERSION) {
                uint16_t computedCheck = calculateChecksum(_reference);
                if (computedCheck == _reference.checksum) {
                    _isEnrolled = true;
                    _enrollmentState = EnrollmentState::ENROLLED;
                }
            }
        }
        prefs.end();
    }

    return true;
}

void Layer2Engine::setConfig(const Layer2Config& config) {
    _config = config;
}

void Layer2Engine::loadMockReference(const StatisticalFingerprint& ref) {
    _reference = ref;
    _reference.schemaVersion = L2_SCHEMA_VERSION;
    _reference.checksum = calculateChecksum(_reference);
    _isEnrolled = true;
    _enrollmentState = EnrollmentState::ENROLLED;
}

float Layer2Engine::calculateNoiseVariance(const RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& gasBuffer) const {
    size_t count = gasBuffer.count();
    if (count < 10) return 0.0f;

    float sum = 0.0f;
    size_t validCount = 0;

    for (size_t i = 0; i < count; i++) {
        SensorSample s = gasBuffer.get(i);
        if (s.gasValid) {
            sum += s.sensorVoltage;
            validCount++;
        }
    }

    if (validCount < 10) return 0.0f;
    float mean = sum / (float)validCount;

    float sqDiffSum = 0.0f;
    for (size_t i = 0; i < count; i++) {
        SensorSample s = gasBuffer.get(i);
        if (s.gasValid) {
            float diff = s.sensorVoltage - mean;
            sqDiffSum += (diff * diff);
        }
    }

    return sqDiffSum / (float)validCount;
}

float Layer2Engine::calculateResponseSlope(const RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& gasBuffer) const {
    size_t count = gasBuffer.count();
    if (count < 6) return 0.0f; // Need at least 3 seconds of history

    SensorSample oldest = gasBuffer.get(count - 6);
    SensorSample newest = gasBuffer.getLatest();

    if (!oldest.gasValid || !newest.gasValid) return 0.0f;

    float deltaV = newest.sensorVoltage - oldest.sensorVoltage;
    float deltaT = (float)(newest.millisMs - oldest.millisMs) / 1000.0f;

    if (deltaT <= 0.1f) return 0.0f;
    return deltaV / deltaT; // V/s
}

Layer2Result Layer2Engine::evaluate(
    const SensorSample& currentSample,
    const RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& gasBuffer,
    const RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE>& powerBuffer
) {
    Layer2Result result;
    memset(&result, 0, sizeof(Layer2Result));

    result.isEnrolled = _isEnrolled;
    result.coldWarmupUsed = false;

    // 1. Unenrolled State Handling
    if (!_isEnrolled) {
        result.status = SensorIdentityStatus::UNENROLLED;
        result.similarityScore = 0.0f; // 0.0f indicates NOT COMPUTABLE
        result.featureDistance = 1.0f;
        result.validFeatureCount = 0;
        return result;
    }

    // 2. Feature Extraction & Bounded Distance Calculation
    float totalWeightedDistance = 0.0f;
    float totalWeight = 0.0f;
    uint8_t validFeatures = 0;

    // Feature 1: Baseline Voltage
    if (currentSample.gasValid && currentSample.sensorVoltage >= 0.05f) {
        float dev = fabsf(currentSample.sensorVoltage - _reference.mu_baselineVoltage);
        float den = 3.0f * _reference.sigma_baselineVoltage + 0.05f; // Safe denominator guard
        float d_base = dev / den;
        if (d_base > 1.0f) d_base = 1.0f;

        totalWeightedDistance += _config.weightBaseline * d_base;
        totalWeight += _config.weightBaseline;
        result.baselineValid = true;
        validFeatures++;
    }

    // Feature 2: High-Frequency Noise Variance
    float liveNoise = calculateNoiseVariance(gasBuffer);
    if (liveNoise > 0.0f) {
        float dev = fabsf(liveNoise - _reference.mu_noiseVariance);
        float den = 3.0f * _reference.sigma_noiseVariance + 1.0e-6f; // Safe denominator guard
        float d_noise = dev / den;
        if (d_noise > 1.0f) d_noise = 1.0f;

        totalWeightedDistance += _config.weightNoise * d_noise;
        totalWeight += _config.weightNoise;
        result.noiseValid = true;
        validFeatures++;
    }

    // Feature 3: Dynamic Response Slope (Only evaluated during active equipment emissions)
    if (currentSample.equipmentActive && currentSample.gasValid) {
        float liveSlope = calculateResponseSlope(gasBuffer);
        if (liveSlope > 0.005f) { // Positive emission rise slope
            float dev = fabsf(liveSlope - _reference.mu_responseSlope);
            float den = 3.0f * _reference.sigma_responseSlope + 0.001f; // Safe denominator guard
            float d_slope = dev / den;
            if (d_slope > 1.0f) d_slope = 1.0f;

            totalWeightedDistance += _config.weightSlope * d_slope;
            totalWeight += _config.weightSlope;
            result.slopeValid = true;
            validFeatures++;
        }
    }

    result.validFeatureCount = validFeatures;

    // 3. Insufficient Features Safety Check
    // If fewer than the minimum steady-state features exist (e.g., fresh boot without history yet)
    if (validFeatures < _config.minValidFeatures || totalWeight <= 0.001f) {
        result.status = SensorIdentityStatus::INSUFFICIENT_FEATURES;
        result.similarityScore = 0.0f; // NOT COMPUTABLE (Does NOT trigger security alarm!)
        result.featureDistance = 1.0f;
        return result;
    }

    // 4. Guaranteed Bounded Distance & Similarity Math
    result.featureDistance = totalWeightedDistance / totalWeight; // Guaranteed [0.0, 1.0]
    if (result.featureDistance > 1.0f) result.featureDistance = 1.0f;
    if (result.featureDistance < 0.0f) result.featureDistance = 0.0f;

    result.similarityScore = 1.0f - result.featureDistance; // Guaranteed [0.0, 1.0]

    // 5. Status Classification (UNVALIDATED DEVELOPMENT THRESHOLDS)
    if (result.similarityScore >= _config.similarityThreshold) { // >= 0.85
        result.status = SensorIdentityStatus::SENSOR_OK;
    } else if (result.similarityScore >= _config.reviewThreshold) { // 0.70 <= Similarity < 0.85
        result.status = SensorIdentityStatus::DEGRADED_REVIEW;
    } else { // < 0.70
        result.status = SensorIdentityStatus::SENSOR_IDENTITY_MISMATCH;
    }

    return result;
}

bool Layer2Engine::startEnrollment() {
    _enrollmentState = EnrollmentState::ENROLLING_RESTING;
    _enrollmentSampleCount = 0;
    _accumBaselineSum = 0.0f;
    _accumNoiseSum = 0.0f;
    return true;
}

bool Layer2Engine::processEnrollmentStep(
    const SensorSample& sample,
    const RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& gasBuffer,
    const RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE>& powerBuffer
) {
    if (_enrollmentState != EnrollmentState::ENROLLING_RESTING &&
        _enrollmentState != EnrollmentState::ENROLLING_EQUIPMENT_CYCLES) {
        return false;
    }

    if (sample.gasValid) {
        _enrollmentSampleCount++;
        _accumBaselineSum += sample.sensorVoltage;
        _accumNoiseSum += calculateNoiseVariance(gasBuffer);
    }

    if (_enrollmentSampleCount >= _config.minEnrollmentSamples) {
        _enrollmentState = EnrollmentState::ENROLLING_EQUIPMENT_CYCLES;
    }

    return true;
}

bool Layer2Engine::confirmEnrollment(const char* password) {
    if (password == NULL || strcmp(password, _config.adminPassword) != 0) {
        return false; // Password verification failed (Do NOT log password!)
    }

    if (_enrollmentSampleCount < 50) {
        return false; // Insufficient enrollment data collected
    }

    // Calculate statistical reference profile
    _reference.schemaVersion = L2_SCHEMA_VERSION;
    _reference.mu_baselineVoltage = _accumBaselineSum / (float)_enrollmentSampleCount;
    _reference.sigma_baselineVoltage = 0.05f; // Estimated reference std dev

    _reference.mu_noiseVariance = _accumNoiseSum / (float)_enrollmentSampleCount;
    if (_reference.mu_noiseVariance <= 0.0f) _reference.mu_noiseVariance = 1.0e-5f;
    _reference.sigma_noiseVariance = _reference.mu_noiseVariance * 0.15f;

    _reference.mu_responseSlope = 0.15f; // Initial response slope baseline (V/s)
    _reference.sigma_responseSlope = 0.03f;

    _reference.mu_recoveryConstant = 12.0f; // Initial decay constant (s)
    _reference.sigma_recoveryConstant = 2.0f;

    _reference.warmupValid = false;
    _reference.enrollmentTimestamp = 1750000000;
    _reference.totalCyclesObserved = 3;
    _reference.checksum = calculateChecksum(_reference);

    _isEnrolled = true;
    _enrollmentState = EnrollmentState::ENROLLED;

    // Persist to NVS Flash if not in mock mode
    if (!_mockMode) {
        Preferences prefs;
        if (prefs.begin(NVS_L2_NAMESPACE, false)) { // Read-write mode
            prefs.putBytes(NVS_L2_KEY_FINGERPRINT, &_reference, sizeof(StatisticalFingerprint));
            prefs.end();
        }
    }

    return true;
}

bool Layer2Engine::clearEnrollment(const char* password) {
    if (password == NULL || strcmp(password, _config.adminPassword) != 0) {
        return false;
    }

    memset(&_reference, 0, sizeof(StatisticalFingerprint));
    _isEnrolled = false;
    _enrollmentState = EnrollmentState::UNENROLLED;

    if (!_mockMode) {
        Preferences prefs;
        if (prefs.begin(NVS_L2_NAMESPACE, false)) {
            prefs.clear();
            prefs.end();
        }
    }

    return true;
}
