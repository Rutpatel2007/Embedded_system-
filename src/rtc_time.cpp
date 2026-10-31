#include "rtc_time.h"
#include "config.h"
#include "serial_cmd.h"
#include "boot_monitor.h"
#include <Wire.h>
#include <RTClib.h>
#include <ctype.h>
#include <stdlib.h>

static RTC_DS3231 rtc;
static bool rtcAvailable;
static bool lostPowerAtBoot;
static QueueHandle_t rtcCommandQueue;

static void handleSetTimeCommand(const char* args) {
    if (args == nullptr || args[0] == '\0') {
        Serial.println("Usage: SETTIME <epoch>");
        return;
    }
    char* end = nullptr;
    unsigned long epoch = strtoul(args, &end, 10);
    while (end != nullptr && *end == ' ') {
        ++end;
    }
    if (end == args || end == nullptr || *end != '\0' || epoch == 0) {
        Serial.println("Usage: SETTIME <epoch>");
        return;
    }
    for (const char* digit = args; digit < end; ++digit) {
        if (!isdigit(static_cast<unsigned char>(*digit))) {
            Serial.println("Usage: SETTIME <epoch>");
            return;
        }
    }
    uint32_t request = static_cast<uint32_t>(epoch);
    if (rtcCommandQueue == nullptr || xQueueSend(rtcCommandQueue, &request, 0) != pdPASS) {
        Serial.println(SERIAL_HANDLER_QUEUE_FULL);
    }
}

namespace rtc_time_module {
bool init() {
    rtcCommandQueue = xQueueCreate(SERIAL_REQUEST_QUEUE_LENGTH, sizeof(uint32_t));
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    bool foundIna219 = false;
    bool foundDs3231 = false;
    Serial.println("I2C scan start");
    for (uint8_t address = 1; address < 127; ++address) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            Serial.printf("I2C device found: 0x%02X\n", address);
            foundIna219 = foundIna219 || address == INA219_I2C_ADDRESS;
            foundDs3231 = foundDs3231 || address == DS3231_I2C_ADDRESS;
        }
    }
    if (!foundIna219) {
        Serial.println("I2C device missing: INA219 0x40");
    }
    if (!foundDs3231) {
        Serial.println("I2C device missing: DS3231 0x68");
    }
    rtcAvailable = rtc.begin();
    if (!rtcAvailable) {
        lostPowerAtBoot = false;
        return false;
    }
    lostPowerAtBoot = rtc.lostPower();
    return true;
}

uint32_t getTimestamp() {
    if (!rtcAvailable) {
        return 0;
    }
    return rtc.now().unixtime();
}

bool rtcLostPower() {
    return lostPowerAtBoot;
}

bool setInitialTime(uint32_t epoch) {
    if (!rtcAvailable) {
        return false;
    }
    rtc.adjust(DateTime(epoch));
    lostPowerAtBoot = false;
    return true;
}

void registerRtcCommands() {
    serial_cmd_module::registerCommand("SETTIME", handleSetTimeCommand);
}

void startRtcTimeTask() {
    if (rtcCommandQueue == nullptr) return;
    xTaskCreate(rtcTimeTask, "rtc_time", STACK_SIZE_DEFAULT, nullptr, PRIORITY_SENSOR, nullptr);
}

void rtcTimeTask(void* parameter) {
    static_cast<void>(parameter);
    uint32_t lastStackReport = millis();
    for (;;) {
        boot_monitor_module::reportTaskStack("rtc_time", lastStackReport);
        uint32_t epoch;
        if (xQueueReceive(rtcCommandQueue, &epoch, pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS)) == pdTRUE) {
            Serial.println(setInitialTime(epoch) ? "RTC time set" : "RTC unavailable");
        } else {
#if DEBUG_VERBOSE
            Serial.println("rtc_time heartbeat");
#endif
        }
    }
}
}
