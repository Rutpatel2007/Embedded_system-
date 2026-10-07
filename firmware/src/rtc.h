#ifndef RTC_DRIVER_H
#define RTC_DRIVER_H

#include <Arduino.h>
#include <RTClib.h>

class RTCDriver {
public:
    RTCDriver(uint8_t address = 0x68);

    // Initialize DS3231 I2C interface
    bool begin();

    // Check if RTC is running and oscillator has not stopped
    bool isRunning();

    // Fetch current time as 32-bit Unix epoch timestamp (seconds since 1970)
    uint64_t getUnixTimestamp();

    // Set RTC time using a 32-bit Unix epoch timestamp
    bool setUnixTimestamp(uint64_t epoch);

    // Format and print date-time string over Serial (YYYY-MM-DD HH:MM:SS UTC)
    String getFormattedDateTime();

private:
    uint8_t _address;
    RTC_DS3231 _rtc;
    bool _initialized;
};

#endif // RTC_DRIVER_H
