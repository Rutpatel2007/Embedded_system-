import os
import math
import csv

def map_fingerprint_enum(status):
    if status in ['SENSOR_OK', 'DEGRADED_REVIEW', 'INSUFFICIENT_FEATURES']:
        return 'SENSOR_OK'
    return 'SENSOR_IDENTITY_MISMATCH'

def generate_dataset():
    os.makedirs('tests/data', exist_ok=True)
    csv_path = 'tests/data/truesense_simulation_dataset.csv'
    
    rows = []
    start_unix = 1750000000
    
    # Generate 600 samples across 5 scenarios
    for i in range(600):
        sample_id = i + 1
        timestamp_ms = i * 500
        timestamp_unix = start_unix + (i // 2)
        noise = float((i % 7) - 3) * 0.001

        if i < 120:
            # Scenario 1: NORMAL_BASELINE (0s - 60s)
            scenario = "NORMAL_BASELINE"
            relay_state = 0
            power_mW = 0.0
            gas_voltage_V = 1.000 + noise

        elif i < 240:
            # Scenario 2: PLAUSIBLE_EQUIPMENT_EVENT (60s - 120s)
            scenario = "PLAUSIBLE_EQUIPMENT_EVENT"
            relay_state = 1
            power_mW = 820.0
            if i < 126:
                gas_voltage_V = 1.000 + noise
            elif i < 170:
                progress = min(1.0, (i - 125) / 5.0)
                gas_voltage_V = 1.000 + progress * 0.300 + noise
            else:
                gas_voltage_V = 1.000 + noise

        elif i < 360:
            # Scenario 3: SUSPICIOUS_GAS_EVENT (120s - 180s)
            scenario = "SUSPICIOUS_GAS_EVENT"
            relay_state = 0
            power_mW = 0.0
            if i < 306:
                gas_voltage_V = 1.000 + noise
            elif i < 350:
                progress = min(1.0, (i - 305) / 5.0)
                gas_voltage_V = 1.000 + progress * 0.350 + noise
            else:
                gas_voltage_V = 1.000 + noise

        elif i < 480:
            # Scenario 4: SENSOR_REPLACEMENT (180s - 240s)
            scenario = "SENSOR_REPLACEMENT"
            relay_state = 1
            power_mW = 820.0
            if i < 366:
                gas_voltage_V = 1.000 + noise
            else:
                progress = min(1.0, (i - 365) / 5.0)
                gas_voltage_V = 1.000 + progress * 1.500 + noise

        else:
            # Scenario 5: RECOVERY_NORMAL (240s - 300s)
            scenario = "RECOVERY_NORMAL"
            relay_state = 0
            power_mW = 0.0
            progress = (i - 480) / 20.0
            decay = math.exp(-progress)
            gas_voltage_V = 1.000 + (1.500 * decay) + noise

        current_mA = 164.0 if power_mW > 50.0 else 0.0
        bus_voltage_V = 5.0
        adc_voltage_V = round(gas_voltage_V, 3)
        adc_raw = int((adc_voltage_V / 3.3) * 4095)
        gas_voltage_V = round(gas_voltage_V, 3)
        power_mW = round(power_mW, 3)

        rows.append({
            "sample_id": sample_id,
            "timestamp_unix": timestamp_unix,
            "timestamp_ms": timestamp_ms,
            "scenario": scenario,
            "relay_state": relay_state,
            "adc_raw": adc_raw,
            "adc_voltage_V": adc_voltage_V,
            "gas_voltage_V": gas_voltage_V,
            "bus_voltage_V": bus_voltage_V,
            "current_mA": current_mA,
            "power_mW": power_mW,
        })

    # Evaluate exact algorithm logic to populate expected_plausibility & expected_fingerprint_status
    gas_window = []
    power_history = []
    ref_mu_base = 1.500
    ref_sigma_base = 0.050
    ref_mu_noise = 1.0e-4

    final_rows = []

    for i, r in enumerate(rows):
        ts_ms = r['timestamp_ms']
        gas_v = r['gas_voltage_V']
        sensor_v = gas_v * 1.5
        power_mw = r['power_mW']

        gas_window.append((ts_ms, sensor_v))
        gas_window = [g for g in gas_window if (ts_ms - g[0]) <= 60000]

        power_history.append((ts_ms, power_mw))
        power_history = [p for p in power_history if (ts_ms - p[0]) <= 90000]

        baseline = sum(g[1] for g in gas_window) / len(gas_window) if gas_window else sensor_v
        if baseline <= 0.05: baseline = sensor_v

        change_ratio = sensor_v / baseline if baseline > 0.001 else 1.0
        gas_event = (change_ratio >= 1.20)

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
            act_l1 = "NORMAL"
        elif gas_event and (temporal_correlation or power_mw >= 50.0):
            act_l1 = "PLAUSIBLE"
        else:
            act_l1 = "SUSPICIOUS"

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
            act_l2 = "SENSOR_OK"
        elif similarity >= 0.70:
            act_l2 = "DEGRADED_REVIEW"
        else:
            act_l2 = "SENSOR_IDENTITY_MISMATCH"

        mapped_l2 = map_fingerprint_enum(act_l2)

        r['expected_plausibility'] = act_l1
        r['expected_fingerprint_status'] = mapped_l2
        final_rows.append(r)

    fieldnames = [
        "sample_id", "timestamp_unix", "timestamp_ms", "scenario", "relay_state",
        "adc_raw", "adc_voltage_V", "gas_voltage_V", "bus_voltage_V", "current_mA",
        "power_mW", "expected_plausibility", "expected_fingerprint_status"
    ]

    with open(csv_path, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(final_rows)

    print(f"Dataset generated with {len(final_rows)} rows at {csv_path}")

if __name__ == "__main__":
    generate_dataset()
