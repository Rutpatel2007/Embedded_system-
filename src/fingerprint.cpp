#include "fingerprint.h"
#include "config.h"
#include "sensor_task.h"
#include "serial_cmd.h"
#include "boot_monitor.h"
#include <Preferences.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>

enum FingerprintCommand : uint8_t {
    FINGERPRINT_COMMAND_INFO,
    FINGERPRINT_COMMAND_ENROLL,
    FINGERPRINT_COMMAND_REENROLL,
    FINGERPRINT_COMMAND_CLEAR,
    FINGERPRINT_COMMAND_TOLERANCE
};

struct FingerprintRequest {
    FingerprintCommand command;
    uint32_t value;
};

struct EnrolledFingerprint {
    bool enrolled;
    uint32_t shape[FP_POINTS];
    uint32_t levelMilliVolts;
    bool pendingEnrollment;
};

static QueueHandle_t fingerprintRequestQueue;
static QueueHandle_t fingerprintStatusQueue;
static uint8_t currentFingerprintStatus = FP_STATUS_PENDING;
static uint32_t fingerprintDropCount;
static uint32_t fingerprintTolerance = FP_TOLERANCE;
static uint64_t currentDistance;
static bool currentDistanceAvailable;
static portMUX_TYPE fingerprintDropMux = portMUX_INITIALIZER_UNLOCKED;

static void enqueueFingerprintCommand(FingerprintCommand command) {
    FingerprintRequest request{command, 0};
    if (fingerprintRequestQueue == nullptr || xQueueSend(fingerprintRequestQueue, &request, 0) != pdPASS) {
        Serial.println(SERIAL_HANDLER_QUEUE_FULL);
    }
}

static void handleEnrollCommand(const char* args) {
    if (args[0] != '\0') {
        Serial.println("ENROLL takes no arguments");
        return;
    }
    enqueueFingerprintCommand(FINGERPRINT_COMMAND_ENROLL);
}

static void handleReenrollCommand(const char* args) {
    if (args[0] != '\0') {
        Serial.println("REENROLL takes no arguments");
        return;
    }
    enqueueFingerprintCommand(FINGERPRINT_COMMAND_REENROLL);
}

static void handleFingerprintCommand(const char* args) {
    if (args[0] == '\0') {
        enqueueFingerprintCommand(FINGERPRINT_COMMAND_INFO);
    } else if (strcasecmp(args, "CLEAR") == 0) {
        enqueueFingerprintCommand(FINGERPRINT_COMMAND_CLEAR);
    } else if (strncasecmp(args, "TOL ", 4) == 0) {
        char* end = nullptr;
        if (args[4] == '-' || args[4] == '\0') {
            Serial.println("Usage: FP TOL <n>");
            return;
        }
        unsigned long value = strtoul(args + 4, &end, 10);
        if (end == args + 4 || *end != '\0' || value > UINT32_MAX) {
            Serial.println("Usage: FP TOL <n>");
            return;
        }
        FingerprintRequest request{FINGERPRINT_COMMAND_TOLERANCE, static_cast<uint32_t>(value)};
        if (fingerprintRequestQueue == nullptr || xQueueSend(fingerprintRequestQueue, &request, 0) != pdPASS) Serial.println(SERIAL_HANDLER_QUEUE_FULL);
    } else {
        Serial.println("Usage: FP [CLEAR|TOL <n>]");
    }
}

static bool loadEnrolledFingerprint(EnrolledFingerprint& enrolled) {
    Preferences preferences;
    if (!preferences.begin(FP_NVS_NAMESPACE, false)) {
        return false;
    }
    enrolled.enrolled = preferences.isKey(FP_NVS_ENROLLED_KEY) && preferences.getBool(FP_NVS_ENROLLED_KEY, false);
    enrolled.levelMilliVolts = preferences.isKey(FP_NVS_LEVEL_KEY) ? preferences.getUInt(FP_NVS_LEVEL_KEY, 0) : 0;
    enrolled.pendingEnrollment = preferences.isKey(FP_NVS_PENDING_KEY) && preferences.getBool(FP_NVS_PENDING_KEY, false);
    size_t shapeBytes = preferences.isKey(FP_NVS_SHAPE_KEY) ? preferences.getBytes(FP_NVS_SHAPE_KEY, enrolled.shape, sizeof(enrolled.shape)) : 0;
    fingerprintTolerance = preferences.isKey(FP_NVS_TOLERANCE_KEY) ? preferences.getUInt(FP_NVS_TOLERANCE_KEY, FP_TOLERANCE) : FP_TOLERANCE;
    preferences.end();
    return !enrolled.enrolled || shapeBytes == sizeof(enrolled.shape);
}

static bool storeEnrolledFingerprint(const FingerprintSignature& signature) {
    Preferences preferences;
    if (!preferences.begin(FP_NVS_NAMESPACE, false)) {
        return false;
    }
    bool stored = preferences.putBytes(FP_NVS_SHAPE_KEY, signature.shape, sizeof(signature.shape)) == sizeof(signature.shape);
    stored = preferences.putUInt(FP_NVS_LEVEL_KEY, signature.levelMilliVolts) > 0 && stored;
    stored = preferences.putBool(FP_NVS_ENROLLED_KEY, true) && stored;
    stored = preferences.putBool(FP_NVS_PENDING_KEY, false) && stored;
    preferences.end();
    return stored;
}

static uint64_t calculateDistance(const uint32_t* measured, const uint32_t* enrolled) {
    uint64_t sumSquaredDifferences = 0;
    if (FP_POINTS == 0) return 0;
    for (size_t index = 0; index < FP_POINTS; ++index) {
        int64_t difference = static_cast<int64_t>(measured[index]) - enrolled[index];
        sumSquaredDifferences += static_cast<uint64_t>(difference * difference);
    }
    return sumSquaredDifferences / FP_POINTS;
}

static const char* statusName(uint8_t status) {
    if (status == FP_STATUS_OK) {
        return "OK";
    }
    if (status == FP_STATUS_MISMATCH) {
        return "MISMATCH";
    }
    if (status == FP_STATUS_NOT_ENROLLED) {
        return "NOT_ENROLLED";
    }
    return "PENDING";
}

static void publishStatus(uint8_t status) {
    currentFingerprintStatus = status;
    if (fingerprintStatusQueue != nullptr) {
        if (uxQueueMessagesWaiting(fingerprintStatusQueue) > 0) {
            portENTER_CRITICAL(&fingerprintDropMux);
            ++fingerprintDropCount;
            portEXIT_CRITICAL(&fingerprintDropMux);
        }
        xQueueOverwrite(fingerprintStatusQueue, &status);
    }
}

static bool setPendingEnrollment() {
    Preferences preferences;
    if (!preferences.begin(FP_NVS_NAMESPACE, false)) {
        return false;
    }
    bool stored = preferences.putBool(FP_NVS_PENDING_KEY, true);
    preferences.end();
    return stored;
}

static void printFingerprintInfo(const EnrolledFingerprint& enrolled, const FingerprintSignature& measured, uint64_t distance, bool hasDistance) {
    Serial.printf("FP STATUS: %s\n", statusName(currentFingerprintStatus));
    Serial.print("FP ENROLLED SHAPE:");
    if (enrolled.enrolled) {
        for (size_t index = 0; index < FP_POINTS; ++index) {
            Serial.printf(" %lu", static_cast<unsigned long>(enrolled.shape[index]));
        }
    } else {
        Serial.print(" none");
    }
    Serial.print("\nFP MEASURED SHAPE:");
    for (size_t index = 0; index < FP_POINTS; ++index) {
        Serial.printf(" %lu", static_cast<unsigned long>(measured.shape[index]));
    }
    if (hasDistance) {
        Serial.printf("\nFP DISTANCE: %llu tolerance=%lu\n", static_cast<unsigned long long>(distance), static_cast<unsigned long>(fingerprintTolerance));
    } else {
        Serial.printf("\nFP DISTANCE: n/a tolerance=%lu\n", static_cast<unsigned long>(fingerprintTolerance));
    }
    uint32_t levelDifference = enrolled.levelMilliVolts > measured.levelMilliVolts ? enrolled.levelMilliVolts - measured.levelMilliVolts : measured.levelMilliVolts - enrolled.levelMilliVolts;
    Serial.printf("FP LEVEL: enrolled_mV=%lu measured_mV=%lu difference_mV=%lu\n", static_cast<unsigned long>(enrolled.levelMilliVolts), static_cast<unsigned long>(measured.levelMilliVolts), static_cast<unsigned long>(levelDifference));
}

namespace fingerprint_module {
void init() {
    currentFingerprintStatus = FP_STATUS_PENDING;
    fingerprintRequestQueue = xQueueCreate(FP_REQUEST_QUEUE_LENGTH, sizeof(FingerprintRequest));
    fingerprintStatusQueue = xQueueCreate(FP_STATUS_QUEUE_LENGTH, sizeof(uint8_t));
    if (fingerprintStatusQueue != nullptr) {
        xQueueOverwrite(fingerprintStatusQueue, &currentFingerprintStatus);
    }
}

bool measureSignature(FingerprintSignature& signature) {
    if (FP_POINTS == 0) return false;
    memset(&signature, 0, sizeof(signature));
    uint32_t samples[FP_POINTS]{};
    TickType_t lastWake = xTaskGetTickCount();
    uint32_t lastStackReport = millis();
    const TickType_t interval = pdMS_TO_TICKS(FP_SAMPLE_INTERVAL_MS);
    for (size_t index = 0; index < FP_POINTS; ++index) {
        boot_monitor_module::reportTaskStack("fingerprint", lastStackReport);
        if (index > 0) {
            vTaskDelayUntil(&lastWake, interval);
        }
        samples[index] = sensor_task_module::readRawADC();
    }
    vTaskDelayUntil(&lastWake, interval);
    boot_monitor_module::reportTaskStack("fingerprint", lastStackReport);
    signature.levelMilliVolts = samples[FP_POINTS - 1];
    if (signature.levelMilliVolts == 0) {
        return false;
    }
    for (size_t index = 0; index < FP_POINTS; ++index) {
        signature.shape[index] = static_cast<uint32_t>((static_cast<uint64_t>(samples[index]) * FP_LEVEL_SCALE) / signature.levelMilliVolts);
    }
    return true;
}

uint8_t getStatus() {
    return currentFingerprintStatus;
}

uint64_t getDistance(bool& available) {
    available = currentDistanceAvailable;
    return currentDistance;
}

uint32_t getDropCount() {
    portENTER_CRITICAL(&fingerprintDropMux);
    uint32_t drops = fingerprintDropCount;
    portEXIT_CRITICAL(&fingerprintDropMux);
    return drops;
}

#ifdef TEST_MODE
bool isEnrolled() {
    Preferences preferences;
    if (!preferences.begin(FP_NVS_NAMESPACE, false)) return false;
    bool enrolled = preferences.isKey(FP_NVS_ENROLLED_KEY) && preferences.getBool(FP_NVS_ENROLLED_KEY, false);
    preferences.end();
    return enrolled;
}
#endif

QueueHandle_t getStatusQueue() {
    return fingerprintStatusQueue;
}

void registerFingerprintCommands() {
    serial_cmd_module::registerCommand("ENROLL", handleEnrollCommand);
    serial_cmd_module::registerCommand("REENROLL", handleReenrollCommand);
    serial_cmd_module::registerCommand("FP", handleFingerprintCommand);
}

void startFingerprintTask() {
    if (fingerprintRequestQueue == nullptr || fingerprintStatusQueue == nullptr) return;
    xTaskCreate(fingerprintTask, "fingerprint", STACK_SIZE_FINGERPRINT, nullptr, PRIORITY_FINGERPRINT, nullptr);
}

void fingerprintTask(void* parameter) {
    static_cast<void>(parameter);
    EnrolledFingerprint enrolled{};
    FingerprintSignature measured{};
    loadEnrolledFingerprint(enrolled);
    uint32_t lastStackReport = millis();
    bool measuredSuccessfully = measureSignature(measured);
    uint64_t distance = 0;
    bool hasDistance = false;
    if (enrolled.pendingEnrollment) {
        bool stored = measuredSuccessfully && storeEnrolledFingerprint(measured);
        if (stored) {
            enrolled.enrolled = true;
            enrolled.pendingEnrollment = false;
            memcpy(enrolled.shape, measured.shape, sizeof(enrolled.shape));
            enrolled.levelMilliVolts = measured.levelMilliVolts;
            publishStatus(FP_STATUS_OK);
        } else {
            publishStatus(enrolled.enrolled ? FP_STATUS_MISMATCH : FP_STATUS_NOT_ENROLLED);
        }
    } else if (!enrolled.enrolled) {
        publishStatus(FP_STATUS_NOT_ENROLLED);
    } else if (measuredSuccessfully) {
        distance = calculateDistance(measured.shape, enrolled.shape);
        hasDistance = true;
        publishStatus(distance <= fingerprintTolerance ? FP_STATUS_OK : FP_STATUS_MISMATCH);
    } else {
        publishStatus(FP_STATUS_MISMATCH);
    }
    currentDistance = distance;
    currentDistanceAvailable = hasDistance;
    Serial.printf("FP STATUS: %s dist=%llu\n", statusName(getStatus()), static_cast<unsigned long long>(distance));
    for (;;) {
        boot_monitor_module::reportTaskStack("fingerprint", lastStackReport);
        FingerprintRequest request;
        if (xQueueReceive(fingerprintRequestQueue, &request, pdMS_TO_TICKS(500)) != pdTRUE) {
            continue;
        }
        if (request.command == FINGERPRINT_COMMAND_INFO) {
            printFingerprintInfo(enrolled, measured, distance, hasDistance);
        } else if (request.command == FINGERPRINT_COMMAND_CLEAR) {
            Preferences preferences;
            bool cleared = preferences.begin(FP_NVS_NAMESPACE, false);
            if (cleared) {
                preferences.remove(FP_NVS_ENROLLED_KEY);
                preferences.remove(FP_NVS_SHAPE_KEY);
                preferences.remove(FP_NVS_LEVEL_KEY);
                preferences.remove(FP_NVS_PENDING_KEY);
                preferences.end();
                enrolled = {};
                distance = 0;
                hasDistance = false;
                currentDistance = 0;
                currentDistanceAvailable = false;
                publishStatus(FP_STATUS_NOT_ENROLLED);
            }
            Serial.println(cleared ? "FP enrollment cleared" : "FP clear failed");
        } else if (request.command == FINGERPRINT_COMMAND_TOLERANCE) {
            Preferences preferences;
            bool stored = preferences.begin(FP_NVS_NAMESPACE, false);
            if (stored) {
                stored = preferences.putUInt(FP_NVS_TOLERANCE_KEY, request.value) > 0;
                preferences.end();
            }
            if (stored) fingerprintTolerance = request.value;
            Serial.printf("FP tolerance=%lu stored=%u\n", static_cast<unsigned long>(fingerprintTolerance), static_cast<unsigned int>(stored));
            if (enrolled.enrolled && measuredSuccessfully) publishStatus(distance <= fingerprintTolerance ? FP_STATUS_OK : FP_STATUS_MISMATCH);
        } else if (request.command == FINGERPRINT_COMMAND_ENROLL) {
            if (enrolled.enrolled) {
                Serial.println("Already enrolled; use REENROLL for sensor replacement");
            } else if (setPendingEnrollment()) {
                Serial.println("Fingerprint enrollment queued; restarting");
                vTaskDelay(pdMS_TO_TICKS(FP_RESTART_DELAY_MS));
                ESP.restart();
            } else {
                Serial.println("Fingerprint enrollment request failed");
            }
        } else if (request.command == FINGERPRINT_COMMAND_REENROLL) {
            if (setPendingEnrollment()) {
                Serial.println("Fingerprint replacement queued; restarting");
                vTaskDelay(pdMS_TO_TICKS(FP_RESTART_DELAY_MS));
                ESP.restart();
            } else {
                Serial.println("Fingerprint enrollment request failed");
            }
        }
    }
}
}
