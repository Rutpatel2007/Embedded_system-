#include "rtos_tasks.h"
#include "network_sync.h"

// FreeRTOS Inter-Task Communication Queue
static QueueHandle_t g_sampleQueue = NULL;

// Active Sensor Provider (Hardware or Mock)
static ISensorProvider* g_sensorProvider = NULL;

// Ring Buffers for History Storage
static RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE> g_gasRingBuffer;
static RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE> g_powerRingBuffer;

// Diagnostics & Telemetry Counters
static uint32_t g_processedSampleCount = 0;
static uint32_t g_droppedSampleCount = 0;
static float g_avgJitterMs = 0.0f;

// Layer 1, Layer 2 & Layer 3 Engine Instances
static Layer1PlausibilityEngine g_layer1Engine;
static Layer1Result g_latestLayer1Result;

static Layer2Engine g_layer2Engine;
static Layer2Result g_latestLayer2Result;

static HashChainEngine g_hashChainEngine;

const RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& getGasRingBuffer() {
    return g_gasRingBuffer;
}

const RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE>& getPowerRingBuffer() {
    return g_powerRingBuffer;
}

const Layer1Result& getLatestLayer1Result() {
    return g_latestLayer1Result;
}

const Layer2Result& getLatestLayer2Result() {
    return g_latestLayer2Result;
}

Layer2Engine& getLayer2Engine() {
    return g_layer2Engine;
}

HashChainEngine& getHashChainEngine() {
    return g_hashChainEngine;
}

uint32_t getProcessedSampleCount() {
    return g_processedSampleCount;
}

uint32_t getDroppedSampleCount() {
    return g_droppedSampleCount;
}

float getAverageJitterMs() {
    return g_avgJitterMs;
}

bool initRTOSTasks(ISensorProvider* provider) {
    if (provider == NULL) return false;
    g_sensorProvider = provider;

    // Initialize Layer 2 Sensor Identity Engine
    g_layer2Engine.begin(true);

    // Initialize Layer 3 Cryptographic Hash Chain Engine
    g_hashChainEngine.begin(nullptr, true);

    // Create bounded FreeRTOS queue
    g_sampleQueue = xQueueCreate(SENSOR_QUEUE_LEN, sizeof(SensorSample));
    if (g_sampleQueue == NULL) {
        Serial.println("[RTOS ERROR] Failed to allocate sample queue!");
        return false;
    }

    // Pin SensorTask to Core 1 for precise hardware acquisition timing
    xTaskCreatePinnedToCore(
        sensorTask,
        "SensorTask",
        STACK_SIZE_SENSOR_TASK,
        NULL,
        PRIORITY_SENSOR_TASK,
        NULL,
        1
    );

    // Pin AlgorithmTask to Core 0 for background processing & history maintenance
    xTaskCreatePinnedToCore(
        algorithmTask,
        "AlgorithmTask",
        STACK_SIZE_ALGO_TASK,
        NULL,
        PRIORITY_ALGO_TASK,
        NULL,
        0
    );

    // Pin HealthTask to Core 0
    xTaskCreatePinnedToCore(
        healthTask,
        "HealthTask",
        STACK_SIZE_HEALTH_TASK,
        NULL,
        PRIORITY_HEALTH_TASK,
        NULL,
        0
    );

    // Initialize Network Sync Task
    initNetworkSyncTask();


    Serial.printf("[RTOS INIT] FreeRTOS queues and tasks initialized. Queue capacity: %d samples.\n", SENSOR_QUEUE_LEN);
    return true;
}

// ----------------------------------------------------
// SensorTask: 500 ms Acquisition Scheduler
// ----------------------------------------------------
void sensorTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(SENSOR_SAMPLE_INTERVAL_MS);

    uint32_t lastSampleMs = millis();

    for (;;) {
        // Wait deterministically for next 500 ms tick
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        uint32_t currentMs = millis();
        uint32_t actualDeltaMs = currentMs - lastSampleMs;
        lastSampleMs = currentMs;

        // Calculate sample timing jitter (difference from expected 500ms)
        float jitter = fabsf((float)actualDeltaMs - (float)SENSOR_SAMPLE_INTERVAL_MS);
        g_avgJitterMs = (g_avgJitterMs * 0.9f) + (jitter * 0.1f); // Exponential moving average

        SensorSample sample;
        memset(&sample, 0, sizeof(SensorSample));
        sample.millisMs = currentMs;

        if (g_sensorProvider != NULL) {
            g_sensorProvider->readSample(sample);
            sample.millisMs = currentMs; // Preserve actual monotonic timestamp
        }

        // Push sample to bounded FreeRTOS queue (non-blocking if queue full)
        if (xQueueSend(g_sampleQueue, &sample, 0) != pdPASS) {
            g_droppedSampleCount++;
            Serial.printf("[QUEUE OVERFLOW] Sample queue full! Dropped sample count: %u\n", g_droppedSampleCount);
        }
    }
}

// ----------------------------------------------------
// AlgorithmTask: Ring Buffer Maintenance, Layer 1 Engine & Development Telemetry
// ----------------------------------------------------
void algorithmTask(void* pvParameters) {
    SensorSample sample;
    static PlausibilityStatus lastStatus = PlausibilityStatus::NORMAL;
    static SensorIdentityStatus lastL2Status = SensorIdentityStatus::UNENROLLED;
    static uint32_t sampleCounter = 0;

    for (;;) {
        // Block until a new SensorSample arrives in the queue
        if (xQueueReceive(g_sampleQueue, &sample, portMAX_DELAY) == pdTRUE) {
            g_processedSampleCount++;
            sampleCounter++;

            // 1. Evaluate Layer 1 Cross-Modal Physical Plausibility Engine
            g_latestLayer1Result = g_layer1Engine.evaluate(sample, g_gasRingBuffer, g_powerRingBuffer);

            // 2. Evaluate Layer 2 Sensor Identity Engine
            g_latestLayer2Result = g_layer2Engine.evaluate(sample, g_gasRingBuffer, g_powerRingBuffer);

            // 3. Evaluate Layer 3 Cryptographic Hash Chain Engine & Append Record
            g_hashChainEngine.appendRecord(sample, g_latestLayer1Result, g_latestLayer2Result);

            // 4. Update 60-second gas history ring buffer
            g_gasRingBuffer.add(sample);

            // 5. Update 90-second power trailing history ring buffer
            PowerSample pSample;
            pSample.timestamp = sample.timestamp;
            pSample.millisMs = sample.millisMs;
            pSample.power_mW = sample.power_mW;
            pSample.equipmentActive = sample.equipmentActive;
            g_powerRingBuffer.add(pSample);

            // 5. Print machine-readable development CSV telemetry row over Serial
            // Format: millisMs,timestamp,gasRaw,gasVoltage,sensorVoltage,power_mW,equipmentActive,gasValid,powerValid
            Serial.printf("[TELEMETRY] %u,%llu,%u,%.3f,%.3f,%.3f,%d,%d,%d\n",
                          sample.millisMs, (unsigned long long)sample.timestamp, sample.gasRaw, sample.gasVoltage, sample.sensorVoltage,
                          sample.power_mW, sample.equipmentActive ? 1 : 0,
                          sample.gasValid ? 1 : 0, sample.powerValid ? 1 : 0);

            // 6. Rate-limited Layer 1 & Layer 2 Diagnostic Output
            if (g_latestLayer1Result.status != lastStatus || g_latestLayer2Result.status != lastL2Status || (sampleCounter % 10 == 0)) {
                lastStatus = g_latestLayer1Result.status;
                lastL2Status = g_latestLayer2Result.status;

                const char* statusStr = "NORMAL";
                if (g_latestLayer1Result.status == PlausibilityStatus::PLAUSIBLE) statusStr = "PLAUSIBLE";
                else if (g_latestLayer1Result.status == PlausibilityStatus::SUSPICIOUS) statusStr = "SUSPICIOUS";

                const char* l2Str = "UNENROLLED";
                if (g_latestLayer2Result.status == SensorIdentityStatus::SENSOR_OK) l2Str = "SENSOR_OK";
                else if (g_latestLayer2Result.status == SensorIdentityStatus::DEGRADED_REVIEW) l2Str = "DEGRADED_REVIEW";
                else if (g_latestLayer2Result.status == SensorIdentityStatus::SENSOR_IDENTITY_MISMATCH) l2Str = "SENSOR_IDENTITY_MISMATCH";
                else if (g_latestLayer2Result.status == SensorIdentityStatus::INSUFFICIENT_FEATURES) l2Str = "INSUFFICIENT_FEATURES";

                Serial.printf("[ALGO DIAG] L1: %s | L2: %s (sim=%.2f, dist=%.2f, features=%d)\n",
                              statusStr, l2Str, g_latestLayer2Result.similarityScore,
                              g_latestLayer2Result.featureDistance, g_latestLayer2Result.validFeatureCount);
            }
        }
    }
}

// ----------------------------------------------------
// HealthTask: System Health & Resource Diagnostics (5000 ms)
// ----------------------------------------------------
void healthTask(void* pvParameters) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        uint32_t freeHeap = ESP.getFreeHeap();
        UBaseType_t queueWaiting = uxQueueMessagesWaiting(g_sampleQueue);

        Serial.println("\n--- [SYSTEM HEALTH] ---");
        Serial.printf("  Free Heap: %u bytes\n", freeHeap);
        Serial.printf("  Sample Queue Usage: %u / %d\n", queueWaiting, SENSOR_QUEUE_LEN);
        Serial.printf("  Total Samples Processed: %u\n", g_processedSampleCount);
        Serial.printf("  Total Samples Dropped: %u\n", g_droppedSampleCount);
        Serial.printf("  Sample Jitter (EMA): %.2f ms\n", g_avgJitterMs);
        Serial.printf("  Gas History Buffer: %u / %u samples (%.1f sec)\n",
                      (unsigned int)g_gasRingBuffer.count(), GAS_RING_BUFFER_SIZE, (g_gasRingBuffer.count() * 0.5f));
        Serial.printf("  Power History Buffer: %u / %u samples (%.1f sec)\n",
                      (unsigned int)g_powerRingBuffer.count(), POWER_RING_BUFFER_SIZE, (g_powerRingBuffer.count() * 0.5f));
        Serial.println("-------------------------\n");
    }
}
