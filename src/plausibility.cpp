#include "plausibility.h"
#include "config.h"

static uint32_t consecutiveSpikeSamples;
static PlausibilityStats stats;
static bool equipmentStateInitialized;
static bool previousEquipmentActive;
static uint32_t equipmentStateChangedAt;
static bool lastChangeWasActivation;

namespace plausibility_module {
uint64_t safeDivide(uint64_t numerator, uint64_t denominator, uint64_t fallback) { return denominator == 0 ? fallback : numerator / denominator; }
uint32_t safeModulo(uint32_t numerator, uint32_t denominator, uint32_t fallback) { return denominator == 0 ? fallback : numerator % denominator; }

void init() {
    consecutiveSpikeSamples = 0;
    stats = {};
    equipmentStateInitialized = false;
    previousEquipmentActive = false;
    equipmentStateChangedAt = 0;
    lastChangeWasActivation = false;
}

int checkPlausibility(uint32_t gasPpmX100, uint32_t baselineX100, bool equipmentActive) {
    uint32_t now = millis();
    if (!equipmentStateInitialized || equipmentActive != previousEquipmentActive) {
        lastChangeWasActivation = equipmentActive;
        previousEquipmentActive = equipmentActive;
        equipmentStateChangedAt = now;
        equipmentStateInitialized = true;
    }
    bool withinActivationGrace = lastChangeWasActivation && now - equipmentStateChangedAt <= EQUIPMENT_GRACE_MS;
    bool effectiveEquipmentActive = equipmentActive || withinActivationGrace;
    bool spike = baselineX100 == 0
        ? gasPpmX100 > 0
        : static_cast<uint64_t>(gasPpmX100) * PERCENT_SCALE > static_cast<uint64_t>(baselineX100) * (PERCENT_SCALE + SPIKE_THRESHOLD_PCT);
    int result = PLAUSIBILITY_NORMAL;
    if (!spike) {
        consecutiveSpikeSamples = 0;
    } else {
        if (consecutiveSpikeSamples < SPIKE_CONFIRM_SAMPLES) {
            ++consecutiveSpikeSamples;
        }
        if (consecutiveSpikeSamples >= SPIKE_CONFIRM_SAMPLES) {
            result = effectiveEquipmentActive ? PLAUSIBILITY_PLAUSIBLE : PLAUSIBILITY_SUSPICIOUS;
        }
    }
    if (result == PLAUSIBILITY_NORMAL) {
        ++stats.normalCount;
    } else if (result == PLAUSIBILITY_PLAUSIBLE) {
        ++stats.plausibleCount;
    } else {
        ++stats.suspiciousCount;
    }
    return result;
}

int invalidSensorSample() {
    consecutiveSpikeSamples = 0;
    return PLAUSIBILITY_NORMAL;
}

PlausibilityStats getPlausibilityStats() {
    return stats;
}

void resetPlausibilityStats() { stats = {}; }
}
