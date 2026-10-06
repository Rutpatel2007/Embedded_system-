#include <Arduino.h>
#include <Wire.h>
#include "../include/config.h"
#include "relay.h"
#include "mq135.h"
#include "ina219_sensor.h"
#include "rtc.h"
#include "sd_storage.h"

// Instantiate Hardware Peripheral Objects
RelayController relay(RELAY_PIN);
MQ135Sensor mq135(MQ_ADC_PIN);
INA219Sensor ina219(INA219_I2C_ADDRESS);
RTCDriver rtc(RTC_ADDRESS);
SDStorage sdStorage(SD_CS);

// Hardware Self-Test Pass/Fail Flags
bool passI2C = false;
bool passINA219 = false;
bool passRTC = false;
bool passMQ135 = false;
bool passSD = false;
bool passRelay = false;

// Scan I2C bus for target peripherals (0x40 INA219, 0x68 DS3231)
void scanI2CBus() {
    Serial.println("\n--- Starting I2C Bus Scan ---");
    bool foundINA = false;
    bool foundRTC = false;

    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("Found I2C Device at 0x%02X\n", addr);
            if (addr == INA219_I2C_ADDRESS) foundINA = true;
            if (addr == RTC_ADDRESS) foundRTC = true;
        }
    }

    if (!foundINA) Serial.println("ERROR: INA219 (0x40) not detected on I2C bus");
    if (!foundRTC) Serial.println("ERROR: DS3231 RTC (0x68) not detected on I2C bus");

    passI2C = (foundINA && foundRTC);
    Serial.printf("I2C Bus Scan Result: %s\n", passI2C ? "PASS" : "FAIL");
}

// Process Serial input commands (e.g., T1750000000 to set RTC timestamp)
void handleSerialCommands() {
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.startsWith("T") || input.startsWith("t")) {
            String epochStr = input.substring(1);
            uint32_t newEpoch = strtoul(epochStr.c_str(), NULL, 10);
            if (newEpoch > 0) {
                rtc.setUnixTimestamp(newEpoch);
                Serial.printf("[RTC] Set new epoch timestamp: %u -> %s\n", newEpoch, rtc.getFormattedDateTime().c_str());
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n==========================================");
    Serial.println("  TrueSense Firmware v0.1.0");
    Serial.println("  Phase 1: Hardware Bring-Up & Diagnostics");
    Serial.println("==========================================");
    Serial.printf("ESP32 Chip Model: %s (Rev %d)\n", ESP.getChipModel(), ESP.getChipRevision());
    Serial.printf("CPU Cores: %d @ %d MHz\n", ESP.getChipCores(), ESP.getCpuFreqMHz());
    Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());

    // 1. Initialize I2C Bus
    Wire.begin(I2C_SDA, I2C_SCL);
    scanI2CBus();

    // 2. Initialize INA219 Power Sensor
    passINA219 = ina219.begin();
    if (passINA219) {
        Serial.printf("[PASS] INA219 initialized. Bus Voltage: %.2f V, Current: %.1f mA, Power: %.1f mW\n",
                      ina219.readBusVoltage(), ina219.readCurrent(), ina219.readPower());
    } else {
        Serial.println("[FAIL] INA219 initialization failed.");
    }

    // 3. Initialize DS3231 RTC
    passRTC = rtc.begin();
    if (passRTC) {
        Serial.printf("[PASS] DS3231 RTC initialized. Current Time: %s (Epoch: %u)\n",
                      rtc.getFormattedDateTime().c_str(), rtc.getUnixTimestamp());
    } else {
        Serial.println("[FAIL] DS3231 RTC initialization failed.");
    }

    // 4. Initialize MQ-135 Gas Sensor
    passMQ135 = mq135.begin();
    if (passMQ135) {
        int rawAdc = mq135.readRawADC();
        float vSensor = mq135.readSensorVoltage();
        Serial.printf("[PASS] MQ-135 initialized. Raw ADC: %d, Sensor Voltage: %.3f V\n", rawAdc, vSensor);
    } else {
        Serial.println("[FAIL] MQ-135 initialization failed.");
    }

    // 5. Initialize microSD Storage
    passSD = sdStorage.begin();
    if (passSD) {
        bool diagOk = sdStorage.runDiagnostic();
        Serial.printf("[PASS] microSD Card initialized. Type: %s, Size: %llu MB, Diagnostic: %s\n",
                      sdStorage.getCardType().c_str(), sdStorage.getCardSizeMB(), diagOk ? "OK" : "FAIL");
    } else {
        Serial.println("[FAIL] microSD Card initialization failed or card absent.");
    }

    // 6. Initialize Relay Controller (Default Active-HIGH)
    relay.begin(true);
    relay.off();
    passRelay = true;
    Serial.println("[PASS] Relay Controller initialized (Domain 2 Equipment OFF).");

    // Print Hardware Bring-Up Self-Test Summary
    Serial.println("\n==========================================");
    Serial.println("  TrueSense Hardware Self-Test Summary");
    Serial.println("==========================================");
    Serial.printf("[%s] I2C Bus Detection\n", passI2C ? "PASS" : "FAIL");
    Serial.printf("[%s] INA219 Power Sensor\n", passINA219 ? "PASS" : "FAIL");
    Serial.printf("[%s] DS3231 Real-Time Clock\n", passRTC ? "PASS" : "FAIL");
    Serial.printf("[%s] MQ-135 Gas Sensor ADC\n", passMQ135 ? "PASS" : "FAIL");
    Serial.printf("[%s] microSD Persistent Storage\n", passSD ? "PASS" : "FAIL");
    Serial.printf("[%s] Equipment Relay Control\n", passRelay ? "PASS" : "FAIL");
    
    bool overallPass = passI2C && passINA219 && passRTC && passMQ135 && passSD && passRelay;
    Serial.printf("\nTrueSense Hardware Bring-Up Overall Status: %s\n", overallPass ? "PASS" : "FAIL (Check missing peripherals)");
    Serial.println("==========================================\n");
    Serial.println("Type 'T<unix_epoch>' in Serial Monitor to set DS3231 RTC time.\n");
}

void loop() {
    handleSerialCommands();

    // Periodically log sensor telemetry every 3 seconds
    static uint32_t lastLogMs = 0;
    if (millis() - lastLogMs >= 3000) {
        lastLogMs = millis();

        uint32_t timestamp = rtc.getUnixTimestamp();
        int mqRaw = mq135.readRawADC();
        float mqVolt = mq135.readSensorVoltage();
        float busV = ina219.readBusVoltage();
        float currentmA = ina219.readCurrent();
        float powermW = ina219.readPower();
        bool eqState = relay.isEquipmentOn();

        Serial.printf("[TELEMETRY] Epoch: %u | MQ Raw: %4d, Volt: %.3f V | INA Bus: %.2f V, Current: %6.1f mA, Power: %6.1f mW | Relay: %s\n",
                      timestamp, mqRaw, mqVolt, busV, currentmA, powermW, eqState ? "ON" : "OFF");
    }
}
