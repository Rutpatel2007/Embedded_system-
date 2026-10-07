from fastapi.testclient import TestClient

from app.main import app


client = TestClient(app)


def valid_payload():
    return {
        "device_id": "TS001",
        "name": "Factory Exhaust Node 1",
        "enrolled_fingerprint": [1.0] * 20,
    }


def test_register_device():
    response = client.post("/api/v1/devices/register", json=valid_payload())

    assert response.status_code == 201

    data = response.json()

    assert data["device_id"] == "TS001"
    
    assert data["status"] == "success"
    assert data["message"] == "Device registered successfully"


def test_invalid_device_id():
    payload = valid_payload()
    payload["device_id"] = "INVALID"

    response = client.post("/api/v1/devices/register", json=payload)

    assert response.status_code == 422


def test_short_fingerprint():
    payload = valid_payload()
    payload["enrolled_fingerprint"] = [1.0] * 19

    response = client.post("/api/v1/devices/register", json=payload)

    assert response.status_code == 422


def test_long_fingerprint():
    payload = valid_payload()
    payload["enrolled_fingerprint"] = [1.0] * 21

    response = client.post("/api/v1/devices/register", json=payload)

    assert response.status_code == 422


def test_empty_name():
    payload = valid_payload()
    payload["name"] = ""

    response = client.post("/api/v1/devices/register", json=payload)

    assert response.status_code == 422


def test_duplicate_device_id():
    payload = valid_payload()

    first_response = client.post("/api/v1/devices/register", json=payload)
    second_response = client.post("/api/v1/devices/register", json=payload)

    assert first_response.status_code == 201
    assert second_response.status_code == 409
    assert second_response.json()["detail"] == "Device already registered"


def test_device_persisted_in_database():
    payload = valid_payload()

    response = client.post("/api/v1/devices/register", json=payload)

    assert response.status_code == 201