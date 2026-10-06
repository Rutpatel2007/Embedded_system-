#include "ina219_sensor.h"
#include <Wire.h>

INA219Sensor::INA219Sensor(uint8_t address)
    : _address(address), _ina219(address), _initialized(false) {}

bool INA219Sensor::begin() {
    _initialized = _ina219.begin();
    return _initialized;
}

bool INA219Sensor::isHealthy() {
    if (!_initialized) return false;
    Wire.beginTransmission(_address);
    return (Wire.endTransmission() == 0);
}

float INA219Sensor::readBusVoltage() {
    if (!_initialized) return 0.0f;
    return _ina219.getBusVoltage_V();
}

float INA219Sensor::readShuntVoltage() {
    if (!_initialized) return 0.0f;
    return _ina219.getShuntVoltage_mV();
}

float INA219Sensor::readCurrent() {
    if (!_initialized) return 0.0f;
    return _ina219.getCurrent_mA();
}

float INA219Sensor::readPower() {
    if (!_initialized) return 0.0f;
    return _ina219.getPower_mW();
}
