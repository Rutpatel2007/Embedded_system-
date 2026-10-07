#include "layer1_plausibility.h"

Layer1Config Layer1PlausibilityEngine::defaultConfig() {
    Layer1Config config;
    // NOTE: All parameters are UNVALIDATED simulation defaults.
    config.powerActiveThreshold_mW = POWER_ACTIVE_THRESHOLD_MW; // 50.0 mW
    config.gasSpikeRatioThreshold = GAS_SPIKE_RATIO_THRESHOLD; // 1.20f (+20% over baseline)
    config.trailingPowerWindowMs = 90000;                      // 90,000 ms (90s trailing history)
    config.gasResponseWindowMs = 30000;                        // 30,000 ms (30s physical response window)
    config.baselineWindowMs = 60000;                           // 60,000 ms (60s rolling baseline)
    return config;
}

Layer1PlausibilityEngine::Layer1PlausibilityEngine(const Layer1Config& config)
    : _config(config) {}

void Layer1PlausibilityEngine::setConfig(const Layer1Config& config) {
    _config = config;
}

float Layer1PlausibilityEngine::calculateGasBaseline(
    const RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& gasBuffer,
    uint32_t currentMillisMs
) const {
    size_t totalCount = gasBuffer.count();
    if (totalCount == 0) return 0.0f;

    float sumVoltage = 0.0f;
    size_t validCount = 0;

    for (size_t i = 0; i < totalCount; i++) {
        SensorSample s = gasBuffer.get(i);
        // Calculate age of sample in milliseconds
        uint32_t sampleAgeMs = (currentMillisMs >= s.millisMs) ? (currentMillisMs - s.millisMs) : 0;

        // Only include valid samples within the configured baseline window
        if (s.gasValid && sampleAgeMs <= _config.baselineWindowMs) {
            sumVoltage += s.sensorVoltage;
            validCount++;
        }
    }

    if (validCount == 0) return 0.0f;
    return sumVoltage / (float)validCount;
}

void Layer1PlausibilityEngine::analyzePowerHistory(
    const RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE>& powerBuffer,
    uint32_t currentMillisMs,
    bool& powerActive,
    float& peakPower,
    uint32_t& timeSincePowerMs
) const {
    size_t totalCount = powerBuffer.count();
    powerActive = false;
    peakPower = 0.0f;
    timeSincePowerMs = UINT32_MAX;

    for (size_t i = 0; i < totalCount; i++) {
        PowerSample p = powerBuffer.get(i);
        uint32_t sampleAgeMs = (currentMillisMs >= p.millisMs) ? (currentMillisMs - p.millisMs) : 0;

        // Track peak power in the trailing window
        if (p.power_mW > peakPower) {
            peakPower = p.power_mW;
        }

        // Check if power exceeds equipment active threshold within trailing window
        if ((p.power_mW >= _config.powerActiveThreshold_mW || p.equipmentActive) && sampleAgeMs <= _config.trailingPowerWindowMs) {
            powerActive = true;
            if (sampleAgeMs < timeSincePowerMs) {
                timeSincePowerMs = sampleAgeMs; // Time elapsed since most recent power activity
            }
        }
    }
}

Layer1Result Layer1PlausibilityEngine::evaluate(
    const SensorSample& currentSample,
    const RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& gasBuffer,
    const RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE>& powerBuffer
) {
    Layer1Result result;
    result.gasSensorValid = currentSample.gasValid;
    result.powerSensorValid = currentSample.powerValid;

    // Handle sensor failure/disconnection gracefully without corrupting baseline
    if (!result.gasSensorValid || !result.powerSensorValid) {
        result.status = PlausibilityStatus::NORMAL;
        result.gasEventDetected = false;
        result.powerActivityDetected = false;
        result.temporalCorrelation = false;
        result.baselineGas = 0.0f;
        result.currentGas = currentSample.sensorVoltage;
        result.gasChangeRatio = 0.0f;
        result.recentPower = currentSample.power_mW;
        result.timeSincePowerActivityMs = UINT32_MAX;
        return result;
    }

    // 1. Calculate historical gas baseline signal (Voltage proxy)
    float baseline = calculateGasBaseline(gasBuffer, currentSample.millisMs);
    if (baseline <= 0.05f) {
        baseline = currentSample.sensorVoltage; // Fallback to current if history is empty
    }

    result.baselineGas = baseline;
    result.currentGas = currentSample.sensorVoltage;
    result.gasChangeRatio = (baseline > 0.001f) ? (result.currentGas / baseline) : 1.0f;

    // 2. Detect gas spike event
    result.gasEventDetected = (result.gasChangeRatio >= _config.gasSpikeRatioThreshold);

    // 3. Search trailing power history for equipment activity
    bool powerActive = false;
    float peakPower = 0.0f;
    uint32_t timeSincePowerMs = UINT32_MAX;

    analyzePowerHistory(powerBuffer, currentSample.millisMs, powerActive, peakPower, timeSincePowerMs);

    // Also check current instantaneous power
    if (currentSample.power_mW >= _config.powerActiveThreshold_mW || currentSample.equipmentActive) {
        powerActive = true;
        timeSincePowerMs = 0;
        if (currentSample.power_mW > peakPower) {
            peakPower = currentSample.power_mW;
        }
    }

    result.powerActivityDetected = powerActive;
    result.recentPower = peakPower;
    result.timeSincePowerActivityMs = timeSincePowerMs;

    // 4. Evaluate temporal correlation between power activity and gas response
    result.temporalCorrelation = (powerActive && (timeSincePowerMs <= _config.gasResponseWindowMs));

    // 5. Final Plausibility Status Classification
    if (!result.gasEventDetected) {
        // No gas spike detected = NORMAL
        result.status = PlausibilityStatus::NORMAL;
    } else if (result.gasEventDetected && (result.temporalCorrelation || powerActive)) {
        // Gas spike WITH matching recent power activity = PLAUSIBLE
        result.status = PlausibilityStatus::PLAUSIBLE;
    } else {
        // Gas spike WITHOUT matching equipment power activity = SUSPICIOUS (Physical Inconsistency)
        result.status = PlausibilityStatus::SUSPICIOUS;
    }

    return result;
}
