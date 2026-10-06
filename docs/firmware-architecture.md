# TrueSense Firmware Architecture Specification (Phase 2)

This document details the software architecture, task execution model, inter-task communication, memory bounds, and integration state for the TrueSense ESP32 firmware.

---

## 1. System Overview & Implementation State Legend

| Component / Layer | Status | Notes |
| :--- | :--- | :--- |
| **Hardware Drivers (MQ135, INA219, DS3231, SD, Relay)** | `IMPLEMENTED` | Phase 1 completed. Drivers verified & operational. |
| **Sensor Provider Abstraction (`ISensorProvider`)** | `IMPLEMENTED` | Decouples acquisition from physical hardware vs simulation harness. |
| **Mock Simulation Harness (`MockSensorProvider`)** | `IMPLEMENTED` | Supports `SCENARIO_NORMAL`, `SCENARIO_PLAUSIBLE`, `SCENARIO_SUSPICIOUS`, `SCENARIO_FAULT`. |
| **500 ms Deterministic Scheduler (`SensorTask`)** | `IMPLEMENTED` | Uses FreeRTOS `vTaskDelayUntil` pinned to Core 1. |
| **Bounded Sample Queue (`xQueueHandle`)** | `IMPLEMENTED` | Fixed capacity of 10 `SensorSample` items. |
| **Bounded Ring Buffers (`RingBuffer<T, N>`)** | `IMPLEMENTED` | 60s Gas History (120 slots), 90s Power History (180 slots), zero heap allocations. |
| **Health Monitoring Task (`HealthTask`)** | `IMPLEMENTED` | 5000 ms interval checking free heap, queue usage, sample drops, and jitter. |
| **Layer 1 Physical Plausibility Engine** | `IMPLEMENTED` | Cross-modal analysis between gas signal ($V_{\text{sensor}}$) and trailing power history ($P_{\text{equipment}}$). |
| **Layer 2 Sensor Identity Fingerprint** | `PLANNED` | Pending physical resolution of cold-boot vs heater-power architecture. |
| **microSD Storage & Hash-Chain Task** | `PLANNED` | Block storage task structure prepared for Phase 3 integration. |
| **WiFi Sync Client (`NetworkTask`)** | `PLANNED` | Asynchronous background client prepared for Phase 4. |
| **MQ-135 Load Resistor $R_L$ & PPM Conversion** | `UNVALIDATED` | Sensor resistance ratio and PPM math pending physical module measurement. |
| **Power Active & Gas Spike Thresholds** | `UNVALIDATED` | Initial parameters (`50 mW`, `20%` spike) are configurable starting estimates. |

---

## 2. FreeRTOS Task & Queue Architecture

```text
                      ESP32 Dual Core (240 MHz)
                        │
         +--------------+--------------+
         |                             |
         v (Core 1)                    v (Core 0)
    +------------+              +---------------+             +------------+
    | SensorTask |              | AlgorithmTask |             | HealthTask |
    |  (500 ms)  |              |  (Queue-driven|             |  (5000 ms) |
    +-----+------+              +-------+-------+             +-----+------+
          |                             |                           |
          | xQueueSend                  | RingBuffer                | ESP.getFreeHeap()
          v                             v                           v
    +------------+              +---------------+             +------------+
    |sampleQueue |------------->|Gas (120 slots)|             | Diagnostic |
    | (10 slots) |              |Pwr (180 slots)|             |   Logs     |
    +------------+              +---------------+             +------------+
```

### Task Specifications:

1. **`SensorTask`** (Priority 3, Core 1, Stack 4096B):
   - Wakes every 500 ms using `vTaskDelayUntil`.
   - Reads active `ISensorProvider` (Hardware or Mock).
   - Measures sampling interval jitter (expected 500 ms vs actual delta).
   - Pushes `SensorSample` to `sampleQueue`. Logs overflow if queue fills up.
2. **`AlgorithmTask`** (Priority 2, Core 0, Stack 4096B):
   - Blocks on `sampleQueue` until a new sample arrives.
   - Pushes `SensorSample` into 60-second Gas Ring Buffer (120 slots) and 90-second Power Ring Buffer (180 slots).
   - Emits development CSV telemetry: `[TELEMETRY] timestamp,gasRaw,gasVoltage,sensorVoltage,power_mW,equipmentActive,gasValid,powerValid`.
3. **`HealthTask`** (Priority 1, Core 0, Stack 2048B):
   - Runs every 5000 ms.
   - Measures free heap memory, queue occupancy, total samples processed, total samples dropped, and exponential moving average jitter.

---

## 3. Sensor Provider Abstraction & Simulation Modes

The acquisition layer uses the `ISensorProvider` virtual interface:

```cpp
class ISensorProvider {
public:
    virtual ~ISensorProvider() {}
    virtual bool begin() = 0;
    virtual bool readSample(SensorSample& sample) = 0;
    virtual const char* getProviderName() const = 0;
};
```

### Mode Selection:
- **`#define USE_MOCK_SENSORS`** in `config.h`:
  - When **defined**: Activates `MockSensorProvider`. Firmware runs hardware-independently with simulated scenario datasets (`SCENARIO_NORMAL`, `SCENARIO_PLAUSIBLE`, `SCENARIO_SUSPICIOUS`, `SCENARIO_FAULT`). Serial logs `[MODE] MOCK SENSOR PROVIDER (SIMULATION VALIDATED)`.
  - When **commented out**: Activates `HardwareSensorProvider`. Firmware connects to physical I2C/SPI sensors (`MQ135Sensor`, `INA219Sensor`, `RTCDriver`, `RelayController`). Serial logs `[MODE] REAL HARDWARE PROVIDER (PHYSICAL HARDWARE VALIDATED)`.

---

## 4. Memory Bounds & Buffer Management

- **Queue Capacity**: `SENSOR_QUEUE_LEN = 10` samples (~5 seconds of data buffer).
- **Gas History Buffer**: `GAS_RING_BUFFER_SIZE = 120` samples ($120 \times 0.5\text{s} = 60\text{ seconds}$).
- **Power History Buffer**: `POWER_RING_BUFFER_SIZE = 180` samples ($180 \times 0.5\text{s} = 90\text{ seconds}$).
- **Static Allocation**: All ring buffers use static internal arrays (`T _buffer[N]`). No dynamic heap allocations occur during continuous 500 ms sampling loops.

---

## 5. Instructions for Switching to Real Hardware

When physical hardware becomes available:
1. Open `firmware/include/config.h`.
2. Comment out line:
   ```cpp
   // #define USE_MOCK_SENSORS
   ```
3. Recompile and upload via PlatformIO (`pio run -t upload`).
4. The system will automatically select `HardwareSensorProvider` without changing any task, queue, or algorithm code.
