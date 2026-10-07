#include "rtc.h"
#include <Wire.h>

RTCDriver::RTCDriver(uint8_t address)
    : _address(address), _initialized(false) {}

bool RTCDriver::begin() {
    _initialized = _rtc.begin();
    return _initialized;
}

bool RTCDriver::isRunning() {
    if (!_initialized) return false;
    return !_rtc.lostPower();
}

uint32_t RTCDriver::getUnixTimestamp() {
    if (!_initialized) return 0;
    DateTime now = _rtc.now();
    return now.unixtime();
}

bool RTCDriver::setUnixTimestamp(uint32_t epoch) {
    if (!_initialized) return false;
    _rtc.adjust(DateTime(epoch));
    return true;
}

String RTCDriver::getFormattedDateTime() {
    if (!_initialized) return "RTC_UNINITIALIZED";
    DateTime now = _rtc.now();
    char buf[25];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d UTC",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return String(buf);
}
