#include "sensor_provider.h"
#include <stdio.h>
#include <string.h>

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
            if (_stepCount > 20 && _stepCount <= 80) {
                sample.power_mW = 850.0f + noise * 100.0f;
                sample.equipmentActive = true;
            } else {
                sample.power_mW = 10.0f;
                sample.equipmentActive = false;
            }

            if (_stepCount > 35 && _stepCount <= 80) {
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
            sample.power_mW = 0.0f;
            sample.equipmentActive = false;
            sample.busVoltage = 4.95f;
            sample.current_mA = 0.0f;
            sample.powerValid = true;

            if (_stepCount > 15) {
                sample.gasVoltage = 3.60f + noise;
            } else {
                sample.gasVoltage = 1.0f + noise;
            }

            sample.gasRaw = (uint16_t)((sample.gasVoltage / 3.3f) * 4095.0f);
            sample.sensorVoltage = sample.gasVoltage * 1.5f;
            sample.gasValid = true;
            break;

        case SCENARIO_FAULT:
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

        case SCENARIO_MATCHING_SENSOR:
        case SCENARIO_WARM_REBOOT:
        case SCENARIO_COLD_BOOT:
            sample.gasRaw = 1241;
            sample.gasVoltage = 1.0f + noise;
            sample.sensorVoltage = sample.gasVoltage * 1.5f;
            sample.gasValid = true;

            sample.busVoltage = 5.0f;
            sample.current_mA = 2.0f;
            sample.power_mW = 10.0f;
            sample.powerValid = true;
            sample.equipmentActive = false;
            break;

        case SCENARIO_REPLACED_SENSOR:
        case SCENARIO_UNENROLLED_NODE:
            sample.gasRaw = 2730;
            sample.gasVoltage = 2.2f + noise;
            sample.sensorVoltage = sample.gasVoltage * 1.5f;
            sample.gasValid = true;

            sample.busVoltage = 5.0f;
            sample.current_mA = 2.0f;
            sample.power_mW = 10.0f;
            sample.powerValid = true;
            sample.equipmentActive = false;
            break;

        case SCENARIO_INSUFFICIENT_HISTORY:
            sample.gasRaw = 0;
            sample.gasVoltage = 0.0f;
            sample.sensorVoltage = 0.0f;
            sample.gasValid = false;

            sample.busVoltage = 5.0f;
            sample.current_mA = 2.0f;
            sample.power_mW = 10.0f;
            sample.powerValid = true;
            sample.equipmentActive = false;
            break;
    }

    return sample.gasValid && sample.powerValid && sample.timestampValid;
}

// ----------------------------------------------------
// ReplaySensorProvider Implementation
// ----------------------------------------------------
ReplaySensorProvider::ReplaySensorProvider(const char* csvPath)
    : _sampleCount(0), _currentIndex(0) {
    strncpy(_csvPath, csvPath != nullptr ? csvPath : "tests/data/truesense_simulation_dataset.csv", sizeof(_csvPath) - 1);
    _csvPath[sizeof(_csvPath) - 1] = '\0';
}

bool ReplaySensorProvider::begin() {
    _currentIndex = 0;
    if (_sampleCount == 0) {
        return loadCSV(_csvPath);
    }
    return true;
}

void ReplaySensorProvider::reset() {
    _currentIndex = 0;
}

bool ReplaySensorProvider::loadCSV(const char* csvPath) {
    if (csvPath != nullptr) {
        strncpy(_csvPath, csvPath, sizeof(_csvPath) - 1);
        _csvPath[sizeof(_csvPath) - 1] = '\0';
    }

    FILE* f = fopen(_csvPath, "r");
    if (!f) {
        f = fopen("tests/data/truesense_simulation_dataset.csv", "r");
        if (!f) {
            f = fopen("../tests/data/truesense_simulation_dataset.csv", "r");
        }
    }

    if (!f) {
        Serial.printf("[REPLAY ERROR] Could not open dataset CSV: %s\n", _csvPath);
        return false;
    }

    char line[256];
    // Skip header line
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return false;
    }

    _sampleCount = 0;
    while (fgets(line, sizeof(line), f) && _sampleCount < MAX_REPLAY_SAMPLES) {
        ReplayRow& row = _samples[_sampleCount];
        memset(&row, 0, sizeof(ReplayRow));

        int parsed = sscanf(
            line,
            "%u,%llu,%u,%31[^,],%hhu,%hu,%f,%f,%f,%f,%f,%15[^,],%31[^\r\n]",
            &row.sample_id,
            (unsigned long long*)&row.timestamp_unix,
            &row.timestamp_ms,
            row.scenario,
            &row.relay_state,
            &row.adc_raw,
            &row.adc_voltage_V,
            &row.gas_voltage_V,
            &row.bus_voltage_V,
            &row.current_mA,
            &row.power_mW,
            row.expected_plausibility,
            row.expected_fingerprint_status
        );

        if (parsed >= 13) {
            _sampleCount++;
        }
    }

    fclose(f);
    _currentIndex = 0;
    Serial.printf("[REPLAY INIT] Loaded %u samples from CSV dataset: %s\n", (unsigned int)_sampleCount, _csvPath);
    return (_sampleCount > 0);
}

bool ReplaySensorProvider::readSample(SensorSample& sample) {
    if (_sampleCount == 0 || _currentIndex >= _sampleCount) {
        return false;
    }

    const ReplayRow& row = _samples[_currentIndex++];

    memset(&sample, 0, sizeof(SensorSample));
    sample.timestamp = row.timestamp_unix;
    sample.millisMs = row.timestamp_ms;
    sample.timestampValid = true;

    sample.gasRaw = row.adc_raw;
    sample.gasVoltage = row.gas_voltage_V;
    sample.sensorVoltage = row.gas_voltage_V * 1.5f;
    sample.gasValid = true;

    sample.busVoltage = row.bus_voltage_V;
    sample.current_mA = row.current_mA;
    sample.power_mW = row.power_mW;
    sample.powerValid = true;

    sample.equipmentActive = (row.relay_state != 0) || (sample.power_mW >= POWER_ACTIVE_THRESHOLD_MW);

    return true;
}
