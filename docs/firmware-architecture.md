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
| **Layer 2 Sensor Identity Fingerprint** | `IMPLEMENTED` | Phase 3B completed. Statistical reference profile in NVS, bounded math, internal `DEGRADED_REVIEW`, serial authorized enrollment. |
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

---

## 6. Layer 2 Sensor Identity Engine Specification (Phase 3B Hardened)

The Layer 2 Sensor Identity Engine evaluates statistical hardware characteristics of physical gas sensors (MQ-135) to verify sensor continuity and detect physical sensor substitution.

### Mathematical Framework & Bounded Distance:

1. **Normalized Feature Distance**:
   $$d_i = \min\left(1.0, \frac{|x_{\text{live}} - \mu_i|}{k \cdot \sigma_i + \epsilon_i}\right)$$
   Where $k=3.0$, $\epsilon_{\text{base}}=0.05\text{V}$, $\epsilon_{\text{noise}}=1\times 10^{-6}\text{V}^2$, $\epsilon_{\text{slope}}=0.001\text{V/s}$.

2. **Total Feature Distance & Similarity**:
   $$D_{\text{fp}} = \frac{\sum w_i d_i}{\sum w_i}$$
   $$\text{Similarity} = 1.0 - D_{\text{fp}}$$
   Guaranteed bounded range: $0.0 \le D_{\text{fp}} \le 1.0$ and $0.0 \le \text{Similarity} \le 1.0$.

3. **Status Classification & Insufficient Features Semantics**:
   - $\text{Similarity} \ge 0.85 \implies \text{SENSOR\_OK}$
   - $0.70 \le \text{Similarity} < 0.85 \implies \text{DEGRADED\_REVIEW}$ (Internal state, public data contract unchanged)
   - $\text{Similarity} < 0.70 \implies \text{SENSOR\_IDENTITY\_MISMATCH}$
   - No reference profile in NVS $\implies \text{UNENROLLED}$
   - Valid features $< 2 \implies \text{INSUFFICIENT\_FEATURES}$ ($\text{similarityScore} = 0.0\text{f}$ representing NOT COMPUTABLE, not a security mismatch alarm)

4. **Security & Enrollment Lifecycle**:
   - Enrollment requires explicit authorized Serial command (`ENROLL_SENSOR_START`, `ENROLL_SENSOR_CONFIRM <password>`).
   - Normal boot **NEVER** automatically enrolls an unknown sensor or silently overwrites an existing valid NVS fingerprint.
   - **Development Password Warning**: `L2_ADMIN_PASSWORD = "TrueSenseAdmin2026"` is **DEVELOPMENT ONLY — NOT PRODUCTION SECURITY**. Hardcoded compiled credentials must be replaced with hardware keystore or challenge-response before production. Credential strings are never printed to Serial or telemetry logs.

5. **Reboot Behavior & Heater Power Domain Rules**:
   - **Cold Power-On**: Domain 1 heater starts cold. Warm-up curve capture is valid (`warmupValid = true`).
   - **MCU Software Reset / Warm Reboot**: Domain 1 heater remains continuously powered. Warm-up feature is omitted (`warmupValid = false`), and weights automatically redistribute among active steady-state features without false alarm penalties.
   - **Sensor Disconnection**: `gasValid = false` sets status to `INSUFFICIENT_FEATURES` without false identity mismatch alarms.

6. **Test Suite Build Gating**:
   - Controlled via `#define ENABLE_FIRMWARE_SELF_TESTS` in `firmware/include/config.h`.
   - **Development Build** (Defined): Executes 12 Layer 1 tests and 28 Layer 2 deterministic tests during startup.
   - **Production Build** (Commented out): Self-tests are bypassed entirely during boot for rapid startup.

7. **Physically Unvalidated Development Parameters**:

| Parameter | Symbol / Macro | Development Value | Physical Status | Calibration Requirement |
| :--- | :--- | :--- | :--- | :--- |
| **Similarity Threshold** | `FINGERPRINT_SIM_THRESHOLD` | `0.85f` | `UNVALIDATED` | Requires physical sensor swap tests across 10+ MQ-135 units |
| **Review Threshold** | `FINGERPRINT_REVIEW_THRESHOLD` | `0.70f` | `UNVALIDATED` | Requires multi-day temperature/humidity drift logging |
| **Baseline Voltage Guard** | $\epsilon_{\text{base}}$ | `0.05 V` | `UNVALIDATED` | Dependent on ESP32 ADC Vref calibration |
| **Noise Variance Guard** | $\epsilon_{\text{noise}}$ | $1\times 10^{-6}\text{ V}^2$ | `UNVALIDATED` | Dependent on power supply ripple measurements |
| **Response Slope Guard** | $\epsilon_{\text{slope}}$ | `0.001 V/s` | `UNVALIDATED` | Dependent on Domain 2 equipment airflow & gas chamber volume |
| **Feature Weights** | $w_{\text{base}}, w_{\text{noise}}, w_{\text{slope}}, w_{\text{warmup}}$ | `0.35, 0.25, 0.20, 0.20` | `UNVALIDATED` | Requires feature importance PCA / discriminative power analysis |
| **Enrollment Samples** | `minEnrollmentSamples` | `300` (~150s) | `UNVALIDATED` | Requires thermal equilibrium measurement of MQ-135 heater |


