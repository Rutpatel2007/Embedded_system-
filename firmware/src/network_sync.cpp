#include "network_sync.h"
#include "../include/config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <SD.h>
#include <ArduinoJson.h>

#define SYNC_STATE_FILE "/sync.txt"

static void networkSyncTask(void* pvParameters) {
    // Attempt initial WiFi connection
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SYNC_INTERVAL_MS));

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[SYNC] WiFi disconnected. Attempting reconnect...");
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            continue;
        }

        // Initialize SD if not already done by main? Main does it.
        // Wait for SD card to be accessible
        if (!SD.exists(LOG_CHAIN_FILE_PATH)) {
            continue;
        }

        // Read last synced offset
        uint32_t lastOffset = 0;
        File syncFile = SD.open(SYNC_STATE_FILE, FILE_READ);
        if (syncFile) {
            lastOffset = syncFile.parseInt();
            syncFile.close();
        }

        File logFile = SD.open(LOG_CHAIN_FILE_PATH, FILE_READ);
        if (!logFile) {
            Serial.println("[SYNC] Failed to open chain log.");
            continue;
        }

        if (logFile.size() < lastOffset) {
            // File was truncated or reset
            lastOffset = 0;
        }

        logFile.seek(lastOffset);

        if (logFile.available() == 0) {
            // Nothing new to sync
            logFile.close();
            continue;
        }

        // Prepare JSON Document for Batch
        JsonDocument doc;
        doc["device_id"] = DEVICE_ID;
        JsonArray readings = doc["readings"].to<JsonArray>();

        uint32_t currentOffset = lastOffset;
        int batchCount = 0;

        while (logFile.available() && batchCount < BATCH_SIZE_LIMIT) {
            String line = logFile.readStringUntil('\n');
            currentOffset += line.length() + 1; // +1 for the newline character

            if (line.length() > 10 && line.startsWith("{")) {
                JsonDocument lineDoc;
                DeserializationError err = deserializeJson(lineDoc, line);
                if (!err) {
                    readings.add(lineDoc);
                    batchCount++;
                }
            }
        }

        logFile.close();

        if (batchCount > 0) {
            String payload;
            serializeJson(doc, payload);

            HTTPClient http;
            String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + API_INGEST_ENDPOINT;
            http.begin(url);
            http.addHeader("Content-Type", "application/json");

            int httpResponseCode = http.POST(payload);

            if (httpResponseCode >= 200 && httpResponseCode < 300) {
                Serial.printf("[SYNC] Successfully synced %d records. HTTP %d\n", batchCount, httpResponseCode);
                // Save new offset
                File writeSyncFile = SD.open(SYNC_STATE_FILE, FILE_WRITE);
                if (writeSyncFile) {
                    writeSyncFile.print(currentOffset);
                    writeSyncFile.close();
                }
            } else {
                Serial.printf("[SYNC] Sync failed. HTTP %d. Retrying later.\n", httpResponseCode);
            }
            http.end();
        }
    }
}

bool initNetworkSyncTask() {
    BaseType_t res = xTaskCreatePinnedToCore(
        networkSyncTask,
        "NetworkSyncTask",
        8192,
        NULL,
        1, // Priority (lower than sensor task)
        NULL,
        0  // Core 0 (background operations)
    );
    return (res == pdPASS);
}
