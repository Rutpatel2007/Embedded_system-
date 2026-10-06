#include <Arduino.h>
#include <Wire.h>
#include "../include/config.h"
#include "relay.h"
#include "mq135.h"
#include "ina219_sensor.h"
#include "rtc.h"
#include "sd_storage.h"
#include "sensor_provider.h"
#include "rtos_tasks.h"
#include "layer1_tests.h"

// Instantiate Physical Hardware Peripheral Objects
RelayController relay(RELAY_PIN);
MQ135Sensor mq135(MQ_ADC_PIN);
INA219Sensor ina219(INA219_I2C_ADDRESS);
RTCDriver rtc(RTC_ADDRESS);
SDStorage sdStorage(SD_CS);

// Sensor Providers
HardwareSensorProvider hwProvider(mq135, ina219, rtc, relay);
MockSensorProvider mockProvider(SCENARIO_PLAUSIBLE); // Default simulation scenario

ISensorProvider* activeProvider = NULL;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n==========================================");
    Serial.println("  TrueSense Firmware v0.3.0");
    Serial.println("  Phase 3A: Layer 1 Engine & Hardened Suite");
    Serial.println("==========================================");
    Serial.printf("ESP32 Chip Model: %s (Rev %d)\n", ESP.getChipModel(), ESP.getChipRevision());
    Serial.printf("CPU Cores: %d @ %d MHz\n", ESP.getChipCores(), ESP.getCpuFreqMHz());
    Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());

    // Execute Independent Layer 1 Deterministic Software Test Suite
    Layer1TestSuite::runAllTests();

#ifdef USE_MOCK_SENSORS
    activeProvider = &mockProvider;
    Serial.println("\n[MODE] MOCK SENSOR PROVIDER (SIMULATION VALIDATED)");
    Serial.println("[MODE] Running offline test harness scenario: SCENARIO_PLAUSIBLE");
    activeProvider->begin();
#else
    activeProvider = &hwProvider;
    Serial.println("\n[MODE] REAL HARDWARE PROVIDER (PHYSICAL HARDWARE VALIDATED)");
    
    Wire.begin(I2C_SDA, I2C_SCL);
    bool hwOk = activeProvider->begin();
    Serial.printf("Hardware Drivers Initialization: %s\n", hwOk ? "PASS" : "FAIL");
    
    sdStorage.begin();
#endif

    // Initialize FreeRTOS Queues and Tasks
    if (initRTOSTasks(activeProvider)) {
        Serial.println("[RTOS] All tasks successfully started!");
    } else {
        Serial.println("[RTOS CRITICAL ERROR] Failed to start tasks!");
    }

    Serial.println("==========================================\n");
}

void loop() {
    // In FreeRTOS architecture, setup() initializes tasks and loop() remains yield-friendly
    vTaskDelay(pdMS_TO_TICKS(1000));
}
