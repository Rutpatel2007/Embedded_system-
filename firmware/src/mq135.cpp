#include "mq135.h"
#include "../include/config.h"

MQ135Sensor::MQ135Sensor(uint8_t adcPin) : _adcPin(adcPin) {}

bool MQ135Sensor::begin() {
    pinMode(_adcPin, INPUT);
    // Set 11dB attenuation to allow full 0 - 3.3V input range on ESP32 ADC
    analogSetPinAttenuation(_adcPin, MQ_ADC_ATTEN);
    return true;
}

int MQ135Sensor::readRawADC() {
    return analogRead(_adcPin);
}

float MQ135Sensor::readADCVoltage() {
    int raw = readRawADC();
    // ESP32 12-bit ADC has 4096 steps across 3.3V nominal range
    return (raw / 4095.0f) * 3.3f;
}

float MQ135Sensor::readSensorVoltage() {
    float vAdc = readADCVoltage();
    // Reconstruct V_sensor using the 10k / 20k resistor divider ratio:
    // V_adc = V_sensor * (R2 / (R1 + R2)) -> V_sensor = V_adc * ((R1 + R2) / R2)
    float dividerRatio = (MQ_DIVIDER_R1_OHMS + MQ_DIVIDER_R2_OHMS) / MQ_DIVIDER_R2_OHMS;
    return vAdc * dividerRatio;
}
