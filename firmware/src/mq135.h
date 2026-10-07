#ifndef MQ135_H
#define MQ135_H

#include <Arduino.h>

class MQ135Sensor {
public:
    MQ135Sensor(uint8_t adcPin = 34);

    // Initialize ADC pin and configure ESP32 attenuation
    bool begin();

    // Read raw 12-bit ADC value (0 - 4095)
    int readRawADC();

    // Read voltage at the ESP32 ADC pin (0 - 3.3V)
    float readADCVoltage();

    // Calculate actual MQ-135 output voltage prior to 10k/20k divider (V_sensor = V_adc * 1.5)
    float readSensorVoltage();

private:
    uint8_t _adcPin;
};

#endif // MQ135_H
