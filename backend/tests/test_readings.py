from decimal import Decimal

from fastapi.testclient import TestClient

from app.hash_verifier import calculate_hash
from app.main import app


client = TestClient(app)

GENESIS_HASH = "0" * 64


def register_test_device():
    response = client.post(
        "/devices/register",
        json={
            "device_id": "TS001",
            "name": "Test Device",
            "enrolled_fingerprint": [1.0] * 20,
        },
    )

    assert response.status_code == 201


def build_reading(previous_hash=GENESIS_HASH, timestamp=1750000000):
    gas_ppm = Decimal("42.381")
    power_mW = Decimal("823.420")

    record_hash = calculate_hash(
        device_id="TS001",
        timestamp=timestamp,
        gas_ppm=gas_ppm,
        power_mW=power_mW,
        plausibility="NORMAL",
        fingerprint_status="SENSOR_OK",
        previous_hash=previous_hash,
    )

    return {
        "device_id": "TS001",
        "timestamp": timestamp,
        "gas_ppm": 42.381,
        "power_mW": 823.420,
        "plausibility": "NORMAL",
        "fingerprint_status": "SENSOR_OK",
        "previous_hash": previous_hash,
        "hash": record_hash,
    }


def test_ingest_valid_reading():
    register_test_device()

    reading = build_reading()

    response = client.post(
        "/readings/ingest",
        json={
            "device_id": "TS001",
            "readings": [reading],
        },
    )

    assert response.status_code == 200
    assert response.json() == {
        "status": "success",
        "accepted_count": 1,
        "last_synced_timestamp": 1750000000,
    }


def test_ingest_rejects_unknown_device():
    reading = build_reading()

    response = client.post(
        "/readings/ingest",
        json={
            "device_id": "TS001",
            "readings": [reading],
        },
    )

    assert response.status_code == 404


def test_ingest_rejects_invalid_hash():
    register_test_device()

    reading = build_reading()
    reading["hash"] = "0" * 64

    response = client.post(
        "/readings/ingest",
        json={
            "device_id": "TS001",
            "readings": [reading],
        },
    )

    assert response.status_code == 422
    assert "Hash verification failed" in response.json()["detail"]


def test_ingest_rejects_chain_break():
    register_test_device()

    first_reading = build_reading()

    first_response = client.post(
        "/readings/ingest",
        json={
            "device_id": "TS001",
            "readings": [first_reading],
        },
    )

    assert first_response.status_code == 200

    broken_reading = build_reading(
        previous_hash="1" * 64,
        timestamp=1750000001,
    )

    response = client.post(
        "/readings/ingest",
        json={
            "device_id": "TS001",
            "readings": [broken_reading],
        },
    )

    assert response.status_code == 422
    assert "continuity" in response.json()["detail"]


def test_ingest_rejects_record_device_id_mismatch():
    register_test_device()

    reading = build_reading()
    reading["device_id"] = "TS002"

    response = client.post(
        "/readings/ingest",
        json={
            "device_id": "TS001",
            "readings": [reading],
        },
    )

    assert response.status_code == 422


def test_ingest_rejects_duplicate_hash():
    register_test_device()

    reading = build_reading()

    first_response = client.post(
        "/readings/ingest",
        json={
            "device_id": "TS001",
            "readings": [reading],
        },
    )

    assert first_response.status_code == 200

    second_response = client.post(
        "/readings/ingest",
        json={
            "device_id": "TS001",
            "readings": [reading],
        },
    )

    assert second_response.status_code == 422