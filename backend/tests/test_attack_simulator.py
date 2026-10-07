import pytest
from decimal import Decimal
from fastapi.testclient import TestClient
from app.main import app
from app.database import get_db
from app.hash_verifier import calculate_hash
from app.attack_simulator import AttackSimulator

client = TestClient(app)
GENESIS_HASH = "0" * 64

def register_and_ingest(device_id="TS001", count=3):
    client.post("/devices/register", json={
        "device_id": device_id,
        "name": "Test Node",
        "enrolled_fingerprint": [1.0] * 20
    })
    readings = []
    prev_hash = GENESIS_HASH
    for i in range(count):
        t = 1000 + i * 10
        gas = Decimal("42.000")
        pwr = Decimal("820.000")
        h = calculate_hash(
            device_id=device_id, timestamp=t, gas_ppm=gas, power_mW=pwr,
            plausibility="NORMAL", fingerprint_status="SENSOR_OK", previous_hash=prev_hash
        )
        readings.append({
            "device_id": device_id, "timestamp": t, "gas_ppm": str(gas), "power_mW": str(pwr),
            "plausibility": "NORMAL", "fingerprint_status": "SENSOR_OK",
            "previous_hash": prev_hash, "hash": h
        })
        prev_hash = h
    client.post("/readings/ingest", json={"device_id": device_id, "readings": readings})

def get_db_session():
    return next(app.dependency_overrides.get(get_db, get_db)())

def test_attack_gas_tampering():
    register_and_ingest("TS001")
    db = get_db_session()
    AttackSimulator.simulate_gas_value_tampering(db, "TS001", Decimal("999.000"))
    
    verify_res = client.get("/verify/TS001").json()
    assert verify_res["chain_valid"] is False
    assert verify_res["tampered_records"] >= 1
    
    alert_res = client.get("/alerts?device_id=TS001").json()
    assert any(a["type"] == "DATA_TAMPERING" for a in alert_res["alerts"])

def test_attack_power_tampering():
    register_and_ingest("TS002")
    db = get_db_session()
    AttackSimulator.simulate_power_value_tampering(db, "TS002", Decimal("999.000"))
    
    verify_res = client.get("/verify/TS002").json()
    assert verify_res["chain_valid"] is False
    assert verify_res["tampered_records"] >= 1
    
    alert_res = client.get("/alerts?device_id=TS002").json()
    assert any(a["type"] == "DATA_TAMPERING" for a in alert_res["alerts"])

def test_attack_hash_tampering():
    register_and_ingest("TS003")
    db = get_db_session()
    AttackSimulator.simulate_hash_tampering(db, "TS003", "a" * 64)
    
    verify_res = client.get("/verify/TS003").json()
    assert verify_res["chain_valid"] is False
    assert verify_res["tampered_records"] >= 1
    
    alert_res = client.get("/alerts?device_id=TS003").json()
    assert any(a["type"] == "DATA_TAMPERING" for a in alert_res["alerts"])

def test_attack_previous_hash_break():
    register_and_ingest("TS004")
    db = get_db_session()
    AttackSimulator.simulate_previous_hash_break(db, "TS004", "b" * 64, offset=1)
    
    verify_res = client.get("/verify/TS004").json()
    assert verify_res["chain_valid"] is False
    assert verify_res["chain_breaks"] >= 1
    
    alert_res = client.get("/alerts?device_id=TS004").json()
    assert any(a["type"] == "DATA_TAMPERING" for a in alert_res["alerts"])

def test_attack_first_record_non_genesis():
    register_and_ingest("TS005", count=1)
    db = get_db_session()
    AttackSimulator.simulate_first_record_non_genesis(db, "TS005", "c" * 64)
    
    verify_res = client.get("/verify/TS005").json()
    assert verify_res["chain_valid"] is False
    assert verify_res["chain_breaks"] >= 1
    
    alert_res = client.get("/alerts?device_id=TS005").json()
    assert any(a["type"] == "DATA_TAMPERING" for a in alert_res["alerts"])
