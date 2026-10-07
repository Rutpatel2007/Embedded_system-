"""
Seed Demo Script for TrueSense Backend.
Ingests simulation_output/log_chain.jsonl into a local demo backend through POST /api/v1/readings/ingest.

Usage:
    python scripts/seed_demo.py [--backend http://127.0.0.1:8000] [--batch-size 50]
"""

import argparse
import json
import os
import sys
import urllib.request
import urllib.error

DEFAULT_BACKEND = "http://127.0.0.1:8000"
LOG_CHAIN_PATH = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "simulation_output",
    "log_chain.jsonl",
)


def register_device_if_needed(backend_url: str, device_id: str = "TS001"):
    url = f"{backend_url}/api/v1/devices/register"
    # Canonical 20-sample reference warm-up fingerprint curve
    payload = {
        "device_id": device_id,
        "name": f"Factory Exhaust Node ({device_id})",
        "enrolled_fingerprint": [
            1.000, 0.982, 0.954, 0.921, 0.893, 0.871, 0.852, 0.840, 0.831, 0.820,
            0.812, 0.804, 0.801, 0.795, 0.790, 0.785, 0.782, 0.780, 0.775, 0.771,
        ],
    }
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req) as resp:
            print(f"[+] Registered device {device_id}: {resp.status}")
    except urllib.error.HTTPError as e:
        if e.code == 409:
            print(f"[*] Device {device_id} already registered.")
        else:
            print(f"[-] Failed to register device: {e.code} {e.read().decode('utf-8')}")
            sys.exit(1)


def ingest_batches(backend_url: str, filepath: str, batch_size: int = 50):
    if not os.path.exists(filepath):
        print(f"[-] Simulation log chain file not found: {filepath}")
        sys.exit(1)

    records = []
    with open(filepath, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                records.append(json.loads(line))

    print(f"[+] Loaded {len(records)} records from simulation log.")
    device_id = records[0]["device_id"]

    ingest_url = f"{backend_url}/api/v1/readings/ingest"

    total_ingested = 0
    for i in range(0, len(records), batch_size):
        batch = records[i : i + batch_size]
        payload = {
            "device_id": device_id,
            "readings": batch,
        }
        data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            ingest_url,
            data=data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(req) as resp:
                resp_json = json.loads(resp.read().decode("utf-8"))
                total_ingested += resp_json.get("accepted_count", len(batch))
                print(
                    f"[+] Ingested batch {i // batch_size + 1}/{(len(records) + batch_size - 1) // batch_size}: "
                    f"{total_ingested}/{len(records)} records committed."
                )
        except urllib.error.HTTPError as e:
            print(f"[-] Ingest error on batch {i}: {e.code} {e.read().decode('utf-8')}")
            sys.exit(1)

    print(f"[SUCCESS] Successfully seeded {total_ingested} records into {backend_url}!")


def main():
    parser = argparse.ArgumentParser(description="Seed demo backend with simulation log chain.")
    parser.add_argument("--backend", default=DEFAULT_BACKEND, help="Backend URL (default: http://127.0.0.1:8000)")
    parser.add_argument("--batch-size", type=int, default=50, help="Batch size for POST /readings/ingest")
    parser.add_argument("--file", default=LOG_CHAIN_PATH, help="Path to log_chain.jsonl")

    args = parser.parse_args()

    # Verify health
    try:
        with urllib.request.urlopen(f"{args.backend}/api/v1/health") as resp:
            print(f"[+] Backend health check passed: {resp.status}")
    except Exception as e:
        print(f"[-] Cannot connect to backend at {args.backend}: {e}")
        print("    Please start the FastAPI server first:")
        print("    uvicorn app.main:app --host 127.0.0.1 --port 8000 --app-dir backend")
        sys.exit(1)

    register_device_if_needed(args.backend, "TS001")
    ingest_batches(args.backend, args.file, args.batch_size)


if __name__ == "__main__":
    main()
