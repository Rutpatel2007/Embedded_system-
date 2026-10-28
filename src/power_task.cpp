#include "power_task.h"
#include "config.h"
#include "serial_cmd.h"
#include "boot_monitor.h"
#include <Adafruit_INA219.h>
#include <Preferences.h>

static Adafruit_INA219 ina219(INA219_I2C_ADDRESS);
static Preferences preferences;
static bool sensorAvailable;
static bool preferencesAvailable;
static int32_t idleBaselineMilliWatts;
static bool equipmentActive;
static QueueHandle_t powerCommandQueue;

static void handleIdleCommand(const char* args) {
    static_cast<void>(args);
    uint8_t request = 1;
    if (powerCommandQueue == nullptr || xQueueSend(powerCommandQueue, &request, 0) != pdPASS) {
        Serial.println(SERIAL_HANDLER_QUEUE_FULL);
    }
}

namespace power_task_module {
bool init() {
    powerCommandQueue = xQueueCreate(SERIAL_REQUEST_QUEUE_LENGTH, sizeof(uint8_t));
    sensorAvailable = ina219.begin();
    if (!sensorAvailable) {
        Serial.println("INA219 initialization failed");
        return false;
    }
    preferencesAvailable = preferences.begin("proov-power", false);
    if (preferencesAvailable) {
        idleBaselineMilliWatts = preferences.getInt("idle_mw", 0);
    }
    return true;
}

int32_t readCurrent_mA() {
    if (!sensorAvailable) {
        return 0;
    }
    return static_cast<int32_t>(ina219.getCurrent_mA() + 0.5f);
}

int32_t readPower_mW() {
    if (!sensorAvailable) {
        return 0;
    }
    return static_cast<int32_t>(ina219.getPower_mW() + 0.5f);
}

bool calibrateIdleBaseline() {
    if (!sensorAvailable) {
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
        preferencesAvailable = preferences.begin("proov-power", false);
    }
    if (preferencesAvailable) {
        preferences.putInt("idle_mw", idleBaselineMilliWatts);
    }
    Serial.printf("Idle baseline calibrated: %ld mW\n", static_cast<long>(idleBaselineMilliWatts));
    return true;
}

bool isEquipmentActive() {
    if (!sensorAvailable) {
        return false;
    }
    int32_t threshold = idleBaselineMilliWatts + (equipmentActive ? ACTIVE_MARGIN_MW_OFF : ACTIVE_MARGIN_MW_ON);
    equipmentActive = equipmentActive ? readPower_mW() > threshold : readPower_mW() >= threshold;
    return equipmentActive;
}

void registerPowerCommands() {
    serial_cmd_module::registerCommand("IDLE", handleIdleCommand);
}

void startPowerTask() {
    xTaskCreate(powerTask, "power_task", STACK_SIZE_DEFAULT, nullptr, PRIORITY_SENSOR, nullptr);
}

void powerTask(void* parameter) {
    static_cast<void>(parameter);
    uint32_t lastStackReport = millis();
    for (;;) {
        boot_monitor_module::reportTaskStack("power", lastStackReport);
        uint8_t request;
        if (xQueueReceive(powerCommandQueue, &request, pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS)) == pdTRUE) {
            Serial.println(calibrateIdleBaseline() ? "Idle calibration complete" : "INA219 unavailable");
        } else {
            Serial.println("power_task heartbeat");
        }
    }
}
}
