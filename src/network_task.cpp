#include "config.h"

#if WIFI_ENABLED && __has_include("secrets.h")
#include "secrets.h"
#elif WIFI_ENABLED
#error "Missing src/secrets.h. Copy src/secrets.h.example to src/secrets.h and set the local values."
#endif

#if WIFI_ENABLED && !defined(WIFI_SSID)
#error "src/secrets.h must define WIFI_SSID."
#endif
#if WIFI_ENABLED && !defined(WIFI_PASSWORD)
#error "src/secrets.h must define WIFI_PASSWORD."
#endif
#if WIFI_ENABLED && !defined(BACKEND_URL)
#error "src/secrets.h must define BACKEND_URL."
#endif
#if WIFI_ENABLED && !defined(API_KEY)
#error "src/secrets.h must define API_KEY."
#endif
#if !WIFI_ENABLED
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define BACKEND_URL ""
#define API_KEY ""
#endif

#include "network_task.h"
#include "hashchain.h"
#include "rtc_time.h"
#include "serial_cmd.h"
#include "boot_monitor.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <strings.h>
#include <string.h>

enum NetworkCommandType : uint8_t {
    NETWORK_COMMAND_STATUS,
    NETWORK_COMMAND_NOW,
    NETWORK_COMMAND_RESET
};

struct NetworkCommand {
    NetworkCommandType type;
};

static QueueHandle_t networkCommandQueue;

static void enqueueNetworkCommand(NetworkCommandType type) {
    NetworkCommand command{type};
    if (networkCommandQueue == nullptr || xQueueSend(networkCommandQueue, &command, 0) != pdPASS) {
        Serial.println(SERIAL_HANDLER_QUEUE_FULL);
    }
}

static void handleSyncCommand(const char* args) {
#if !WIFI_ENABLED
    static_cast<void>(args);
    Serial.println("wifi disabled");
#else
    if (args[0] == '\0') {
        enqueueNetworkCommand(NETWORK_COMMAND_STATUS);
    } else if (strcasecmp(args, "NOW") == 0) {
        enqueueNetworkCommand(NETWORK_COMMAND_NOW);
    } else if (strcasecmp(args, "RESET") == 0) {
        enqueueNetworkCommand(NETWORK_COMMAND_RESET);
    } else {
        Serial.println("Usage: SYNC [NOW|RESET]");
    }
#endif
}

static bool loadCursor(SyncCursor& cursor) {
    Preferences preferences;
    if (!preferences.begin(SYNC_NVS_NAMESPACE, false)) {
        return false;
    }
    bool present = preferences.isKey(SYNC_NVS_CURSOR_KEY);
    size_t cursorBytes = present ? preferences.getBytes(SYNC_NVS_CURSOR_KEY, &cursor, sizeof(cursor)) : 0;
    preferences.end();
    return present && cursorBytes == sizeof(cursor) && cursor.lineIndex > 0;
}

static bool storeCursor(const SyncCursor& cursor) {
    Preferences preferences;
    if (!preferences.begin(SYNC_NVS_NAMESPACE, false)) {
        return false;
    }
    bool stored = preferences.putBytes(SYNC_NVS_CURSOR_KEY, &cursor, sizeof(cursor)) == sizeof(cursor);
    preferences.end();
    return stored;
}

static uint32_t loadLastSuccessTimestamp() {
    Preferences preferences;
    if (!preferences.begin(SYNC_NVS_NAMESPACE, false)) {
        return 0;
    }
    uint32_t timestamp = preferences.isKey(SYNC_NVS_LAST_SUCCESS_KEY) ? preferences.getUInt(SYNC_NVS_LAST_SUCCESS_KEY, 0) : 0;
    preferences.end();
    return timestamp;
}

static void storeLastSuccessTimestamp(uint32_t timestamp) {
    Preferences preferences;
    if (preferences.begin(SYNC_NVS_NAMESPACE, false)) {
        preferences.putUInt(SYNC_NVS_LAST_SUCCESS_KEY, timestamp);
        preferences.end();
    }
}

static void initializeCursor(NetworkRuntime& runtime) {
    if (loadCursor(runtime.cursor)) {
        return;
    }
    if (!storage_module::getOldestLogCursor(runtime.cursor)) {
        runtime.cursor = {0, SYNC_CURSOR_FIRST_LINE};
    }
    storeCursor(runtime.cursor);
}

static String endpointUrl() {
    String url = BACKEND_URL;
    while (url.endsWith("/")) {
        url.remove(url.length() - 1);
    }
    url += BACKEND_INGEST_PATH;
    return url;
}

static String buildPayload(const ChainRecord* records, size_t count) {
    JsonDocument document;
    JsonArray array = document.to<JsonArray>();
    for (size_t index = 0; index < count; ++index) {
        JsonObject item = array.add<JsonObject>();
        item["device_id"] = records[index].deviceId;
        item["timestamp"] = records[index].timestamp;
        item["gas_ppm_x100"] = records[index].gasPpmX100;
        item["power_mw"] = records[index].powerMw;
        item["plausibility_flag"] = records[index].plausibilityFlag;
        item["fingerprint_status"] = records[index].fingerprintStatus;
        item["previous_hash"] = records[index].previousHash;
        item["hash"] = records[index].hash;
    }
    String payload;
    serializeJson(document, payload);
    return payload;
}

static void printSyncStatus(const NetworkRuntime& runtime) {
    const char* wifiState = WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED";
    uint32_t pending = storage_module::countUnsyncedRecords(runtime.cursor);
    Serial.printf("SYNC wifi=%s cursor=%08lu:%lu pending=%lu last_http=%d last_success=%lu\n", wifiState, static_cast<unsigned long>(runtime.cursor.fileDate), static_cast<unsigned long>(runtime.cursor.lineIndex), static_cast<unsigned long>(pending), runtime.lastHttpCode, static_cast<unsigned long>(runtime.lastSuccessTimestamp));
}

static void processNetworkCommand(NetworkRuntime& runtime, NetworkCommandType command) {
    if (command == NETWORK_COMMAND_STATUS) {
        printSyncStatus(runtime);
    } else if (command == NETWORK_COMMAND_NOW) {
        runtime.immediateSync = true;
        Serial.println("SYNC NOW queued");
    } else if (command == NETWORK_COMMAND_RESET) {
        SyncCursor oldest{0, SYNC_CURSOR_FIRST_LINE};
        storage_module::getOldestLogCursor(oldest);
        if (storeCursor(oldest)) {
            runtime.cursor = oldest;
            runtime.immediateSync = true;
            Serial.printf("SYNC cursor reset to %08lu:%lu\n", static_cast<unsigned long>(oldest.fileDate), static_cast<unsigned long>(oldest.lineIndex));
        } else {
            Serial.println("SYNC cursor reset failed");
        }
    }
}

namespace network_task_module {
void init() {
#if WIFI_ENABLED
    networkCommandQueue = xQueueCreate(NETWORK_COMMAND_QUEUE_LENGTH, sizeof(NetworkCommand));
#endif
}

bool connectWiFi(NetworkRuntime& runtime) {
#if !WIFI_ENABLED
    static_cast<void>(runtime);
    return false;
#else
    uint32_t now = millis();
    if (WiFi.status() == WL_CONNECTED) {
        if (runtime.connecting) {
            Serial.println("WiFi connected");
        }
        runtime.connecting = false;
        runtime.retryDelayMs = WIFI_RETRY_MIN_MS;
        runtime.retryAtMs = 0;
        return true;
    }
    if (runtime.connecting) {
        if (now - runtime.connectionStartedAtMs < HTTP_TIMEOUT_MS) {
            return false;
        }
        WiFi.disconnect(false, false);
        runtime.connecting = false;
        runtime.retryAtMs = now + runtime.retryDelayMs;
        runtime.retryDelayMs = WIFI_RETRY_MAX_MS == 0 || runtime.retryDelayMs >= WIFI_RETRY_MAX_MS / 2 ? WIFI_RETRY_MAX_MS : runtime.retryDelayMs * 2;
        Serial.println("WiFi connection attempt failed");
        return false;
    }
    if (runtime.retryAtMs != 0 && static_cast<int32_t>(now - runtime.retryAtMs) < 0) {
        return false;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    runtime.connecting = true;
    runtime.connectionStartedAtMs = now;
    Serial.println("WiFi connection attempt started");
    return false;
#endif
}

bool wifiConnected() {
#if WIFI_ENABLED
    return WiFi.status() == WL_CONNECTED;
#else
    return false;
#endif
}

bool syncPendingRecords(NetworkRuntime& runtime) {
    ChainRecord records[SYNC_BATCH_SIZE];
    size_t count = 0;
    SyncCursor nextCursor = runtime.cursor;
    if (!storage_module::readUnsyncedBatch(runtime.cursor, records, SYNC_BATCH_SIZE, count, nextCursor)) {
        Serial.println("SYNC storage read failed");
        return false;
    }
    if (count == 0) {
        return false;
    }
    String payload = buildPayload(records, count);
    String url = endpointUrl();
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    bool started;
    if (url.startsWith("https://")) {
        WiFiClientSecure client;
        if (BACKEND_INSECURE_TLS) {
            client.setInsecure();
        }
        started = http.begin(client, url);
        if (!started) {
            runtime.lastHttpCode = HTTPC_ERROR_CONNECTION_REFUSED;
            return false;
        }
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-API-Key", API_KEY);
        runtime.lastHttpCode = http.POST(payload);
        http.end();
    } else {
        WiFiClient client;
        started = http.begin(client, url);
        if (!started) {
            runtime.lastHttpCode = HTTPC_ERROR_CONNECTION_REFUSED;
            return false;
        }
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-API-Key", API_KEY);
        runtime.lastHttpCode = http.POST(payload);
        http.end();
    }
    Serial.printf("SYNC batch=%u http=%d\n", static_cast<unsigned int>(count), runtime.lastHttpCode);
    if (runtime.lastHttpCode != 200) {
        return false;
    }
    if (!storeCursor(nextCursor)) {
        Serial.println("SYNC cursor save failed");
        return false;
    }
    runtime.cursor = nextCursor;
    runtime.lastSuccessTimestamp = rtc_time_module::getTimestamp();
    storeLastSuccessTimestamp(runtime.lastSuccessTimestamp);
    return true;
}

void registerNetworkCommands() {
    serial_cmd_module::registerCommand("SYNC", handleSyncCommand);
}

void startNetworkTask() {
#if WIFI_ENABLED
    if (networkCommandQueue != nullptr) xTaskCreate(networkTask, "network_task", STACK_SIZE_NETWORK, nullptr, PRIORITY_NETWORK, nullptr);
#endif
}

void networkTask(void* parameter) {
#if WIFI_ENABLED
    static_cast<void>(parameter);
    NetworkRuntime runtime{};
    runtime.retryDelayMs = WIFI_RETRY_MIN_MS;
    runtime.lastHttpCode = 0;
    initializeCursor(runtime);
    runtime.lastSuccessTimestamp = loadLastSuccessTimestamp();
    runtime.lastSyncAtMs = millis();
    uint32_t lastStackReport = millis();
    for (;;) {
        boot_monitor_module::reportTaskStack("network", lastStackReport);
        NetworkCommand command;
        if (networkCommandQueue != nullptr && xQueueReceive(networkCommandQueue, &command, 0) == pdTRUE) {
            processNetworkCommand(runtime, command.type);
        }
        bool connected = connectWiFi(runtime);
        uint32_t now = millis();
        bool intervalReached = now - runtime.lastSyncAtMs >= SYNC_INTERVAL_MS;
        if (connected && (runtime.immediateSync || intervalReached)) {
            runtime.immediateSync = false;
            runtime.lastSyncAtMs = now;
            syncPendingRecords(runtime);
        }
        vTaskDelay(pdMS_TO_TICKS(NETWORK_POLL_MS));
    }
#else
    static_cast<void>(parameter);
    vTaskDelete(nullptr);
#endif
}
}
