from decimal import Decimal
import pytest
from fastapi.testclient import TestClient
from app.main import app
from app.database import get_db
from app.models import Reading
from app.hash_verifier import calculate_hash

client = TestClient(app)
GENESIS_HASH = "0" * 64

def register_device(device_id="TS001"):
    client.post("/devices/register", json={
        "device_id": device_id,
        "name": "Test Node",
        "enrolled_fingerprint": [1.0] * 20
    })

def ingest_chain(device_id="TS001", count=5, start_time=1000):
    readings = []
    prev_hash = GENESIS_HASH
    for i in range(count):
        t = start_time + i * 10
        gas = Decimal("42.000") + Decimal(i)
        pwr = Decimal("820.000") + Decimal(i)
        h = calculate_hash(
            device_id=device_id,
            timestamp=t,
            gas_ppm=gas,
            power_mW=pwr,
            plausibility="NORMAL",
            fingerprint_status="SENSOR_OK",
            previous_hash=prev_hash
        )
        readings.append({
            "device_id": device_id,
            "timestamp": t,
            "gas_ppm": str(gas),
            "power_mW": str(pwr),
            "plausibility": "NORMAL",
            "fingerprint_status": "SENSOR_OK",
            "previous_hash": prev_hash,
            "hash": h
        })
        prev_hash = h
    res = client.post("/readings/ingest", json={"device_id": device_id, "readings": readings})
    assert res.status_code == 200, res.text
    return readings

def test_unknown_device():
    res = client.get("/readings/TS999")
    assert res.status_code == 404

def test_empty_result_behavior():
    register_device()
    res = client.get("/readings/TS001")
    assert res.status_code == 200
    assert res.json()["total_records"] == 0

def test_successful_retrieval_and_genesis_validation():
    register_device()
    ingest_chain(count=1)
    res = client.get("/readings/TS001")
    r = res.json()["readings"][0]
    assert r["is_valid_hash"] is True
    assert r["is_valid_chain"] is True

def test_default_limit():
    register_device()
    ingest_chain(count=105)
    res = client.get("/readings/TS001")
    assert len(res.json()["readings"]) == 100

def test_custom_limit():
    register_device()
    ingest_chain(count=10)
    res = client.get("/readings/TS001?limit=5")
    assert len(res.json()["readings"]) == 5

def test_maximum_limit():
    register_device()
    ingest_chain(count=10)
    res = client.get("/readings/TS001?limit=1000")
    assert res.status_code == 200

def test_invalid_limit_above_1000():
    register_device()
    res = client.get("/readings/TS001?limit=1001")
    assert res.status_code == 422

def test_start_time_filtering():
    register_device()
    ingest_chain(count=5, start_time=1000)
    res = client.get("/readings/TS001?start_time=1020")
    data = res.json()
    assert data["readings"][-1]["timestamp"] == 1020
    assert data["readings"][-1]["is_valid_chain"] is True

def test_end_time_filtering():
    register_device()
    ingest_chain(count=5, start_time=1000)
    res = client.get("/readings/TS001?end_time=1020")
    data = res.json()
    assert data["readings"][0]["timestamp"] == 1020

def test_combined_time_filtering():
    register_device()
    ingest_chain(count=5, start_time=1000)
    res = client.get("/readings/TS001?start_time=1010&end_time=1030")
    data = res.json()
    assert data["readings"][0]["timestamp"] == 1030
    assert data["readings"][-1]["timestamp"] == 1010

def test_tampered_hash_and_chain():
    register_device()
    ingest_chain(count=3, start_time=1000)
    
    db = next(app.dependency_overrides.get(get_db, get_db)())
    r1010 = db.query(Reading).filter(Reading.timestamp == 1010).first()
    r1010.gas_ppm = Decimal("999.000")
    r1020 = db.query(Reading).filter(Reading.timestamp == 1020).first()
    r1020.previous_hash = "1" + r1020.previous_hash[1:]
    db.commit()
    
    res = client.get("/readings/TS001")
    data = res.json()
    r_1020 = data["readings"][0]
    r_1010 = data["readings"][1]
    r_1000 = data["readings"][2]
    
    assert r_1010["is_valid_hash"] is False
    assert r_1010["is_valid_chain"] is True
    assert r_1020["is_valid_chain"] is False
    assert r_1020["is_valid_hash"] is False
    assert r_1000["is_valid_hash"] is True
    assert r_1000["is_valid_chain"] is True

def test_json_number_types():
    register_device()
    ingest_chain(count=1)
    res = client.get("/readings/TS001")
    r = res.json()["readings"][0]
    assert isinstance(r["gas_ppm"], float) or isinstance(r["gas_ppm"], int)
    assert isinstance(r["power_mW"], float) or isinstance(r["power_mW"], int)

def test_same_timestamp_chain():
    register_device()
    readings = []
    prev_hash = GENESIS_HASH
    for i in range(3):
        t = 1500
        gas = Decimal("42.000") + Decimal(i)
        pwr = Decimal("820.000") + Decimal(i)
        h = calculate_hash("TS001", t, gas, pwr, "NORMAL", "SENSOR_OK", prev_hash)
        readings.append({
            "device_id": "TS001", "timestamp": t, "gas_ppm": str(gas), "power_mW": str(pwr),
            "plausibility": "NORMAL", "fingerprint_status": "SENSOR_OK",
            "previous_hash": prev_hash, "hash": h
        })
        prev_hash = h
    client.post("/readings/ingest", json={"device_id": "TS001", "readings": readings})
    
    res = client.get("/readings/TS001")
    data = res.json()
    assert len(data["readings"]) == 3
    # Check JSON output is numeric and order is descending ID (which means reversed insertion)
    assert data["readings"][0]["gas_ppm"] == 44.0
    assert data["readings"][1]["gas_ppm"] == 43.0
    assert data["readings"][2]["gas_ppm"] == 42.0
    
    for r in data["readings"]:
        assert r["is_valid_chain"] is True

def test_limit_one_valid_chain():
    register_device()
    ingest_chain(count=3)
    res = client.get("/readings/TS001?limit=1")
    data = res.json()
    assert len(data["readings"]) == 1
    assert data["readings"][0]["is_valid_chain"] is True
    assert data["readings"][0]["is_valid_hash"] is True
