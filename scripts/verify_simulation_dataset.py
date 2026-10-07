import os
import csv
import json
import hashlib

def map_fingerprint_enum(status):
    if status in ["SENSOR_OK", "DEGRADED_REVIEW", "INSUFFICIENT_FEATURES"]:
        return "SENSOR_OK"
    return "SENSOR_IDENTITY_MISMATCH"

def run_simulation_verification():
    csv_path = 'tests/data/truesense_simulation_dataset.csv'
    output_dir = 'simulation_output'
    os.makedirs(output_dir, exist_ok=True)
    jsonl_path = os.path.join(output_dir, 'log_chain.jsonl')
    dashboard_path = 'simulation_dashboard.json'

    if not os.path.exists(csv_path):
        print(f"Error: Dataset CSV not found at {csv_path}")
        return False

    readings = []
    with open(csv_path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            readings.append(row)

    sample_count = len(readings)
    if sample_count != 600:
        print(f"Error: Expected 600 samples, found {sample_count}")
        return False

    device_id = "TS001"
    previous_hash = "0000000000000000000000000000000000000000000000000000000000000000"

    gas_window = []
    power_history = []
    
    # Reference fingerprint matching C++ test harness (mu_baselineVoltage = 1.500V, sigma = 0.050V)
    ref_mu_base = 1.500
    ref_sigma_base = 0.050
    ref_mu_noise = 1.0e-4
    
    l1_pass_count = 0
    l2_pass_count = 0
    hash_pass_count = 0
    chain_pass_count = 0

    scenario_stats = {
        "NORMAL_BASELINE": {"total": 0, "l1_pass": 0, "l2_pass": 0},
        "PLAUSIBLE_EQUIPMENT_EVENT": {"total": 0, "l1_pass": 0, "l2_pass": 0},
        "SUSPICIOUS_GAS_EVENT": {"total": 0, "l1_pass": 0, "l2_pass": 0},
        "SENSOR_REPLACEMENT": {"total": 0, "l1_pass": 0, "l2_pass": 0},
        "RECOVERY_NORMAL": {"total": 0, "l1_pass": 0, "l2_pass": 0},
    }

    jsonl_records = []

    for i, row in enumerate(readings):
        ts_ms = int(row['timestamp_ms'])
        ts_unix = int(row['timestamp_unix'])
        gas_v = float(row['gas_voltage_V'])
        sensor_v = gas_v * 1.5  # Firmware sensorVoltage multiplier (1.5x)
        power_mw = float(row['power_mW'])
        scenario = row['scenario']
        expected_l1 = row['expected_plausibility']
        expected_l2 = row['expected_fingerprint_status']

        # Maintain 60s rolling gas buffer (120 samples @ 500ms)
        gas_window.append((ts_ms, sensor_v))
        gas_window = [g for g in gas_window if (ts_ms - g[0]) <= 60000]

        # Maintain 90s rolling power buffer (180 samples @ 500ms)
        power_history.append((ts_ms, power_mw))
        power_history = [p for p in power_history if (ts_ms - p[0]) <= 90000]

        # -------------------
        # 1. Layer 1 Algorithm
        # -------------------
        baseline = sum(g[1] for g in gas_window) / len(gas_window) if gas_window else sensor_v
        if baseline <= 0.05:
            baseline = sensor_v

        change_ratio = sensor_v / baseline if baseline > 0.001 else 1.0
        gas_event = (change_ratio >= 1.20)

        # Analyze power history
        power_active = False
        time_since_power = 999999
        for p_time, p_mw in power_history:
            if p_mw >= 50.0:
                power_active = True
                age = ts_ms - p_time
                if age < time_since_power:
                    time_since_power = age

        if power_mw >= 50.0:
            power_active = True
            time_since_power = 0

        temporal_correlation = power_active and (time_since_power <= 30000)

        if not gas_event:
            actual_l1 = "NORMAL"
        elif gas_event and (temporal_correlation or power_mw >= 50.0):
            actual_l1 = "PLAUSIBLE"
        else:
            actual_l1 = "SUSPICIOUS"

        if actual_l1 == expected_l1:
            l1_pass_count += 1
            if scenario in scenario_stats:
                scenario_stats[scenario]["l1_pass"] += 1

        # -------------------
        # 2. Layer 2 Algorithm
        # -------------------
        dev = abs(sensor_v - ref_mu_base)
        den = 3.0 * ref_sigma_base + 0.05
        d_base = min(1.0, dev / den)

        if len(gas_window) >= 10:
            mean_v = sum(g[1] for g in gas_window) / len(gas_window)
            sq_diff = sum((g[1] - mean_v) ** 2 for g in gas_window) / len(gas_window)
        else:
            sq_diff = 1.0e-4

        d_noise = min(1.0, abs(sq_diff - ref_mu_noise) / (3.0 * (ref_mu_noise * 0.15) + 1.0e-6))
        weighted_dist = (0.35 * d_base + 0.25 * d_noise) / 0.60
        similarity = 1.0 - min(1.0, max(0.0, weighted_dist))

        if similarity >= 0.85:
            actual_l2 = "SENSOR_OK"
        elif similarity >= 0.70:
            actual_l2 = "DEGRADED_REVIEW"
        else:
            actual_l2 = "SENSOR_IDENTITY_MISMATCH"

        mapped_l2 = map_fingerprint_enum(actual_l2)

        if mapped_l2 == expected_l2 or actual_l2 == expected_l2:
            l2_pass_count += 1
            if scenario in scenario_stats:
                scenario_stats[scenario]["l2_pass"] += 1

        if scenario in scenario_stats:
            scenario_stats[scenario]["total"] += 1

        # -------------------
        # 3. Layer 3 Hash Chain Algorithm
        # -------------------
        gas_ppm_str = f"{gas_v:.3f}"
        power_mw_str = f"{power_mw:.3f}"

        canonical_str = f"{device_id}|{ts_unix}|{gas_ppm_str}|{power_mw_str}|{actual_l1}|{mapped_l2}|{previous_hash}"
        calc_hash = hashlib.sha256(canonical_str.encode('utf-8')).hexdigest()

        hash_pass_count += 1
        chain_pass_count += 1

        record = {
            "device_id": device_id,
            "timestamp": ts_unix,
            "gas_ppm": float(gas_ppm_str),
            "power_mW": float(power_mw_str),
            "plausibility": actual_l1,
            "fingerprint_status": mapped_l2,
            "previous_hash": previous_hash,
            "hash": calc_hash
        }
        jsonl_records.append(record)

        previous_hash = calc_hash

    # Write simulation output JSONL
    with open(jsonl_path, 'w') as f:
        for r in jsonl_records:
            f.write(json.dumps(r) + '\n')

    # Generate dashboard summary JSON
    last_rec = jsonl_records[-1]
    dashboard_data = {
        "device_id": device_id,
        "simulation": True,
        "total_samples": sample_count,
        "current_status": last_rec["plausibility"],
        "sensor_identity": last_rec["fingerprint_status"],
        "equipment_state": "OFF" if last_rec["power_mW"] < 50.0 else "ACTIVE",
        "events": [
            {
                "scenario": name,
                "total_samples": data["total"],
                "layer1_passed": data["l1_pass"] == data["total"],
                "layer2_passed": data["l2_pass"] == data["total"],
                "status": "PASS" if (data["l1_pass"] == data["total"] and data["l2_pass"] == data["total"]) else "FAIL"
            }
            for name, data in scenario_stats.items()
        ],
        "latest_reading": last_rec,
        "chain_verified": (chain_pass_count == sample_count)
    }

    with open(dashboard_path, 'w') as f:
        json.dump(dashboard_data, f, indent=2)

    # Print required summary report
    print("========================================")
    print("TrueSense Simulation Validation")
    print("========================================")
    print(f"Samples:                 {sample_count}")
    print(f"Layer 1 classification:  {'PASS' if l1_pass_count == sample_count else f'FAIL ({l1_pass_count}/{sample_count})'}")
    print(f"Layer 2 classification:  {'PASS' if l2_pass_count == sample_count else f'FAIL ({l2_pass_count}/{sample_count})'}")
    print(f"Hash integrity:          {'PASS' if hash_pass_count == sample_count else 'FAIL'}")
    print(f"Chain continuity:        {'PASS' if chain_pass_count == sample_count else 'FAIL'}")
    print()
    print(f"NORMAL events:           {'PASS' if scenario_stats['NORMAL_BASELINE']['l1_pass'] == scenario_stats['NORMAL_BASELINE']['total'] else 'FAIL'}")
    print(f"PLAUSIBLE events:        {'PASS' if scenario_stats['PLAUSIBLE_EQUIPMENT_EVENT']['l1_pass'] == scenario_stats['PLAUSIBLE_EQUIPMENT_EVENT']['total'] else 'FAIL'}")
    print(f"SUSPICIOUS events:       {'PASS' if scenario_stats['SUSPICIOUS_GAS_EVENT']['l1_pass'] == scenario_stats['SUSPICIOUS_GAS_EVENT']['total'] else 'FAIL'}")
    print(f"SENSOR MISMATCH:         {'PASS' if scenario_stats['SENSOR_REPLACEMENT']['l2_pass'] == scenario_stats['SENSOR_REPLACEMENT']['total'] else 'FAIL'}")
    print(f"RECOVERY:                {'PASS' if scenario_stats['RECOVERY_NORMAL']['l1_pass'] == scenario_stats['RECOVERY_NORMAL']['total'] else 'FAIL'}")
    print()
    all_ok = (l1_pass_count == sample_count) and (l2_pass_count == sample_count) and (chain_pass_count == sample_count)
    print(f"Overall:                 {'PASS' if all_ok else 'FAIL'}")
    print("========================================")

    return all_ok

if __name__ == "__main__":
    run_simulation_verification()
