#include "network_sync.h"
#include "../include/config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <SD.h>
#include <ArduinoJson.h>

#define SYNC_STATE_FILE "/sync.txt"

static void networkSyncTask(void* pvParameters) {
    // Set WiFi to station mode and attempt initial connection
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    uint32_t lastWiFiConnectAttempt = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SYNC_INTERVAL_MS));

        if (WiFi.status() != WL_CONNECTED) {
            uint32_t now = millis();
            // Bounded reconnect strategy: only retry every 30s to avoid aggressive blocking
            if (now - lastWiFiConnectAttempt > 30000) {
                Serial.println("[SYNC] WiFi disconnected. Attempting reconnect...");
                WiFi.disconnect();
                WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
                lastWiFiConnectAttempt = now;
            }
            continue;
        }

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
            logFile.close();
            continue;
        }

        // Prepare JSON Document for Batch
        JsonDocument doc;
        doc["device_id"] = DEVICE_ID;
        JsonArray readings = doc["readings"].to<JsonArray>();

        uint32_t currentOffset = lastOffset;
        int batchCount = 0;
        bool malformedEncountered = false;

        while (logFile.available() && batchCount < BATCH_SIZE_LIMIT) {
            uint32_t lineStartPos = logFile.position();
            String line = logFile.readStringUntil('\n');
            uint32_t nextPos = logFile.position();

            // Skip empty lines (e.g. trailing newlines)
            if (line.length() <= 1) {
                currentOffset = nextPos;
                continue;
            }

            JsonDocument lineDoc;
            DeserializationError err = deserializeJson(lineDoc, line);
            
            if (err) {
                Serial.printf("[SYNC] ERROR: Malformed record at offset %u: %s\n", lineStartPos, err.c_str());
                if (err == DeserializationError::IncompleteInput) {
                    // Partial write. Stop batching, don't advance past this line so it can be completed later.
                    break;
                } else {
                    // Unrecoverable malformed line (corrupt data). 
                    // Handle explicitly by logging and advancing the offset so we don't deadlock the sync process,
                    // but do not silently pretend it was synchronized.
                    Serial.printf("[SYNC] QUARANTINED corrupt line: %s\n", line.c_str());
                    currentOffset = nextPos;
                    malformedEncountered = true;
                    continue;
                }
            }

            readings.add(lineDoc);
            batchCount++;
            currentOffset = nextPos;
        }

        logFile.close();

        if (batchCount > 0) {
            String payload;
            serializeJson(doc, payload);

            HTTPClient http;
            // 1. Explicit bounded timeout (5000 ms)
            http.setTimeout(5000); 
            String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + API_INGEST_ENDPOINT;
            http.begin(url);
            http.addHeader("Content-Type", "application/json");

            int httpResponseCode = http.POST(payload);

            // 6. Treat ONLY exact 200 OK as success to prevent data loss
            if (httpResponseCode == HTTP_CODE_OK) {
                Serial.printf("[SYNC] Successfully synced %d records. HTTP 200\n", batchCount);
                // 3. Advance offset ONLY after confirmed successful ingestion
                File writeSyncFile = SD.open(SYNC_STATE_FILE, FILE_WRITE);
                if (writeSyncFile) {
                    writeSyncFile.print(currentOffset);
                    writeSyncFile.close();
                }
            } else {
                Serial.printf("[SYNC] Sync failed. HTTP %d. Retrying later.\n", httpResponseCode);
                // Ingestion failed, offset remains unchanged
            }
            http.end();
        } else if (malformedEncountered) {
            // We encountered corrupt data but no valid records to send.
            // We must save the advanced offset to move past the corruption.
            File writeSyncFile = SD.open(SYNC_STATE_FILE, FILE_WRITE);
            if (writeSyncFile) {
                writeSyncFile.print(currentOffset);
                writeSyncFile.close();
            }
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
