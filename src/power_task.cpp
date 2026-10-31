#include "power_task.h"
#include "config.h"
#include "serial_cmd.h"
#include "boot_monitor.h"
#include <Adafruit_INA219.h>
#include <Preferences.h>
#include <strings.h>
#include <math.h>

static Adafruit_INA219 ina219(INA219_I2C_ADDRESS);
static Preferences preferences;
static bool sensorAvailable;
static bool preferencesAvailable;
static int32_t idleBaselineMilliWatts;
static bool equipmentActive;
static QueueHandle_t powerCommandQueue;
static QueueHandle_t equipmentModeQueue;
static uint32_t powerI2cErrors;
static int32_t lastValidCurrentMilliAmps;
static int32_t lastValidPowerMilliWatts;

static void handleEquipCommand(const char* args) {
    uint8_t request;
    if (strcasecmp(args, "ON") == 0) {
        request = POWER_COMMAND_EQUIP_ON;
    } else if (strcasecmp(args, "OFF") == 0) {
        request = POWER_COMMAND_EQUIP_OFF;
    } else if (strcasecmp(args, "AUTO") == 0) {
        request = POWER_COMMAND_EQUIP_AUTO;
    } else {
        Serial.println("EQUIP usage: EQUIP ON|OFF|AUTO");
        return;
    }
    if (powerCommandQueue == nullptr || xQueueSend(powerCommandQueue, &request, 0) != pdPASS) {
        Serial.println(SERIAL_HANDLER_QUEUE_FULL);
    }
}

static void handleIdleCommand(const char* args) {
    static_cast<void>(args);
    uint8_t request = POWER_COMMAND_IDLE;
    if (powerCommandQueue == nullptr || xQueueSend(powerCommandQueue, &request, 0) != pdPASS) {
        Serial.println(SERIAL_HANDLER_QUEUE_FULL);
    }
}

namespace power_task_module {
bool init() {
    powerCommandQueue = xQueueCreate(SERIAL_REQUEST_QUEUE_LENGTH, sizeof(uint8_t));
    equipmentModeQueue = xQueueCreate(1, sizeof(uint8_t));
    if (powerCommandQueue == nullptr || equipmentModeQueue == nullptr) {
        return false;
    }
    uint8_t initialMode = EQUIPMENT_MODE_AUTO;
    xQueueOverwrite(equipmentModeQueue, &initialMode);
    sensorAvailable = ina219.begin();
    if (!sensorAvailable) {
        Serial.println("INA219 initialization failed");
        return false;
    }
    preferencesAvailable = preferences.begin(POWER_NVS_NAMESPACE, false);
    if (preferencesAvailable) {
        idleBaselineMilliWatts = preferences.isKey(POWER_NVS_IDLE_KEY) ? preferences.getInt(POWER_NVS_IDLE_KEY, 0) : 0;
    }
    return true;
}

int32_t readCurrent_mA() {
    if (!sensorAvailable) {
        return 0;
    }
    float value = ina219.getCurrent_mA();
    if (!isfinite(value) || value < 0.0f || value > 1000.0f) {
        ++powerI2cErrors;
        return lastValidCurrentMilliAmps;
    }
    lastValidCurrentMilliAmps = static_cast<int32_t>(value + 0.5f);
    return lastValidCurrentMilliAmps;
}

int32_t readPower_mW() {
    if (!sensorAvailable) {
        return 0;
    }
    if (POWER_FILTER_SAMPLES == 0) return lastValidPowerMilliWatts;
    int32_t samples[POWER_FILTER_SAMPLES];
    for (size_t index = 0; index < POWER_FILTER_SAMPLES; ++index) {
        float value = ina219.getPower_mW();
        if (!isfinite(value) || value < 0.0f || value > 5000.0f) {
            ++powerI2cErrors;
            samples[index] = lastValidPowerMilliWatts;
        } else {
            samples[index] = static_cast<int32_t>(value + 0.5f);
            lastValidPowerMilliWatts = samples[index];
        }
    }
    for (size_t index = 1; index < POWER_FILTER_SAMPLES; ++index) {
        int32_t value = samples[index];
        size_t insertion = index;
        while (insertion > 0 && samples[insertion - 1] > value) {
            samples[insertion] = samples[insertion - 1];
            --insertion;
        }
        samples[insertion] = value;
    }
    return samples[POWER_FILTER_SAMPLES / 2];
}

bool calibrateIdleBaseline() {
    if (!sensorAvailable || IDLE_SAMPLES == 0) {
        return false;
    }
    int64_t totalMilliWatts = 0;
    for (uint32_t sample = 0; sample < IDLE_SAMPLES; ++sample) {
        totalMilliWatts += readPower_mW();
        vTaskDelay(pdMS_TO_TICKS(IDLE_SAMPLE_INTERVAL_MS));
    }
    idleBaselineMilliWatts = static_cast<int32_t>(totalMilliWatts / IDLE_SAMPLES);
    equipmentActive = false;
    if (!preferencesAvailable) {
        preferencesAvailable = preferences.begin(POWER_NVS_NAMESPACE, false);
    }
    if (preferencesAvailable) {
        preferences.putInt(POWER_NVS_IDLE_KEY, idleBaselineMilliWatts);
    }
    Serial.printf("Idle baseline calibrated: %ld mW\n", static_cast<long>(idleBaselineMilliWatts));
    return true;
}

bool isEquipmentActive(int32_t filteredPowerMilliWatts) {
    uint8_t equipmentMode = EQUIPMENT_MODE_AUTO;
    if (equipmentModeQueue != nullptr) {
        xQueuePeek(equipmentModeQueue, &equipmentMode, 0);
    }
    if (equipmentMode == EQUIPMENT_MODE_ON) {
        equipmentActive = true;
        return true;
    }
    if (equipmentMode == EQUIPMENT_MODE_OFF) {
        equipmentActive = false;
        return false;
    }
    if (!sensorAvailable) {
        equipmentActive = false;
        return false;
    }
    int32_t threshold = idleBaselineMilliWatts + (equipmentActive ? ACTIVE_MARGIN_MW_OFF : ACTIVE_MARGIN_MW_ON);
    equipmentActive = equipmentActive ? filteredPowerMilliWatts > threshold : filteredPowerMilliWatts >= threshold;
    return equipmentActive;
}

const char* equipmentModeName() {
    uint8_t equipmentMode = EQUIPMENT_MODE_AUTO;
    if (equipmentModeQueue != nullptr) {
        xQueuePeek(equipmentModeQueue, &equipmentMode, 0);
    }
    if (equipmentMode == EQUIPMENT_MODE_ON) return "on";
    if (equipmentMode == EQUIPMENT_MODE_OFF) return "off";
    return "auto";
}

void requestEquipmentAuto() {
    uint8_t request = POWER_COMMAND_EQUIP_AUTO;
    if (powerCommandQueue != nullptr) xQueueSend(powerCommandQueue, &request, 0);
}

uint32_t i2cErrorCount() { return powerI2cErrors; }

void registerPowerCommands() {
    serial_cmd_module::registerCommand("IDLE", handleIdleCommand);
    serial_cmd_module::registerCommand("EQUIP", handleEquipCommand);
}

void startPowerTask() {
    if (powerCommandQueue == nullptr || equipmentModeQueue == nullptr) return;
    xTaskCreate(powerTask, "power_task", STACK_SIZE_DEFAULT, nullptr, PRIORITY_SENSOR, nullptr);
}

void powerTask(void* parameter) {
    static_cast<void>(parameter);
    uint32_t lastStackReport = millis();
    for (;;) {
        boot_monitor_module::reportTaskStack("power", lastStackReport);
        uint8_t request;
        if (xQueueReceive(powerCommandQueue, &request, pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS)) == pdTRUE) {
            if (request == POWER_COMMAND_IDLE) {
                Serial.println(calibrateIdleBaseline() ? "Idle calibration complete" : "INA219 unavailable");
            } else {
                uint8_t mode = request == POWER_COMMAND_EQUIP_ON ? EQUIPMENT_MODE_ON : request == POWER_COMMAND_EQUIP_OFF ? EQUIPMENT_MODE_OFF : EQUIPMENT_MODE_AUTO;
                xQueueOverwrite(equipmentModeQueue, &mode);
                Serial.printf("EQUIP mode=%s\n", mode == EQUIPMENT_MODE_ON ? "on" : mode == EQUIPMENT_MODE_OFF ? "off" : "auto");
            }
        } else {
#if DEBUG_VERBOSE
            Serial.println("power_task heartbeat");
#endif
        }
    }
}
}
