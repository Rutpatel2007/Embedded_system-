# TrueSense Project Rules

- Hardware: I2C SDA GPIO21, SCL GPIO22 (INA219 @0x40, DS3231 @0x68); SPI microSD MOSI 23, MISO 19, SCK 18, CS 5; MQ-135 analog out GPIO34; relay control GPIO4. GPIO12 and GPIO2 are forbidden (boot strapping).
- No code comments, no explanatory prose in code files.
- All pins and constants live only in `src/config.h`.
- One `.cpp`/`.h` pair per module; `main.cpp` only initializes and creates tasks.
- Inter-task communication via FreeRTOS queues only, no shared globals.
- Local logging must never depend on WiFi.
- Never use floats in hashed or serialized data; use scaled integers.
- Every module exposes `init()` and a clear API in its header.
- After each change, run `pio run` and fix all warnings and errors before finishing.
- Hash rule: `hash` is lowercase hexadecimal SHA-256 of the exact serialized record, including its `previous_hash` field. The genesis hash is lowercase hexadecimal SHA-256 of `TRUESENSE-GENESIS-` followed by the device ID.
