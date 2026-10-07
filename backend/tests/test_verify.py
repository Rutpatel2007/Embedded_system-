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

def ingest_chain(device_id="TS001", count=5, start_time=1000, same_timestamp=False):
    readings = []
    prev_hash = GENESIS_HASH
    for i in range(count):
        t = start_time if same_timestamp else start_time + i * 10
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
    res = client.get("/verify/TS999")
    assert res.status_code == 404

def test_empty_device_behavior():
    register_device()
    res = client.get("/verify/TS001")
    assert res.status_code == 200
    data = res.json()
    assert data["chain_valid"] is True
    assert data["total_records_checked"] == 0
    assert data["tampered_records"] == 0
    assert data["chain_breaks"] == 0

def test_valid_single_record_genesis_chain():
    register_device()
    ingest_chain(count=1)
    res = client.get("/verify/TS001")
    data = res.json()
    assert data["chain_valid"] is True
    assert data["total_records_checked"] == 1
    assert data["tampered_records"] == 0
    assert data["chain_breaks"] == 0

def test_valid_multi_record_chain():
    register_device()
    ingest_chain(count=5)
    res = client.get("/verify/TS001")
    data = res.json()
    assert data["chain_valid"] is True
    assert data["total_records_checked"] == 5

def test_same_timestamp_multi_record_chain():
    register_device()
    ingest_chain(count=5, same_timestamp=True)
    res = client.get("/verify/TS001")
    data = res.json()
    assert data["chain_valid"] is True
    assert data["total_records_checked"] == 5

def test_tampered_stored_hash_detected():
    register_device()
    ingest_chain(count=3)
    db = next(app.dependency_overrides.get(get_db, get_db)())
    r = db.query(Reading).order_by(Reading.id.asc()).offset(1).first()
    r.hash = "f" * 64
    db.commit()
    
    res = client.get("/verify/TS001")
    data = res.json()
    assert data["chain_valid"] is False
    assert data["tampered_records"] == 1
    assert data["chain_breaks"] == 1 # Because the next record's previous_hash won't match "f"*64

def test_tampered_gas_value_detected():
    register_device()
    ingest_chain(count=3)
    db = next(app.dependency_overrides.get(get_db, get_db)())
    r = db.query(Reading).order_by(Reading.id.asc()).offset(1).first()
    r.gas_ppm = Decimal("999.000")
    db.commit()
    
    res = client.get("/verify/TS001")
    data = res.json()
    assert data["chain_valid"] is False
    assert data["tampered_records"] == 1
    assert data["chain_breaks"] == 0

def test_tampered_power_value_detected():
    register_device()
    ingest_chain(count=3)
    db = next(app.dependency_overrides.get(get_db, get_db)())
    r = db.query(Reading).order_by(Reading.id.asc()).offset(1).first()
    r.power_mW = Decimal("999.000")
    db.commit()
    
    res = client.get("/verify/TS001")
    data = res.json()
    assert data["chain_valid"] is False
    assert data["tampered_records"] == 1

def test_broken_previous_hash_linkage_detected():
    register_device()
    ingest_chain(count=3)
    db = next(app.dependency_overrides.get(get_db, get_db)())
    r = db.query(Reading).order_by(Reading.id.asc()).offset(1).first()
    r.previous_hash = "a" * 64
    db.commit()
    
    res = client.get("/verify/TS001")
    data = res.json()
    assert data["chain_valid"] is False
    assert data["tampered_records"] == 1 # the hash of the record itself is now invalid because its contents (previous_hash) changed
    assert data["chain_breaks"] == 1

def test_first_record_non_genesis():
    register_device()
    ingest_chain(count=1)
    db = next(app.dependency_overrides.get(get_db, get_db)())
    r = db.query(Reading).order_by(Reading.id.asc()).first()
    r.previous_hash = "1" * 64
    db.commit()
    
    res = client.get("/verify/TS001")
    data = res.json()
    assert data["chain_valid"] is False
    assert data["tampered_records"] == 1
    assert data["chain_breaks"] == 1

def test_multiple_simultaneous_tampering():
    register_device()
    ingest_chain(count=5)
    db = next(app.dependency_overrides.get(get_db, get_db)())
    
    # Break hash of record 2 (index 1)
    r2 = db.query(Reading).order_by(Reading.id.asc()).offset(1).first()
    r2.gas_ppm = Decimal("999.000")
    
    # Break linkage of record 4 (index 3)
    r4 = db.query(Reading).order_by(Reading.id.asc()).offset(3).first()
    r4.previous_hash = "2" * 64
    db.commit()
    
    res = client.get("/verify/TS001")
    data = res.json()
    assert data["chain_valid"] is False
    assert data["total_records_checked"] == 5
    assert data["tampered_records"] == 2 # r2 (gas), r4 (prev_hash)
    assert data["chain_breaks"] == 1 # r4 prev_hash != r3 hash

def test_endpoint_is_read_only():
    register_device()
    ingest_chain(count=3)
    
    res1 = client.get("/verify/TS001")
    res2 = client.get("/verify/TS001")
    
    assert res1.json() == res2.json()
