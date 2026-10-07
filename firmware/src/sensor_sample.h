#ifndef SENSOR_SAMPLE_H
#define SENSOR_SAMPLE_H

#include <Arduino.h>

// Canonical internal acquisition structure for a single 500ms sensor snapshot
struct SensorSample {
    uint32_t timestamp;        // DS3231 Unix epoch timestamp in seconds
    uint32_t millisMs;         // Monotonic millisecond counter for sub-second jitter diagnostic

    uint16_t gasRaw;           // Raw 12-bit ADC value (0 - 4095)
    float gasVoltage;          // Voltage at ESP32 ADC pin (0 - 3.3V)
    float sensorVoltage;       // Reconstructed MQ-135 voltage prior to divider (0 - 5.0V)

    float busVoltage;          // INA219 bus voltage (V)
    float current_mA;          // INA219 current draw (mA)
    float power_mW;            // INA219 power consumption (mW)

    bool equipmentActive;      // True if power_mW >= POWER_ACTIVE_THRESHOLD_MW

    bool gasValid;             // Sensor readout validity flags
    bool powerValid;
    bool timestampValid;
};

// Power activity entry stored in the 90-second trailing history ring buffer
struct PowerSample {
    uint32_t timestamp;        // Unix epoch in seconds
    uint32_t millisMs;         // Monotonic millisecond counter for exact trailing window math
    float power_mW;
    bool equipmentActive;
};

#endif // SENSOR_SAMPLE_H
