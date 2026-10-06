from sqlalchemy import create_engine, event
from sqlalchemy.orm import sessionmaker
from fastapi.testclient import TestClient

from app.database import Base, get_db
from app.main import app
from app.models import Device


TEST_DATABASE_URL = "sqlite:///:memory:"

engine = create_engine(
    TEST_DATABASE_URL,
    connect_args={"check_same_thread": False},
)

SessionTesting = sessionmaker(
    autocommit=False,
    autoflush=False,
    bind=engine,
)


@event.listens_for(engine, "connect")
def enable_sqlite_foreign_keys(dbapi_connection, connection_record):
    cursor = dbapi_connection.cursor()
    cursor.execute("PRAGMA foreign_keys=ON")
    cursor.close()


def override_get_db():
    db = SessionTesting()
    try:
        yield db
    finally:
        db.close()


app.dependency_overrides[get_db] = override_get_db

client = TestClient(app)


def setup_function():
    Base.metadata.create_all(bind=engine)


def teardown_function():
    Base.metadata.drop_all(bind=engine)


def valid_payload():
    return {
        "device_id": "TS001",
        "name": "TrueSense Device 001",
        "enrolled_fingerprint": [float(i) for i in range(20)],
    }


def test_successful_device_registration():
    response = client.post("/devices/register", json=valid_payload())

    assert response.status_code == 201

    data = response.json()

    assert data["device_id"] == "TS001"
    assert data["name"] == "TrueSense Device 001"
    assert len(data["enrolled_fingerprint"]) == 20


def test_invalid_device_id():
    payload = valid_payload()
    payload["device_id"] = "INVALID"

    response = client.post("/devices/register", json=payload)

    assert response.status_code == 422


def test_fingerprint_too_short():
    payload = valid_payload()
    payload["enrolled_fingerprint"] = [1.0] * 19

    response = client.post("/devices/register", json=payload)

    assert response.status_code == 422


def test_fingerprint_too_long():
    payload = valid_payload()
    payload["enrolled_fingerprint"] = [1.0] * 21

    response = client.post("/devices/register", json=payload)

    assert response.status_code == 422


def test_duplicate_device_id():
    payload = valid_payload()

    first_response = client.post("/devices/register", json=payload)
    second_response = client.post("/devices/register", json=payload)

    assert first_response.status_code == 201
    assert second_response.status_code == 409


def test_device_persisted_in_database():
    payload = valid_payload()

    response = client.post("/devices/register", json=payload)

    assert response.status_code == 201

    db = SessionTesting()

    try:
        device = (
            db.query(Device)
            .filter(Device.device_id == "TS001")
            .first()
        )

        assert device is not None
        assert device.name == "TrueSense Device 001"
        assert len(device.enrolled_fingerprint) == 20
    finally:
        db.close()
