import hashlib
from decimal import Decimal


def format_decimal(value: Decimal | float | int | str) -> str:
    return f"{Decimal(str(value)):.3f}"


def serialize_reading(
    device_id: str,
    timestamp: int,
    gas_ppm: Decimal | float | int | str,
    power_mW: Decimal | float | int | str,
    plausibility: str,
    fingerprint_status: str,
    previous_hash: str,
) -> str:
    return "|".join(
        [
            device_id,
            str(timestamp),
            format_decimal(gas_ppm),
            format_decimal(power_mW),
            plausibility,
            fingerprint_status,
            previous_hash,
        ]
    )


def calculate_hash(
    device_id: str,
    timestamp: int,
    gas_ppm: Decimal | float | int | str,
    power_mW: Decimal | float | int | str,
    plausibility: str,
    fingerprint_status: str,
    previous_hash: str,
) -> str:
    serialized = serialize_reading(
        device_id=device_id,
        timestamp=timestamp,
        gas_ppm=gas_ppm,
        power_mW=power_mW,
        plausibility=plausibility,
        fingerprint_status=fingerprint_status,
        previous_hash=previous_hash,
    )

    return hashlib.sha256(serialized.encode("utf-8")).hexdigest()


def verify_hash(
    device_id: str,
    timestamp: int,
    gas_ppm: Decimal | float | int | str,
    power_mW: Decimal | float | int | str,
    plausibility: str,
    fingerprint_status: str,
    previous_hash: str,
    expected_hash: str,
) -> bool:
    calculated_hash = calculate_hash(
        device_id=device_id,
        timestamp=timestamp,
        gas_ppm=gas_ppm,
        power_mW=power_mW,
        plausibility=plausibility,
        fingerprint_status=fingerprint_status,
        previous_hash=previous_hash,
    )

    return calculated_hash == expected_hash.lower()