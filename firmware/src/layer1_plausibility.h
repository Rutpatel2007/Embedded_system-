#ifndef LAYER1_PLAUSIBILITY_H
#define LAYER1_PLAUSIBILITY_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "sensor_sample.h"
#include "ring_buffer.h"
#include "../include/config.h"

// Layer 1 Physical Plausibility Status Classification
enum class PlausibilityStatus {
    NORMAL,     // Baseline gas signal, no anomalous emissions detected
    PLAUSIBLE,  // Gas event detected with matching recent equipment power activity
    SUSPICIOUS  // Gas event detected WITHOUT corresponding equipment power activity
};

// Comprehensive result output produced by the Layer 1 Cross-Modal Plausibility Engine
struct Layer1Result {
    PlausibilityStatus status;

    bool gasEventDetected;            // True if currentGas >= baselineGas * gasSpikeRatioThreshold
    bool powerActivityDetected;       // True if equipment power >= powerActiveThreshold_mW in trailing window
    bool temporalCorrelation;         // True if power activity occurred within valid temporal response window

    float baselineGas;                // Calculated historical rolling gas baseline signal (Voltage)
    float currentGas;                 // Current uncalibrated gas proxy signal (sensorVoltage)
    float gasChangeRatio;             // Ratio currentGas / baselineGas (1.0 = baseline, 1.2 = +20%)

    float recentPower;                // Peak or most recent power consumption in trailing window (mW)
    uint32_t timeSincePowerActivityMs; // Time elapsed since equipment was last active (ms)

    bool gasSensorValid;              // Hardware sensor operational status flags
    bool powerSensorValid;
};

// Configuration parameters for the Layer 1 Plausibility Engine
// NOTE: All thresholds are UNVALIDATED simulation estimates. Must be physically calibrated later.
struct Layer1Config {
    float powerActiveThreshold_mW;    // UNVALIDATED — Equipment active threshold (default 50.0 mW)
    float gasSpikeRatioThreshold;     // UNVALIDATED — Gas spike ratio threshold (default 1.20 = +20% over baseline)

    uint32_t trailingPowerWindowMs;   // UNVALIDATED — Trailing window to search for equipment power activity (default 90000 ms = 90s)
    uint32_t gasResponseWindowMs;     // UNVALIDATED — Maximum delay window between power activation and gas response (default 30000 ms = 30s)
    uint32_t baselineWindowMs;        // UNVALIDATED — Rolling window for computing gas baseline (default 60000 ms = 60s)
};

// Standalone Hardware-Independent Layer 1 Plausibility Engine
class Layer1PlausibilityEngine {
public:
    Layer1PlausibilityEngine(const Layer1Config& config = defaultConfig());

    // Update operational configuration
    void setConfig(const Layer1Config& config);
    const Layer1Config& getConfig() const { return _config; }

    // Evaluates current sample against historical gas and power ring buffers
    Layer1Result evaluate(
        const SensorSample& currentSample,
        const RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& gasBuffer,
        const RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE>& powerBuffer
    );

    // Factory method providing baseline configuration parameters
    static Layer1Config defaultConfig();

private:
    Layer1Config _config;

    // Helper: Compute average gas baseline signal from valid historical samples
    float calculateGasBaseline(
        const RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& gasBuffer,
        uint32_t currentMillisMs
    ) const;

    // Helper: Search trailing power history for recent equipment activity
    void analyzePowerHistory(
        const RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE>& powerBuffer,
        uint32_t currentMillisMs,
        bool& powerActive,
        float& peakPower,
        uint32_t& timeSincePowerMs
    ) const;
};

#endif // LAYER1_PLAUSIBILITY_H
