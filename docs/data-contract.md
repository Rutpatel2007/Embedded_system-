# TrueSense Data Contract Specification

This document defines the canonical JSON record schema and data contracts shared between the ESP32 firmware, microSD storage, FastAPI backend, and frontend dashboard.

---

## 1. Canonical Record Schema

Every reading captured by a TrueSense node and stored or transmitted across the network **MUST** adhere strictly to the following JSON structure and field names.

```json
{
  "device_id": "TS001",
  "timestamp": 1750000000,
  "gas_ppm": 42.381,
  "power_mW": 823.420,
  "plausibility": "NORMAL",
  "fingerprint_status": "SENSOR_OK",
  "previous_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
  "hash": "a591a6d40bf420404a011733cfb7b190d62c65bf0bcda32b57b277d9ad9f146e"
}
```

---

## 2. Field Specifications & Constraints

| Field Name | Type | Precision / Format | Valid Range / Allowed Values | Units | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `device_id` | `string` | Alphanumeric (`TS[0-9]{3}`) | e.g. `"TS001"` to `"TS999"` | N/A | Unique hardware node identifier enrolled in system |
| `timestamp` | `integer` | 64-bit integer | Unix Epoch in seconds ($>0$) | Seconds | Time derived from DS3231 RTC module |
| `gas_ppm` | `float` | Fixed 3 decimal places (`%.3f`) | $0.000$ to $10000.000$ | PPM | Calculated gas concentration estimate |
| `power_mW` | `float` | Fixed 3 decimal places (`%.3f`) | $0.000$ to $50000.000$ | mW | Monitored equipment electrical power consumption |
| `plausibility` | `string` | Uppercase Enum | `"NORMAL"`, `"PLAUSIBLE"`, `"SUSPICIOUS"` | N/A | Layer 1 cross-modal physical verification result |
| `fingerprint_status` | `string` | Uppercase Enum | `"SENSOR_OK"`, `"SENSOR_IDENTITY_MISMATCH"` | N/A | Layer 2 sensor identity fingerprint result |
| `previous_hash` | `string` | 64-char Hex (lowercase) | Valid SHA-256 string | N/A | Hash of preceding record ($H_{n-1}$). Genesis = 64 `0`s |
| `hash` | `string` | 64-char Hex (lowercase) | Valid SHA-256 string | N/A | SHA-256 digest of canonical record serialization ($H_n$) |

---

## 3. Enum Definitions

### `plausibility` (Layer 1 Analysis)
- `"NORMAL"`: Gas reading is within baseline limits (no spike detected).
- `"PLAUSIBLE"`: Gas spike detected, and INA219 power history confirms equipment was recently active within the trailing window (90s).
- `"SUSPICIOUS"`: Gas spike detected, but INA219 power history shows equipment was inactive (0 mW / below active threshold).

### `fingerprint_status` (Layer 2 Fingerprint)
- `"SENSOR_OK"`: Measured warm-up curve cosine similarity against enrolled NVS profile is $\ge 0.85$.
- `"SENSOR_IDENTITY_MISMATCH"`: Measured warm-up curve cosine similarity against enrolled NVS profile is $< 0.85$.

---

## 4. Genesis Hash Rule
For the **very first record** produced by a device (Record 1):
```text
previous_hash = "0000000000000000000000000000000000000000000000000000000000000000"
```
(64 ASCII zero characters).
