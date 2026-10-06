#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

class SDStorage {
public:
    SDStorage(uint8_t csPin = 5);

    // Initialize SPI bus and SD card module
    bool begin();

    // Return card type string (SDSC, SDHC, etc.)
    String getCardType();

    // Return total card size in Megabytes (MB)
    uint64_t getCardSizeMB();

    // Write text to a file (overwrites existing content)
    bool writeFile(const char* path, const char* message);

    // Append text to a file
    bool appendFile(const char* path, const char* message);

    // Read and return file contents as a String
    String readFile(const char* path);

    // Run diagnostic test: write, read, and verify persistence
    bool runDiagnostic();

private:
    uint8_t _csPin;
    bool _initialized;
};

#endif // SD_STORAGE_H
