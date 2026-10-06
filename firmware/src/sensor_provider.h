#ifndef SENSOR_PROVIDER_H
#define SENSOR_PROVIDER_H

#include <Arduino.h>
#include "sensor_sample.h"
#include "../include/config.h"
#include "mq135.h"
#include "ina219_sensor.h"
#include "rtc.h"
#include "relay.h"

// Abstract Interface for Sensor Acquisition (Hardware vs Simulation)
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
    uint32_t _mockTimeEpoch;
};

#endif // SENSOR_PROVIDER_H
