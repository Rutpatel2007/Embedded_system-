#ifndef SENSOR_PROVIDER_H
#define SENSOR_PROVIDER_H

#include <Arduino.h>
#include "sensor_sample.h"
#include "../include/config.h"
#include "mq135.h"
#include "ina219_sensor.h"
#include "rtc.h"
#include "relay.h"

// Abstract Interface for Sensor Acquisition (Hardware vs Simulation vs Replay)
class ISensorProvider {
public:
    virtual ~ISensorProvider() {}
    virtual bool begin() = 0;
    virtual bool readSample(SensorSample& sample) = 0;
    virtual const char* getProviderName() const = 0;
};

// Physical Hardware Provider (Uses real peripheral drivers)
class HardwareSensorProvider : public ISensorProvider {
public:
    HardwareSensorProvider(MQ135Sensor& mq, INA219Sensor& ina, RTCDriver& rtc, RelayController& relay);
    bool begin() override;
    bool readSample(SensorSample& sample) override;
    const char* getProviderName() const override { return "HARDWARE_PROVIDER"; }

private:
    MQ135Sensor& _mq;
    INA219Sensor& _ina;
    RTCDriver& _rtc;
    RelayController& _relay;
};

// Mock Simulation Provider (Generates deterministic test scenarios for offline development)
class MockSensorProvider : public ISensorProvider {
public:
    MockSensorProvider(MockScenario scenario = SCENARIO_NORMAL);
    bool begin() override;
    bool readSample(SensorSample& sample) override;
    const char* getProviderName() const override { return "MOCK_PROVIDER"; }

    void setScenario(MockScenario scenario);
    MockScenario getScenario() const { return _scenario; }

private:
    MockScenario _scenario;
    uint32_t _stepCount;
    uint64_t _mockTimeEpoch;
};

// Replay Sensor Provider (Feeds truesense_simulation_dataset.csv row-by-row into pipeline)
class ReplaySensorProvider : public ISensorProvider {
public:
    struct ReplayRow {
        uint32_t sample_id;
        uint64_t timestamp_unix;
        uint32_t timestamp_ms;
        char scenario[32];
        uint8_t relay_state;
        uint16_t adc_raw;
        float adc_voltage_V;
        float gas_voltage_V;
        float bus_voltage_V;
        float current_mA;
        float power_mW;
        char expected_plausibility[16];
        char expected_fingerprint_status[32];
    };

    ReplaySensorProvider(const char* csvPath = "tests/data/truesense_simulation_dataset.csv");
    bool begin() override;
    bool readSample(SensorSample& sample) override;
    const char* getProviderName() const override { return "REPLAY_PROVIDER"; }

    bool loadCSV(const char* csvPath);
    void reset();
    size_t getSampleCount() const { return _sampleCount; }
    size_t getCurrentIndex() const { return _currentIndex; }
    const ReplayRow* getSampleAt(size_t index) const { return (index < _sampleCount) ? &_samples[index] : nullptr; }

private:
    static const size_t MAX_REPLAY_SAMPLES = 600;
    ReplayRow _samples[MAX_REPLAY_SAMPLES];
    size_t _sampleCount;
    size_t _currentIndex;
    char _csvPath[128];
};

#endif // SENSOR_PROVIDER_H
