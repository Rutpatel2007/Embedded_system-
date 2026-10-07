from decimal import Decimal

from app.hash_verifier import (
    calculate_hash,
    serialize_reading,
    verify_hash,
)


GENESIS_HASH = "0" * 64


def test_serialization_uses_exact_three_decimal_places():
    result = serialize_reading(
        device_id="TS001",
        timestamp=1750000000,
        gas_ppm=Decimal("42.381"),
        power_mW=Decimal("823.420"),
        plausibility="NORMAL",
        fingerprint_status="SENSOR_OK",
        previous_hash=GENESIS_HASH,
    )

    assert result == (
        "TS001|1750000000|42.381|823.420|NORMAL|SENSOR_OK|"
        "0000000000000000000000000000000000000000000000000000000000000000"
    )


def test_hash_is_deterministic():
    kwargs = {
        "device_id": "TS001",
        "timestamp": 1750000000,
        "gas_ppm": Decimal("42.381"),
        "power_mW": Decimal("823.420"),
        "plausibility": "NORMAL",
        "fingerprint_status": "SENSOR_OK",
        "previous_hash": GENESIS_HASH,
    }

    first = calculate_hash(**kwargs)
    second = calculate_hash(**kwargs)

    assert first == second


def test_previous_hash_changes_hash():
    first = calculate_hash(
        "TS001",
        1750000000,
        "42.381",
        "823.420",
        "NORMAL",
        "SENSOR_OK",
        GENESIS_HASH,
    )

    second = calculate_hash(
        "TS001",
        1750000000,
        "42.381",
        "823.420",
        "NORMAL",
        "SENSOR_OK",
        "1" * 64,
    )

    assert first != second


def test_hash_is_lowercase_64_character_hex():
    result = calculate_hash(
        "TS001",
        1750000000,
        "42.381",
        "823.420",
        "NORMAL",
        "SENSOR_OK",
        GENESIS_HASH,
    )

    assert len(result) == 64
    assert result == result.lower()
    assert all(character in "0123456789abcdef" for character in result)


def test_calculated_hash_matches_current_serialization():
    serialized = serialize_reading(
        "TS001",
        1750000000,
        "42.381",
        "823.420",
        "NORMAL",
        "SENSOR_OK",
        GENESIS_HASH,
    )

    calculated = calculate_hash(
        "TS001",
        1750000000,
        "42.381",
        "823.420",
        "NORMAL",
        "SENSOR_OK",
        GENESIS_HASH,
    )

    import hashlib

    expected = hashlib.sha256(serialized.encode("utf-8")).hexdigest()

    assert calculated == expected


def test_verify_hash_accepts_correct_hash():
    expected_hash = calculate_hash(
        device_id="TS001",
        timestamp=1750000000,
        gas_ppm="42.381",
        power_mW="823.420",
        plausibility="NORMAL",
        fingerprint_status="SENSOR_OK",
        previous_hash=GENESIS_HASH,
    )

    assert verify_hash(
        device_id="TS001",
        timestamp=1750000000,
        gas_ppm="42.381",
        power_mW="823.420",
        plausibility="NORMAL",
        fingerprint_status="SENSOR_OK",
        previous_hash=GENESIS_HASH,
        expected_hash=expected_hash,
    )


def test_verify_hash_rejects_wrong_hash():
    assert not verify_hash(
        device_id="TS001",
        timestamp=1750000000,
        gas_ppm="42.381",
        power_mW="823.420",
        plausibility="NORMAL",
        fingerprint_status="SENSOR_OK",
        previous_hash=GENESIS_HASH,
        expected_hash="0" * 64,
    )
def test_authoritative_hash_vector():
    expected_hash = "2e8e7995bb9576188874c028488bafb8a5973133134cbb123182b3c79051dcd3"
    result = calculate_hash(
        device_id="TS001",
        timestamp=1750000000,
        gas_ppm="42.381",
        power_mW="823.420",
        plausibility="NORMAL",
        fingerprint_status="SENSOR_OK",
        previous_hash=GENESIS_HASH,
    )
    assert result == expected_hash
