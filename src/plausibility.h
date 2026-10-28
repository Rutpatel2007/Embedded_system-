#ifndef PROOV_PLAUSIBILITY_H
#define PROOV_PLAUSIBILITY_H

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
}

#endif
