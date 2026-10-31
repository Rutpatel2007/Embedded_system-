#ifndef TRUESENSE_PLAUSIBILITY_H
#define TRUESENSE_PLAUSIBILITY_H

#include <Arduino.h>

struct PlausibilityStats {
    uint32_t normalCount;
    uint32_t plausibleCount;
    uint32_t suspiciousCount;
};

namespace plausibility_module {
void init();
int checkPlausibility(uint32_t gasPpmX100, uint32_t baselineX100, bool equipmentActive);
PlausibilityStats getPlausibilityStats();
void resetPlausibilityStats();
uint64_t safeDivide(uint64_t numerator, uint64_t denominator, uint64_t fallback = 0);
uint32_t safeModulo(uint32_t numerator, uint32_t denominator, uint32_t fallback = 0);
int invalidSensorSample();
}

#endif
