#include "sensor_task.h"
#include "config.h"
#include "power_task.h"
#include "rtc_time.h"
#include "serial_cmd.h"
#include "plausibility.h"
#include "fingerprint.h"
#include "storage.h"
#include "boot_monitor.h"
#include <Preferences.h>
#include <math.h>
#include <strings.h>

enum SensorCommand : uint8_t {
    SENSOR_COMMAND_CAL,
    SENSOR_COMMAND_GAS,
    SENSOR_COMMAND_CAL_CLEAR
};

static QueueHandle_t sensorCommandQueue;
static uint32_t sensorQueueDrops;
static bool sensorCalibrated;
static portMUX_TYPE sensorDropMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t lastNoSignalNotice;

static const char* plausibilityName(int value) {
    if (value == PLAUSIBILITY_PLAUSIBLE) return "PLAUSIBLE";
    if (value == PLAUSIBILITY_SUSPICIOUS) return "SUSPICIOUS";
    return "NORMAL";
}

static const char* fingerprintName(uint8_t value) {
    if (value == FP_STATUS_OK) return "OK";
    if (value == FP_STATUS_MISMATCH) return "MISMATCH";
    if (value == FP_STATUS_NOT_ENROLLED) return "NOT_ENROLLED";
    return "PENDING";
}

static void printStateLine(uint32_t timestamp, uint32_t gas, uint32_t baseline, uint32_t rawMv, int32_t power, bool equipment, const char* equipmentMode, int plausibility, uint8_t fingerprint) {
    bool distanceAvailable = false;
    uint64_t distance = fingerprint_module::getDistance(distanceAvailable);
    PlausibilityStats stats = plausibility_module::getPlausibilityStats();
    bool verified = storage_module::hasLastVerify();
    const char* verifyText = verified ? (storage_module::lastVerifyPassed() ? "PASS" : "FAIL") : (boot_monitor_module::bootVerifyCount() > 0 ? (boot_monitor_module::bootVerifyPassed() ? "PASS" : "FAIL") : "NONE");
    int32_t failure = verified ? storage_module::lastVerifyFailureIndex() : boot_monitor_module::bootVerifyFailureIndex();
    Serial.printf("STATE {\"t\":%lu,\"gas\":%lu,\"base\":%lu,\"mv\":%lu,\"pw\":%ld,\"eq\":%u,\"eqm\":\"%s\",\"pl\":\"%s\",\"fp\":\"%s\",\"fpd\":%lld,\"chain\":%lu,\"ver\":\"%s\",\"vfail\":%ld,\"n\":%lu,\"p\":%lu,\"s\":%lu,\"i2c\":%lu}\n", static_cast<unsigned long>(timestamp), static_cast<unsigned long>(gas), static_cast<unsigned long>(baseline), static_cast<unsigned long>(rawMv), static_cast<long>(power), static_cast<unsigned int>(equipment), equipmentMode, plausibilityName(plausibility), fingerprintName(fingerprint), static_cast<long long>(distanceAvailable ? static_cast<int64_t>(distance) : -1), static_cast<unsigned long>(storage_module::cachedTodayRecordCount()), verifyText, static_cast<long>(failure), static_cast<unsigned long>(stats.normalCount), static_cast<unsigned long>(stats.plausibleCount), static_cast<unsigned long>(stats.suspiciousCount), static_cast<unsigned long>(power_task_module::i2cErrorCount()));
}

static void requestSensorCommand(SensorCommand command) {
    if (sensorCommandQueue == nullptr || xQueueSend(sensorCommandQueue, &command, 0) != pdPASS) {
        Serial.println(SERIAL_HANDLER_QUEUE_FULL);
    }
}

static void handleCalCommand(const char* args) {
    requestSensorCommand(args != nullptr && strcasecmp(args, "CLEAR") == 0 ? SENSOR_COMMAND_CAL_CLEAR : SENSOR_COMMAND_CAL);
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
    return static_cast<uint32_t>(plausibility_module::safeDivide(numerator, rawMilliVolts));
}

static void loadR0(GasState& state) {
    Preferences preferences;
    if (preferences.begin(GAS_NVS_NAMESPACE, false)) {
        state.r0Ohms = preferences.isKey(GAS_NVS_R0_KEY) ? preferences.getUInt(GAS_NVS_R0_KEY, 0) : 0;
        preferences.end();
    }
    state.calibrated = state.r0Ohms > 0 && state.r0Ohms != UINT32_MAX;
    if (!state.calibrated) state.r0Ohms = 0;
    sensorCalibrated = state.calibrated;
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
    if (ADC_SAMPLES == 0 || MQ_DIVIDER_RATIO_SCALE == 0) return 0;
    uint64_t totalMilliVolts = 0;
    for (uint32_t sample = 0; sample < ADC_SAMPLES; ++sample) {
        totalMilliVolts += analogReadMilliVolts(MQ_ADC_PIN);
    }
    uint32_t measuredMilliVolts = static_cast<uint32_t>(plausibility_module::safeDivide(totalMilliVolts, ADC_SAMPLES));
    return static_cast<uint32_t>(plausibility_module::safeDivide(static_cast<uint64_t>(measuredMilliVolts) * MQ_DIVIDER_RATIO, MQ_DIVIDER_RATIO_SCALE));
}

bool calibrateBaseline(GasState& state) {
    uint64_t totalRsOhms = 0;
    uint64_t totalMilliVolts = 0;
    uint32_t validSamples = 0;
    for (uint32_t sample = 0; sample < CAL_SAMPLES; ++sample) {
        uint32_t rawMilliVolts = readRawADC();
        totalMilliVolts += rawMilliVolts;
        uint32_t rsOhms = calculateRsOhms(rawMilliVolts);
        if (rsOhms > 0) {
            totalRsOhms += rsOhms;
            ++validSamples;
        }
        vTaskDelay(pdMS_TO_TICKS(CAL_SAMPLE_INTERVAL_MS));
    }
    uint32_t averageMilliVolts = static_cast<uint32_t>(plausibility_module::safeDivide(totalMilliVolts, CAL_SAMPLES));
    if (averageMilliVolts < SENSOR_MIN_VALID_MV || validSamples == 0 || MQ_CLEAN_AIR_RATIO_SCALE == 0 || MQ_CLEAN_AIR_RS_R0_X1000 == 0) {
        Serial.println("CAL FAILED sensor not reading");
        return false;
    }
    uint32_t averageRsOhms = static_cast<uint32_t>(plausibility_module::safeDivide(totalRsOhms, validSamples));
    state.r0Ohms = static_cast<uint32_t>(plausibility_module::safeDivide(static_cast<uint64_t>(averageRsOhms) * MQ_CLEAN_AIR_RATIO_SCALE, MQ_CLEAN_AIR_RS_R0_X1000));
    if (state.r0Ohms == 0) { Serial.println("CAL FAILED sensor not reading"); return false; }
    state.calibrated = state.r0Ohms > 0;
    sensorCalibrated = state.calibrated;
    Preferences preferences;
    bool stored = preferences.begin(GAS_NVS_NAMESPACE, false);
    if (stored) {
        stored = preferences.putUInt(GAS_NVS_R0_KEY, state.r0Ohms) > 0;
        preferences.end();
    }
    Serial.printf("R0=%lu ohms stored=%u\n", static_cast<unsigned long>(state.r0Ohms), static_cast<unsigned int>(stored));
    return stored;
}

uint32_t convertToPPM(uint32_t rsOhms, uint32_t r0Ohms) {
    if (rsOhms == 0 || r0Ohms == 0 || MQ_CO2_A_SCALE == 0 || MQ_CO2_B_SCALE == 0) {
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
    if (BASELINE_WINDOW == 0) return;
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
    if (state.windowCount > 0) state.baselinePpmX100 = static_cast<uint32_t>(state.baselineSum / state.windowCount);
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

bool isCalibrated() { return sensorCalibrated; }

void startSensorTask(QueueHandle_t outputQueue) {
    if (outputQueue == nullptr || sensorCommandQueue == nullptr) return;
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
    uint32_t lastStateLine = millis();
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
            } else if (command == SENSOR_COMMAND_CAL_CLEAR) {
                Preferences preferences;
                bool cleared = preferences.begin(GAS_NVS_NAMESPACE, false);
                if (cleared) { if (preferences.isKey(GAS_NVS_R0_KEY)) preferences.remove(GAS_NVS_R0_KEY); preferences.end(); }
                gasState.r0Ohms = 0;
                gasState.calibrated = false;
                sensorCalibrated = false;
                Serial.println(cleared ? "CAL CLEAR OK" : "CAL CLEAR FAILED");
            }
        }
        int32_t currentMilliAmps = power_task_module::readCurrent_mA();
        int32_t powerMilliWatts = power_task_module::readPower_mW();
        bool active = power_task_module::isEquipmentActive(powerMilliWatts);
        bool rtcLostPower = rtc_time_module::rtcLostPower();
        uint32_t timestamp = rtc_time_module::getTimestamp();
        uint32_t elapsedWarmup = millis() - warmupStart;
        uint32_t gasPpmX100 = 0;
        gasState.rawMilliVolts = readRawADC();
        if (gasState.rawMilliVolts < SENSOR_MIN_VALID_MV && millis() - lastNoSignalNotice >= 5000) {
            lastNoSignalNotice = millis();
            Serial.println("MQ135 NO SIGNAL check GPIO34 wiring");
        }
        if (elapsedWarmup < WARMUP_MS) {
            uint32_t remainingSeconds = MILLISECONDS_PER_SECOND > 0 ? (WARMUP_MS - elapsedWarmup + WARMUP_ROUNDING_MS) / MILLISECONDS_PER_SECOND : 0;
            if (remainingSeconds != lastWarmupNotice) {
#if DEBUG_VERBOSE
                Serial.printf("WARMUP %lu\n", static_cast<unsigned long>(remainingSeconds));
#endif
                lastWarmupNotice = remainingSeconds;
            }
        } else {
            if (gasState.rawMilliVolts < SENSOR_MIN_VALID_MV) {
                gasState.rsOhms = 0;
            } else {
            gasState.rsOhms = calculateRsOhms(gasState.rawMilliVolts);
            if (gasState.calibrated && gasState.r0Ohms > 0) {
                gasPpmX100 = convertToPPM(gasState.rsOhms, gasState.r0Ohms);
                updateBaseline(gasState, gasPpmX100);
            }
            }
        }
        bool sensorValid = elapsedWarmup < WARMUP_MS || gasState.rawMilliVolts >= SENSOR_MIN_VALID_MV;
        int plausibilityFlag = sensorValid ? plausibility_module::checkPlausibility(gasPpmX100, getBaseline_x100(gasState), active) : plausibility_module::invalidSensorSample();
        TrueSenseRecord record{sequence++, static_cast<int32_t>(gasPpmX100), powerMilliWatts, timestamp, currentMilliAmps, static_cast<uint8_t>(active), static_cast<uint8_t>(rtcLostPower), plausibilityFlag, fingerprintStatus};
        if (xQueueSend(outputQueue, &record, 0) != pdPASS) {
            portENTER_CRITICAL(&sensorDropMux);
            ++sensorQueueDrops;
            portEXIT_CRITICAL(&sensorDropMux);
        }
        Serial.printf("timestamp=%lu raw_mV=%lu gas_ppm_x100=%lu baseline_x100=%lu power_mW=%ld active=%u calibrated=%u equip_mode=%s plausibility=%s fingerprint=%s\n", static_cast<unsigned long>(record.timestamp), static_cast<unsigned long>(gasState.rawMilliVolts), static_cast<unsigned long>(gasPpmX100), static_cast<unsigned long>(getBaseline_x100(gasState)), static_cast<long>(record.powerMilliWatts), static_cast<unsigned int>(record.equipmentActive), static_cast<unsigned int>(gasState.calibrated), power_task_module::equipmentModeName(), plausibilityName(plausibilityFlag), fingerprintName(fingerprintStatus));
        uint32_t stateNow = millis();
        if (stateNow - lastStateLine >= STATE_INTERVAL_MS) {
            lastStateLine = stateNow;
            printStateLine(record.timestamp, gasPpmX100, getBaseline_x100(gasState), gasState.rawMilliVolts, powerMilliWatts, active, power_task_module::equipmentModeName(), plausibilityFlag, fingerprintStatus);
        }
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }
}
}
