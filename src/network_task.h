#ifndef TRUESENSE_NETWORK_TASK_H
#define TRUESENSE_NETWORK_TASK_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "storage.h"

struct NetworkRuntime {
    SyncCursor cursor;
    uint32_t retryDelayMs;
    uint32_t retryAtMs;
    uint32_t connectionStartedAtMs;
    uint32_t lastSyncAtMs;
    uint32_t lastSuccessTimestamp;
    int lastHttpCode;
    bool connecting;
    bool immediateSync;
};

namespace network_task_module {
void init();
bool connectWiFi(NetworkRuntime& runtime);
bool syncPendingRecords(NetworkRuntime& runtime);
bool wifiConnected();
void registerNetworkCommands();
void startNetworkTask();
void networkTask(void* parameter);
}

#endif
