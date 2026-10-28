#include "sensor_task.h"
#include "config.h"
#include "power_task.h"
#include "rtc_time.h"
#include "serial_cmd.h"
#include "plausibility.h"
#include "fingerprint.h"
#include "boot_monitor.h"
#include <Preferences.h>
#include <math.h>

enum SensorCommand : uint8_t {
    SENSOR_COMMAND_CAL,
    SENSOR_COMMAND_GAS
};

static QueueHandle_t sensorCommandQueue;
static uint32_t sensorQueueDrops;
static portMUX_TYPE sensorDropMux = portMUX_INITIALIZER_UNLOCKED;

static void requestSensorCommand(SensorCommand command) {
    if (sensorCommandQueue == nullptr || xQueueSend(sensorCommandQueue, &command, 0) != pdPASS) {
        Serial.println(SERIAL_HANDLER_QUEUE_FULL);
    }
}

static void handleCalCommand(const char* args) {
    static_cast<void>(args);
    requestSensorCommand(SENSOR_COMMAND_CAL);
}

static void handleGasCommand(const char* args) {
    static_cast<void>(args);
    requestSensorCommand(SENSOR_COMMAND_GAS);
}

static uint32_t calculateRsOhms(uint32_t rawMilliVolts) {
    if (rawMilliVolts == 0 || rawMilliVolts >= MQ_VCC_MV) {
        return 0;
    }
    uint64_t numerator = static_cast<uint64_t>(MQ_RL_KOHM) * MQ_RL_OHM_SCALE * (MQ_VCC_MV - rawMilliVolts);
    return static_cast<uint32_t>(numerator / rawMilliVolts);
}

static void loadR0(GasState& state) {
    Preferences preferences;
    if (preferences.begin("proov-gas", true)) {
        state.r0Ohms = preferences.getUInt("r0_ohms", 0);
        preferences.end();
    }
    state.calibrated = state.r0Ohms > 0;
}

namespace sensor_task_module {
void init() {
    pinMode(MQ_ADC_PIN, INPUT);
    analogReadResolution(ADC_RESOLUTION_BITS);
    analogSetPinAttenuation(MQ_ADC_PIN, ADC_11db);
    sensorCommandQueue = xQueueCreate(SERIAL_REQUEST_QUEUE_LENGTH, sizeof(SensorCommand));
}

void registerSensorCommands() {
    serial_cmd_module::registerCommand("CAL", handleCalCommand);
    serial_cmd_module::registerCommand("GAS", handleGasCommand);
}

uint32_t readRawADC() {
    uint64_t totalMilliVolts = 0;
    for (uint32_t sample = 0; sample < ADC_SAMPLES; ++sample) {
        totalMilliVolts += analogReadMilliVolts(MQ_ADC_PIN);
    }
    uint32_t measuredMilliVolts = static_cast<uint32_t>(totalMilliVolts / ADC_SAMPLES);
    return static_cast<uint32_t>((static_cast<uint64_t>(measuredMilliVolts) * MQ_DIVIDER_RATIO) / MQ_DIVIDER_RATIO_SCALE);
}

bool calibrateBaseline(GasState& state) {
    uint64_t totalRsOhms = 0;
    uint32_t validSamples = 0;
    for (uint32_t sample = 0; sample < CAL_SAMPLES; ++sample) {
        uint32_t rawMilliVolts = readRawADC();
        uint32_t rsOhms = calculateRsOhms(rawMilliVolts);
        if (rsOhms > 0) {
            totalRsOhms += rsOhms;
            ++validSamples;
        }
        vTaskDelay(pdMS_TO_TICKS(CAL_SAMPLE_INTERVAL_MS));
    }
    if (validSamples == 0) {
        Serial.println("CAL failed: invalid sensor voltage");
        return false;
    }
    uint32_t averageRsOhms = static_cast<uint32_t>(totalRsOhms / validSamples);
    state.r0Ohms = static_cast<uint32_t>((static_cast<uint64_t>(averageRsOhms) * MQ_CLEAN_AIR_RATIO_SCALE) / MQ_CLEAN_AIR_RS_R0_X1000);
    state.calibrated = state.r0Ohms > 0;
    Preferences preferences;
    bool stored = preferences.begin("proov-gas", false);
    if (stored) {
        stored = preferences.putUInt("r0_ohms", state.r0Ohms) > 0;
        preferences.end();
    }
    Serial.printf("R0=%lu ohms stored=%u\n", static_cast<unsigned long>(state.r0Ohms), static_cast<unsigned int>(stored));
    return stored;
}

uint32_t convertToPPM(uint32_t rsOhms, uint32_t r0Ohms) {
    if (rsOhms == 0 || r0Ohms == 0) {
        return 0;
    }
    double ratio = static_cast<double>(rsOhms) / static_cast<double>(r0Ohms);
    double ppm = (static_cast<double>(MQ_CO2_A_X1000) / MQ_CO2_A_SCALE) * pow(ratio, static_cast<double>(MQ_CO2_B_X1000000) / MQ_CO2_B_SCALE);
    if (!isfinite(ppm) || ppm <= 0.0) {
        return 0;
    }
    double scaledPpm = ppm * MQ_PPM_SCALE;
    return scaledPpm >= MQ_MAX_PPM_X100 ? MQ_MAX_PPM_X100 : static_cast<uint32_t>(scaledPpm + 0.5);
}

void updateBaseline(GasState& state, uint32_t gasPpmX100) {
    if (state.windowCount > 0 && static_cast<uint64_t>(gasPpmX100) * PERCENT_SCALE > static_cast<uint64_t>(state.baselinePpmX100) * (PERCENT_SCALE + SPIKE_THRESHOLD_PCT)) {
        return;
    }
    if (state.windowCount < BASELINE_WINDOW) {
        state.baselineWindow[state.windowCount++] = gasPpmX100;
        state.baselineSum += gasPpmX100;
    } else {
        state.baselineSum -= state.baselineWindow[state.windowIndex];
        state.baselineWindow[state.windowIndex] = gasPpmX100;
        state.baselineSum += gasPpmX100;
        state.windowIndex = (state.windowIndex + 1U) % BASELINE_WINDOW;
    }
    state.baselinePpmX100 = static_cast<uint32_t>(state.baselineSum / state.windowCount);
}

uint32_t getBaseline_x100(const GasState& state) {
    return state.baselinePpmX100;
}

uint32_t getQueueDropCount() {
    portENTER_CRITICAL(&sensorDropMux);
    uint32_t drops = sensorQueueDrops;
    portEXIT_CRITICAL(&sensorDropMux);
    return drops;
}

void startSensorTask(QueueHandle_t outputQueue) {
    xTaskCreate(sensorTask, "sensor_task", STACK_SIZE_DEFAULT, outputQueue, PRIORITY_SENSOR, nullptr);
}

void sensorTask(void* parameter) {
    QueueHandle_t outputQueue = static_cast<QueueHandle_t>(parameter);
    uint32_t sequence = 0;
    GasState gasState{};
    loadR0(gasState);
    uint32_t warmupStart = millis();
    uint32_t lastWarmupNotice = UINT32_MAX;
    uint8_t fingerprintStatus = FP_STATUS_PENDING;
    QueueHandle_t fingerprintStatusQueue = fingerprint_module::getStatusQueue();
    TickType_t lastWake = xTaskGetTickCount();
    uint32_t lastStackReport = millis();
    for (;;) {
        boot_monitor_module::feedWatchdog();
        boot_monitor_module::reportTaskStack("sensor", lastStackReport);
        uint8_t updatedFingerprintStatus;
        while (fingerprintStatusQueue != nullptr && xQueueReceive(fingerprintStatusQueue, &updatedFingerprintStatus, 0) == pdTRUE) {
            fingerprintStatus = updatedFingerprintStatus;
        }
        SensorCommand command;
        while (xQueueReceive(sensorCommandQueue, &command, 0) == pdTRUE) {
            if (command == SENSOR_COMMAND_CAL) {
                if (millis() - warmupStart < WARMUP_MS) {
                    Serial.println("CAL unavailable during warm-up");
                } else {
                    Serial.println(calibrateBaseline(gasState) ? "Gas calibration complete" : "Gas calibration failed");
                }
            } else if (command == SENSOR_COMMAND_GAS) {
                Serial.printf("raw_mV=%lu Rs_ohms=%lu R0_ohms=%lu ppm_x100=%lu baseline_x100=%lu\n", static_cast<unsigned long>(gasState.rawMilliVolts), static_cast<unsigned long>(gasState.rsOhms), static_cast<unsigned long>(gasState.r0Ohms), static_cast<unsigned long>(convertToPPM(gasState.rsOhms, gasState.r0Ohms)), static_cast<unsigned long>(getBaseline_x100(gasState)));
            }
        }
        int32_t currentMilliAmps = power_task_module::readCurrent_mA();
        int32_t powerMilliWatts = power_task_module::readPower_mW();
        bool active = power_task_module::isEquipmentActive();
        bool rtcLostPower = rtc_time_module::rtcLostPower();
        uint32_t timestamp = rtc_time_module::getTimestamp();
        uint32_t elapsedWarmup = millis() - warmupStart;
        uint32_t gasPpmX100 = 0;
        if (elapsedWarmup < WARMUP_MS) {
            uint32_t remainingSeconds = (WARMUP_MS - elapsedWarmup + WARMUP_ROUNDING_MS) / MILLISECONDS_PER_SECOND;
            if (remainingSeconds != lastWarmupNotice) {
                Serial.printf("WARMUP %lu\n", static_cast<unsigned long>(remainingSeconds));
                lastWarmupNotice = remainingSeconds;
            }
        } else {
            gasState.rawMilliVolts = readRawADC();
            gasState.rsOhms = calculateRsOhms(gasState.rawMilliVolts);
            if (gasState.calibrated) {
                gasPpmX100 = convertToPPM(gasState.rsOhms, gasState.r0Ohms);
                updateBaseline(gasState, gasPpmX100);
            }
        }
        int plausibilityFlag = plausibility_module::checkPlausibility(gasPpmX100, getBaseline_x100(gasState), active);
        ProovRecord record{sequence++, static_cast<int32_t>(gasPpmX100), powerMilliWatts, timestamp, currentMilliAmps, static_cast<uint8_t>(active), static_cast<uint8_t>(rtcLostPower), plausibilityFlag, fingerprintStatus};
        if (xQueueSend(outputQueue, &record, 0) != pdPASS) {
            portENTER_CRITICAL(&sensorDropMux);
            ++sensorQueueDrops;
            portEXIT_CRITICAL(&sensorDropMux);
        }
        Serial.printf("timestamp=%lu raw_mV=%lu gas_ppm_x100=%lu baseline_x100=%lu power_mW=%ld active=%u calibrated=%u\n", static_cast<unsigned long>(record.timestamp), static_cast<unsigned long>(gasState.rawMilliVolts), static_cast<unsigned long>(gasPpmX100), static_cast<unsigned long>(getBaseline_x100(gasState)), static_cast<long>(record.powerMilliWatts), static_cast<unsigned int>(record.equipmentActive), static_cast<unsigned int>(gasState.calibrated));
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }
}
}
