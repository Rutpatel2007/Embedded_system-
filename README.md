# TrueSense — Provenance-Aware Emissions Monitoring System

**TrueSense** is an IoT & embedded security project that builds a low-cost emissions-monitoring node whose readings are not merely measured, but are accompanied by evidence that the reading is **physically plausible**, comes from the **expected physical sensor**, and has **not been modified after recording**.

---

## 1. Core Problem & Three Trust Layers

Standard emissions sensors answer: *"What did the sensor report?"*

TrueSense answers three additional trust questions:

| Layer | Trust Question | Physical Mechanism / Verification |
| :--- | :--- | :--- |
| **Layer 1: Physical Plausibility** | Is this gas reading physically consistent with the equipment actually operating? | Cross-modal analysis between MQ-135 emissions and INA219 equipment power consumption. |
| **Layer 2: Sensor Identity** | Is this the same physical sensor that was previously enrolled on this node? | Dynamic warm-up curve fingerprinting ($C_{\text{live}}$ vs $C_{\text{enrolled}}$ cosine similarity). |
| **Layer 3: Data Integrity** | Has this record been modified after it was recorded? | Cryptographic SHA-256 hash-chain logging stored locally on microSD and independently verified by FastAPI backend. |

---

## 2. System Architecture

```text
    +-----------------------------------------------------------+
    |                     TrueSense Hardware                    |
    |                                                           |
    |   +----------------+               +------------------+   |
    |   |  MQ-135 Gas    |               |  INA219 Power    |   |
    |   |  (Emissions)   |               |  (Equipment)     |   |
    |   +-------+--------+               +--------+---------+   |
    |           |                                 |             |
    |           v                                 v             |
    |   +---------------------------------------------------+   |
    |   |                ESP32 MCU / FreeRTOS               |   |
    |   +---+-----------------+------------------+------+---+   |
    |       |                 |                  |      |       |
    |       v                 v                  v      v       |
    |   +-------+        +----------+        +-------+ +----+   |
    |   |DS3231 |        | microSD  |        | Relay | |WiFi|   |
    |   |  RTC  |        | Storage  |        |Control| +--+-+   |
    |   +-------+        +----------+        +-------+    |     |
    +-----------------------------------------------------|-----+
                                                          |
                                                          v
                                                 +-----------------+
                                                 | FastAPI Backend |
                                                 +--------+--------+
                                                          |
                                                          v
                                                 +-----------------+
                                                 | PostgreSQL DB   |
                                                 +--------+--------+
                                                          |
                                                          v
                                                 +-----------------+
                                                 | HTML5 Dashboard |
                                                 +-----------------+
```

---

## 3. Shared Engineering Contracts (Phase 0)

All components operate under strict shared interface definitions located in `/docs`:

1. [Hardware Specification (`docs/hardware.md`)](file:///c:/Users/Rut%20Patel/OneDrive/Desktop/embedded_systems/docs/hardware.md): GPIO pinouts, I2C addresses, and isolated power domain definitions.
2. [Data Contract (`docs/data-contract.md`)](file:///c:/Users/Rut%20Patel/OneDrive/Desktop/embedded_systems/docs/data-contract.md): Canonical JSON schema, data types, precision, and enum values.
3. [Hash Protocol Specification (`docs/hash-protocol.md`)](file:///c:/Users/Rut%20Patel/OneDrive/Desktop/embedded_systems/docs/hash-protocol.md): Cross-language SHA-256 string serialization format for ESP32 (mbedTLS) and Python (`hashlib`).
4. [Backend API Contract (`docs/api-contract.md`)](file:///c:/Users/Rut%20Patel/OneDrive/Desktop/embedded_systems/docs/api-contract.md): REST endpoints, request/response formats, and verification services.
5. [Hardware Pin Header (`firmware/include/config.h`)](file:///c:/Users/Rut%20Patel/OneDrive/Desktop/embedded_systems/firmware/include/config.h): ESP32 GPIO pin definitions and algorithm parameters.

---

## 4. Repository Structure

```text
embedded_systems/
│
├── docs/
│   ├── hardware.md            # Hardware contract & GPIO map
│   ├── data-contract.md       # JSON schema & enum specifications
│   ├── hash-protocol.md       # SHA-256 serialization protocol
│   └── api-contract.md        # REST API endpoints & schemas
│
├── firmware/                  # Person A ownership
│   ├── platformio.ini         # PlatformIO build configuration
│   └── include/
│       └── config.h           # System GPIO and algorithm parameters
│
├── backend/                   # Person B ownership
│
├── dashboard/                 # Person B ownership
│
└── README.md                  # Project overview & architecture
```

---

## 5. Engineering Review & Identified Specification Contradictions

Before freezing Phase 0 contracts for feature implementation, the following potential conflicts have been identified for engineering review:

1. **Layer 2 (Fingerprint) vs. Power Architecture Tension**:
   - *Conflict*: Domain 1 (MQ heater) is designed to be **Always ON** (5V directly from power rail), while Layer 2 fingerprinting assumes measuring a 20-point warm-up transient curve upon boot ($C_{\text{live}}$). If the MQ heater remains hot across MCU reboots, warm-up transients will not be present.
   - *Proposed Resolution*: Define a hardware boot condition or software flag to distinguish a cold power-on boot from a warm MCU software reset.
2. **MQ-135 Load Resistor ($R_L$) & ADC Calibration**:
   - *Conflict*: MQ-135 breakout modules vary in built-in load resistor $R_L$ (1k$\Omega$ to 20k$\Omega$).
   - *Proposed Resolution*: Measure the exact physical $R_L$ value on the actual hardware module before finalizing PPM conversion equations in firmware.
3. **Floating Point Canonical Serialization**:
   - *Constraint*: `gas_ppm` and `power_mW` must strictly enforce 3 decimal places (`%.3f`) across C++ and Python to prevent subtle hash mismatch issues (`42.38` vs `42.380`).

## Sensor Fingerprint Enrollment

Use `ENROLL` on a first-use node to save the current MQ-135 signature as the trusted sensor. Use `REENROLL` only when intentionally replacing the MQ-135 sensor; it overwrites the trusted signature with the replacement sensor's signature after reboot.
# Firmware orchestration now verifies the boot chain, supervises critical tasks with the watchdog, reports stack and queue health, and provides HEALTH/STATS diagnostics.
