#include "rtos_tasks.h"

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

// Layer 1 Plausibility Engine Instance
static Layer1PlausibilityEngine g_layer1Engine;
static Layer1Result g_latestLayer1Result;

const RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& getGasRingBuffer() {
    return g_gasRingBuffer;
}

const RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE>& getPowerRingBuffer() {
    return g_powerRingBuffer;
}

const Layer1Result& getLatestLayer1Result() {
    return g_latestLayer1Result;
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
    static uint32_t sampleCounter = 0;

    for (;;) {
        // Block until a new SensorSample arrives in the queue
        if (xQueueReceive(g_sampleQueue, &sample, portMAX_DELAY) == pdTRUE) {
            g_processedSampleCount++;
            sampleCounter++;

            // 1. Evaluate Layer 1 Cross-Modal Physical Plausibility Engine
            g_latestLayer1Result = g_layer1Engine.evaluate(sample, g_gasRingBuffer, g_powerRingBuffer);

            // 2. Update 60-second gas history ring buffer
            g_gasRingBuffer.add(sample);

            // 3. Update 90-second power trailing history ring buffer
            PowerSample pSample;
            pSample.timestamp = sample.timestamp;
            pSample.millisMs = sample.millisMs;
            pSample.power_mW = sample.power_mW;
            pSample.equipmentActive = sample.equipmentActive;
            g_powerRingBuffer.add(pSample);

            // 4. Print machine-readable development CSV telemetry row over Serial
            // Format: millisMs,timestamp,gasRaw,gasVoltage,sensorVoltage,power_mW,equipmentActive,gasValid,powerValid
            Serial.printf("[TELEMETRY] %u,%u,%u,%.3f,%.3f,%.3f,%d,%d,%d\n",
                          sample.millisMs, sample.timestamp, sample.gasRaw, sample.gasVoltage, sample.sensorVoltage,
                          sample.power_mW, sample.equipmentActive ? 1 : 0,
                          sample.gasValid ? 1 : 0, sample.powerValid ? 1 : 0);

            // 5. Rate-limited Layer 1 Diagnostic Output (Prints on status change OR every 10 samples ~ 5 sec)
            if (g_latestLayer1Result.status != lastStatus || (sampleCounter % 10 == 0)) {
                lastStatus = g_latestLayer1Result.status;

                const char* statusStr = "NORMAL";
                if (g_latestLayer1Result.status == PlausibilityStatus::PLAUSIBLE) statusStr = "PLAUSIBLE";
                else if (g_latestLayer1Result.status == PlausibilityStatus::SUSPICIOUS) statusStr = "SUSPICIOUS";

                Serial.printf("[LAYER1 DIAG] gas=%.3fV, baseline=%.3fV, ratio=%.2f, power=%.1fmW, active=%d, timeSincePwr=%ums -> STATUS: %s\n",
                              g_latestLayer1Result.currentGas,
                              g_latestLayer1Result.baselineGas,
                              g_latestLayer1Result.gasChangeRatio,
                              g_latestLayer1Result.recentPower,
                              g_latestLayer1Result.powerActivityDetected ? 1 : 0,
                              g_latestLayer1Result.timeSincePowerActivityMs,
                              statusStr);
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
