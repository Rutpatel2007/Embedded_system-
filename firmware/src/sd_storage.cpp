#include "sd_storage.h"
#include "../include/config.h"

SDStorage::SDStorage(uint8_t csPin) : _csPin(csPin), _initialized(false) {}

bool SDStorage::begin() {
    // Configure dedicated SPI pins for SD card module
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    _initialized = SD.begin(_csPin, SPI);
    return _initialized;
}

String SDStorage::getCardType() {
    if (!_initialized) return "NO_CARD";
    uint8_t type = SD.cardType();
    switch (type) {
        case CARD_MMC:  return "MMC";
        case CARD_SD:   return "SDSC";
        case CARD_SDHC: return "SDHC";
        default:        return "UNKNOWN";
    }
}

uint64_t SDStorage::getCardSizeMB() {
    if (!_initialized) return 0;
    return SD.cardSize() / (1024 * 1024);
}

bool SDStorage::writeFile(const char* path, const char* message) {
    if (!_initialized) return false;
    File file = SD.open(path, FILE_WRITE);
    if (!file) return false;
    bool written = file.print(message);
    file.close();
    return written;
}

bool SDStorage::appendFile(const char* path, const char* message) {
    if (!_initialized) return false;
    File file = SD.open(path, FILE_APPEND);
    if (!file) return false;
    bool written = file.print(message);
    file.close();
    return written;
}

String SDStorage::readFile(const char* path) {
    if (!_initialized) return "";
    File file = SD.open(path, FILE_READ);
    if (!file) return "";
    String content = "";
    while (file.available()) {
        content += (char)file.read();
    }
    file.close();
    return content;
}

bool SDStorage::runDiagnostic() {
    const char* diagPath = "/diagnostic.txt";
    const char* testData = "TrueSense SD Persistence Test OK\n";
    
    if (!writeFile(diagPath, testData)) return false;
    String readBack = readFile(diagPath);
    return (readBack == testData);
}
