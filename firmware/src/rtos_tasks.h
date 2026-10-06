#ifndef RTOS_TASKS_H
#define RTOS_TASKS_H

#include <Arduino.h>
#include "sensor_sample.h"
#include "sensor_provider.h"
#include "ring_buffer.h"
#include "layer1_plausibility.h"
#include "layer2_fingerprint.h"
#include "hash_chain.h"
#include "../include/config.h"

// Global Ring Buffers, Layer 1, Layer 2 & Hash Chain Engine Accessors
const RingBuffer<SensorSample, GAS_RING_BUFFER_SIZE>& getGasRingBuffer();
const RingBuffer<PowerSample, POWER_RING_BUFFER_SIZE>& getPowerRingBuffer();
const Layer1Result& getLatestLayer1Result();
const Layer2Result& getLatestLayer2Result();
Layer2Engine& getLayer2Engine();
HashChainEngine& getHashChainEngine();

uint32_t getProcessedSampleCount();
uint32_t getDroppedSampleCount();
float getAverageJitterMs();

// Initialize FreeRTOS Queues & Tasks
bool initRTOSTasks(ISensorProvider* provider);

// FreeRTOS Task Prototypes
void sensorTask(void* pvParameters);
void algorithmTask(void* pvParameters);
void healthTask(void* pvParameters);

#endif // RTOS_TASKS_H
