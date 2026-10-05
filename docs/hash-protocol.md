# TrueSense Hash Protocol & Canonical Serialization Specification

This document defines the exact deterministic string serialization format required for calculating SHA-256 hash digests across C++ (ESP32 mbedTLS / SHA-256) and Python (FastAPI `hashlib`).

To prevent hash mismatches between firmware logging and backend verification, **both systems must follow this specification without deviation**.

---

## 1. SHA-256 Hash Construction

The hash digest of record $n$ ($H_n$) is calculated by taking the SHA-256 hash of the **Canonical Serialization String** ($S_n$):

$$H_n = \text{SHA-256}(S_n)$$

Where $S_n$ is formed by concatenating the record's payload fields and the preceding record's hash ($H_{n-1}$) using a pipe (`|`) delimiter.

---

## 2. Canonical Field Order & Format

The string $S_n$ must be concatenated in the following exact order:

```text
device_id|timestamp|gas_ppm|power_mW|plausibility|fingerprint_status|previous_hash
```

### Formatting Rules:

| Field Index | Field | Formatting Rule | Example |
| :--- | :--- | :--- | :--- |
| 1 | `device_id` | Plain string | `TS001` |
| 2 | `timestamp` | 64-bit integer, Unix epoch in seconds | `1750000000` |
| 3 | `gas_ppm` | Float formatted to **exactly 3 decimal places** (`%.3f`) | `42.381` |
| 4 | `power_mW` | Float formatted to **exactly 3 decimal places** (`%.3f`) | `823.420` |
| 5 | `plausibility` | Exact uppercase enum string | `NORMAL` |
| 6 | `fingerprint_status` | Exact uppercase enum string | `SENSOR_OK` |
| 7 | `previous_hash` | 64-character lowercase hex string | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |

---

## 3. Critical Formatting Constraints

1. **Delimiter**: Pipe character `|` (`ASCII 0x7C`).
2. **Float Formatting**:
   - Must use standard C `%.3f` / Python `f"{val:.3f}"`.
   - Examples:
     - `42.3` $\rightarrow$ `42.300`
     - `0` $\rightarrow$ `0.000`
     - `42.3819` $\rightarrow$ `42.382` (rounded)
3. **No Whitespace**: No spaces around delimiters, no leading or trailing whitespace.
4. **No Newlines**: Do not include `\n` or `\r` anywhere in the serialization string.
5. **Character Encoding**: Standard UTF-8 / ASCII.
6. **Genesis Hash**: For the first record ($n=1$), `previous_hash` is 64 ASCII zero characters:
   `0000000000000000000000000000000000000000000000000000000000000000`

---

## 4. Canonical Example

Given the payload:
```json
{
  "device_id": "TS001",
  "timestamp": 1750000000,
  "gas_ppm": 42.381,
  "power_mW": 823.420,
  "plausibility": "NORMAL",
  "fingerprint_status": "SENSOR_OK",
  "previous_hash": "0000000000000000000000000000000000000000000000000000000000000000"
}
```

### Canonical Serialization String ($S_1$):
```text
TS001|1750000000|42.381|823.420|NORMAL|SENSOR_OK|0000000000000000000000000000000000000000000000000000000000000000
```

### Resulting SHA-256 Digest ($H_1$):
```text
3e7cb4f09d8aa0e71691168f187a5a3a2e379bc83f0631bfaeb24b2165215d2a
```
*(Note: Verification tests in C++ and Python will validate against this exact canonical example).*

---

## 5. Cross-Language Reference Implementation

### C++ (ESP32 / mbedTLS) Reference:
```cpp
#include <stdio.h>
#include <string.h>
#include "mbedtls/sha256.h"

String serialize_record(const char* device_id, uint64_t timestamp, float gas_ppm, float power_mW, const char* plausibility, const char* fingerprint_status, const char* previous_hash) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s|%llu|%.3f|%.3f|%s|%s|%s",
             device_id, (unsigned long long)timestamp, gas_ppm, power_mW, plausibility, fingerprint_status, previous_hash);
    return String(buf);
}

String compute_sha256(const String& input) {
    unsigned char hash[32];
    mbedtls_sha256((const unsigned char*)input.c_str(), input.length(), hash, 0);
    char hex_str[65];
    for (int i = 0; i < 32; i++) {
        sprintf(hex_str + (i * 2), "%02x", hash[i]);
    }
    hex_str[64] = '\0';
    return String(hex_str);
}
```

### Python (FastAPI / Backend) Reference:
```python
import hashlib

def serialize_record(device_id: str, timestamp: int, gas_ppm: float, power_mW: float, plausibility: str, fingerprint_status: str, previous_hash: str) -> str:
    return f"{device_id}|{timestamp}|{gas_ppm:.3f}|{power_mW:.3f}|{plausibility}|{fingerprint_status}|{previous_hash}"

def compute_sha256(canonical_str: str) -> str:
    return hashlib.sha256(canonical_str.encode('utf-8')).hexdigest()
```
