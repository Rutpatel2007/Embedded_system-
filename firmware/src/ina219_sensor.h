#ifndef INA219_SENSOR_H
#define INA219_SENSOR_H

#include <Arduino.h>
#include <Adafruit_INA219.h>

class INA219Sensor {
public:
    INA219Sensor(uint8_t address = 0x40);

    // Initialize I2C communication and calibrate INA219 sensor
    bool begin();

    // Check if INA219 sensor responds on I2C bus
    bool isHealthy();

    // Read bus voltage in Volts (V)
    float readBusVoltage();

    // Read shunt voltage in millivolts (mV)
    float readShuntVoltage();

    // Read current draw in milliamperes (mA)
    float readCurrent();

    // Read power consumption in milliwatts (mW)
    float readPower();

private:
    uint8_t _address;
    Adafruit_INA219 _ina219;
    bool _initialized;
};

#endif // INA219_SENSOR_H
