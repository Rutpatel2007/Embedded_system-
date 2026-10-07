#!/usr/bin/env python3
"""
TrueSense Phase 3C Cryptographic Hash Chain Verifier & Cross-Language Reference
Matches C++ (ESP32 mbedTLS) hash chain serialization and SHA-256 byte string parity.
"""

import sys
import json
import hashlib

GENESIS_HASH = "0000000000000000000000000000000000000000000000000000000000000000"

def serialize_record(device_id: str, timestamp: int, gas_ppm: float, power_mW: float, plausibility: str, fingerprint_status: str, previous_hash: str) -> str:
    """
    Exact canonical serialization format:
    device_id|timestamp|gas_ppm|power_mW|plausibility|fingerprint_status|previous_hash
    Floats are formatted to exactly 3 decimal places (%.3f).
    """
    return f"{device_id}|{timestamp}|{gas_ppm:.3f}|{power_mW:.3f}|{plausibility}|{fingerprint_status}|{previous_hash}"

def compute_sha256(canonical_str: str) -> str:
    """Computes SHA-256 digest returning 64 lowercase hex characters."""
    return hashlib.sha256(canonical_str.encode('utf-8')).hexdigest()

def verify_test_vector_1() -> bool:
    """Vector 1 Verification"""
    canonical = serialize_record("TS001", 1750000000, 42.381, 823.420, "NORMAL", "SENSOR_OK", GENESIS_HASH)
    expected_canonical = "TS001|1750000000|42.381|823.420|NORMAL|SENSOR_OK|0000000000000000000000000000000000000000000000000000000000000000"
    digest = compute_sha256(canonical)
    expected_digest = "2e8e7995bb9576188874c028488bafb8a5973133134cbb123182b3c79051dcd3"

    match_str = (canonical == expected_canonical)
    match_hash = (digest == expected_digest)

    print(f"Vector 1 Canonical String Match: {'PASS' if match_str else 'FAIL'}")
    print(f"Vector 1 Hash Digest Match:     {'PASS' if match_hash else 'FAIL'}")
    return match_str and match_hash

def verify_test_vector_2() -> bool:
    """Vector 2 Verification"""
    prev_hash = "2e8e7995bb9576188874c028488bafb8a5973133134cbb123182b3c79051dcd3"
    canonical = serialize_record("TS001", 1750000500, 120.500, 850.000, "PLAUSIBLE", "SENSOR_OK", prev_hash)
    expected_canonical = f"TS001|1750000500|120.500|850.000|PLAUSIBLE|SENSOR_OK|{prev_hash}"
    digest = compute_sha256(canonical)
    expected_digest = "5b1e21572e9523d3d1ec209de9d7678f0f649fc7f7bde55cf991d0a6940611a6"

    match_str = (canonical == expected_canonical)
    match_hash = (digest == expected_digest)

    print(f"Vector 2 Canonical String Match: {'PASS' if match_str else 'FAIL'}")
    print(f"Vector 2 Hash Digest Match:     {'PASS' if match_hash else 'FAIL'}")
    return match_str and match_hash

def verify_jsonl_file(file_path: str) -> bool:
    """Verifies an entire .jsonl hash chain file."""
    print(f"\nVerifying JSONL Chain File: {file_path}")
    expected_prev = GENESIS_HASH
    record_count = 0

    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            for line_idx, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                rec = json.loads(line)
                
                canonical = serialize_record(
                    rec["device_id"],
                    rec["timestamp"],
                    float(rec["gas_ppm"]),
                    float(rec["power_mW"]),
                    rec["plausibility"],
                    rec["fingerprint_status"],
                    rec["previous_hash"]
                )
                
                computed_hash = compute_sha256(canonical)

                if rec["previous_hash"] != expected_prev:
                    print(f"[FAIL] Line {line_idx}: previous_hash mismatch! Stored: {rec['previous_hash']}, Expected: {expected_prev}")
                    return False

                if computed_hash != rec["hash"]:
                    print(f"[FAIL] Line {line_idx}: Hash verification failed! Stored: {rec['hash']}, Computed: {computed_hash}")
                    return False

                expected_prev = rec["hash"]
                record_count += 1

        print(f"[SUCCESS] Verified {record_count} records. Hash chain intact!")
        return True
    except Exception as e:
        print(f"[ERROR] Verification failed with exception: {e}")
        return False

if __name__ == "__main__":
    print("==========================================")
    print("  TrueSense Cross-Language Verifier (Python)")
    print("==========================================")

    v1_ok = verify_test_vector_1()
    v2_ok = verify_test_vector_2()

    if len(sys.argv) > 1:
        file_ok = verify_jsonl_file(sys.argv[1])
        sys.exit(0 if (v1_ok and v2_ok and file_ok) else 1)
    else:
        sys.exit(0 if (v1_ok and v2_ok) else 1)
