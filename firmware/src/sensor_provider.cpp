#include "sensor_provider.h"

// ----------------------------------------------------
// HardwareSensorProvider Implementation
// ----------------------------------------------------
HardwareSensorProvider::HardwareSensorProvider(MQ135Sensor& mq, INA219Sensor& ina, RTCDriver& rtc, RelayController& relay)
    : _mq(mq), _ina(ina), _rtc(rtc), _relay(relay) {}

bool HardwareSensorProvider::begin() {
    bool okMQ = _mq.begin();
    bool okINA = _ina.begin();
    bool okRTC = _rtc.begin();
    _relay.begin(true);
    return okMQ && okINA && okRTC;
}

bool HardwareSensorProvider::readSample(SensorSample& sample) {
    sample.timestamp = _rtc.getUnixTimestamp();
    sample.timestampValid = (sample.timestamp > 0);

    sample.gasRaw = (uint16_t)_mq.readRawADC();
    sample.gasVoltage = _mq.readADCVoltage();
    sample.sensorVoltage = _mq.readSensorVoltage();
    sample.gasValid = (sample.gasRaw < 4095);

    sample.busVoltage = _ina.readBusVoltage();
    sample.current_mA = _ina.readCurrent();
    sample.power_mW = _ina.readPower();
    sample.powerValid = _ina.isHealthy();

    sample.equipmentActive = (sample.power_mW >= POWER_ACTIVE_THRESHOLD_MW);

    return sample.gasValid && sample.powerValid && sample.timestampValid;
}

// ----------------------------------------------------
// MockSensorProvider Implementation
// ----------------------------------------------------
MockSensorProvider::MockSensorProvider(MockScenario scenario)
    : _scenario(scenario), _stepCount(0), _mockTimeEpoch(1750000000) {}

bool MockSensorProvider::begin() {
    _stepCount = 0;
    _mockTimeEpoch = 1750000000;
    return true;
}

void MockSensorProvider::setScenario(MockScenario scenario) {
    _scenario = scenario;
    _stepCount = 0;
}

bool MockSensorProvider::readSample(SensorSample& sample) {
    _stepCount++;
    _mockTimeEpoch += 0; // Epoch in seconds increments every 2 samples (500ms * 2 = 1s)
    if (_stepCount % 2 == 0) {
        _mockTimeEpoch += 1;
    }

    sample.timestamp = _mockTimeEpoch;
    sample.timestampValid = true;

    // Small deterministic pseudo-noise
    float noise = (float)((_stepCount % 7) - 3) * 0.005f;

    switch (_scenario) {
        case SCENARIO_NORMAL:
            sample.gasRaw = 1200 + (_stepCount % 15);
            sample.gasVoltage = 0.967f + noise;
            sample.sensorVoltage = sample.gasVoltage * 1.5f;
            sample.gasValid = true;

            sample.busVoltage = 4.98f;
            sample.current_mA = 2.1f;
            sample.power_mW = 10.45f;
            sample.powerValid = true;
            sample.equipmentActive = false;
            break;

        case SCENARIO_PLAUSIBLE:
            // Timeline:
            // Steps 0-20: Equipment OFF, Baseline Gas
            // Steps 21-80: Equipment ON (Power 850 mW)
            // Steps 35-80: Gas rises after 7-second physical diffusion delay (Plausible emission)
            if (_stepCount > 20 && _stepCount <= 80) {
                sample.power_mW = 850.0f + noise * 100.0f;
                sample.equipmentActive = true;
            } else {
                sample.power_mW = 10.0f;
                sample.equipmentActive = false;
            }

            if (_stepCount > 35 && _stepCount <= 80) {
                // Gas rises up to 3.2V
                float fillRatio = (float)(_stepCount - 35) / 15.0f;
                if (fillRatio > 1.0f) fillRatio = 1.0f;
                sample.gasVoltage = 1.0f + fillRatio * 2.2f + noise;
            } else {
                sample.gasVoltage = 1.0f + noise;
            }

            sample.gasRaw = (uint16_t)((sample.gasVoltage / 3.3f) * 4095.0f);
            sample.sensorVoltage = sample.gasVoltage * 1.5f;
            sample.busVoltage = 5.01f;
            sample.current_mA = sample.power_mW / 5.01f;
            sample.gasValid = true;
            sample.powerValid = true;
            break;

        case SCENARIO_SUSPICIOUS:
            // Equipment remains OFF (0 mW), but gas suddenly spikes to 3.6V (Physical Inconsistency)
            sample.power_mW = 0.0f;
            sample.equipmentActive = false;
            sample.busVoltage = 4.95f;
            sample.current_mA = 0.0f;
            sample.powerValid = true;

            if (_stepCount > 15) {
                sample.gasVoltage = 3.60f + noise; // Gas spike without power activity
            } else {
                sample.gasVoltage = 1.0f + noise;
            }

            sample.gasRaw = (uint16_t)((sample.gasVoltage / 3.3f) * 4095.0f);
            sample.sensorVoltage = sample.gasVoltage * 1.5f;
            sample.gasValid = true;
            break;

        case SCENARIO_FAULT:
            // Simulates hardware failure/disconnection
            sample.gasRaw = 0xFFFF;
            sample.gasVoltage = 0.0f;
            sample.sensorVoltage = 0.0f;
            sample.gasValid = false;

            sample.busVoltage = 0.0f;
            sample.current_mA = 0.0f;
            sample.power_mW = 0.0f;
            sample.powerValid = false;
            sample.timestampValid = true;
            break;
    }

    return sample.gasValid && sample.powerValid && sample.timestampValid;
}
