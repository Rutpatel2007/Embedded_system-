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
    client.post("/api/v1/devices/register", json={
        "device_id": device_id,
        "name": "Test Node",
        "enrolled_fingerprint": [1.0] * 20
    })

def ingest_chain(device_id="TS001", count=3, start_time=1000, plaus="NORMAL", fingerprint="SENSOR_OK"):
    readings = []
    prev_hash = GENESIS_HASH
    for i in range(count):
        t = start_time + i * 10
        gas = Decimal("42.000")
        pwr = Decimal("820.000")
        h = calculate_hash(
            device_id=device_id,
            timestamp=t,
            gas_ppm=gas,
            power_mW=pwr,
            plausibility=plaus,
            fingerprint_status=fingerprint,
            previous_hash=prev_hash
        )
        readings.append({
            "device_id": device_id,
            "timestamp": t,
            "gas_ppm": str(gas),
            "power_mW": str(pwr),
            "plausibility": plaus,
            "fingerprint_status": fingerprint,
            "previous_hash": prev_hash,
            "hash": h
        })
        prev_hash = h
    res = client.post("/api/v1/readings/ingest", json={"device_id": device_id, "readings": readings})
    assert res.status_code == 200, res.text

def test_no_alerts_empty_response():
    register_device()
    ingest_chain()
    res = client.get("/api/v1/alerts?device_id=TS001")
    assert res.status_code == 200
    assert res.json() == {"alerts": []}

def test_alerts_can_be_retrieved():
    register_device()
    ingest_chain(plaus="SUSPICIOUS")
    res = client.get("/api/v1/alerts?device_id=TS001")
    assert res.status_code == 200
    alerts = res.json()["alerts"]
    assert len(alerts) == 3
    assert alerts[0]["type"] == "PHYSICAL_PLAUSIBILITY_SUSPICIOUS"

def test_multiple_alerts_returned():
    register_device()
    ingest_chain(plaus="SUSPICIOUS", fingerprint="SENSOR_IDENTITY_MISMATCH")
    res = client.get("/api/v1/alerts?device_id=TS001")
    alerts = res.json()["alerts"]
    assert len(alerts) == 6 # 3 suspicious, 3 fingerprint
    # Check fields match API contract
    alert = alerts[0]
    assert "alert_id" in alert
    assert "device_id" in alert
    assert "timestamp" in alert
    assert "type" in alert
    assert "severity" in alert
    assert "message" in alert

def test_device_id_filter_works_and_excludes_others():
    register_device("TS001")
    register_device("TS002")
    
    ingest_chain("TS001", plaus="SUSPICIOUS")
    ingest_chain("TS002", fingerprint="SENSOR_IDENTITY_MISMATCH")
    
    res1 = client.get("/api/v1/alerts?device_id=TS001")
    alerts1 = res1.json()["alerts"]
    assert len(alerts1) == 3
    assert all(a["device_id"] == "TS001" for a in alerts1)
    
    res2 = client.get("/api/v1/alerts?device_id=TS002")
    alerts2 = res2.json()["alerts"]
    assert len(alerts2) == 3
    assert all(a["device_id"] == "TS002" for a in alerts2)
    
    res_all = client.get("/api/v1/alerts")
    alerts_all = res_all.json()["alerts"]
    assert len(alerts_all) == 6

def test_unknown_device_behavior():
    res = client.get("/api/v1/alerts?device_id=TS999")
    assert res.status_code == 404
    assert res.json()["detail"] == "Device TS999 is not registered"

def test_alert_ordering_deterministic():
    register_device()
    ingest_chain(count=3, plaus="SUSPICIOUS")
    res = client.get("/api/v1/alerts?device_id=TS001")
    alerts = res.json()["alerts"]
    assert len(alerts) == 3
    # Check newest first
    assert alerts[0]["timestamp"] >= alerts[1]["timestamp"]
    assert alerts[1]["timestamp"] >= alerts[2]["timestamp"]

def test_does_not_modify_database_state():
    register_device()
    ingest_chain(count=1, plaus="SUSPICIOUS")
    res1 = client.get("/api/v1/alerts?device_id=TS001")
    res2 = client.get("/api/v1/alerts?device_id=TS001")
    assert res1.json() == res2.json()
