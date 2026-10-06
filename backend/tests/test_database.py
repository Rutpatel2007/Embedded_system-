import pytest
from sqlalchemy import create_engine, event
from sqlalchemy.orm import sessionmaker
from sqlalchemy.engine import Engine
from sqlalchemy.exc import IntegrityError
from decimal import Decimal
from app.database import Base
from app.models.db_models import Device, Reading, PlausibilityEnum, FingerprintStatusEnum

# Use in-memory SQLite for testing to avoid leaving files
SQLALCHEMY_DATABASE_URL = "sqlite:///:memory:"

engine = create_engine(
    SQLALCHEMY_DATABASE_URL, connect_args={"check_same_thread": False}
)

# Enforce foreign keys for SQLite
@event.listens_for(Engine, "connect")
def set_sqlite_pragma(dbapi_connection, connection_record):
    cursor = dbapi_connection.cursor()
    cursor.execute("PRAGMA foreign_keys=ON")
    cursor.close()

TestingSessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)

@pytest.fixture(scope="function")
def db():
    Base.metadata.create_all(bind=engine)
    session = TestingSessionLocal()
    yield session
    session.close()
    Base.metadata.drop_all(bind=engine)

def test_create_and_retrieve_device_and_reading(db):
    # Test Device insertion
    fingerprint = [float(i) for i in range(20)]
    device = Device(
        device_id="TS001",
        name="Test Node",
        enrolled_fingerprint=fingerprint
    )
    db.add(device)
    db.commit()
    db.refresh(device)
    
    assert device.device_id == "TS001"
    assert device.name == "Test Node"
    assert len(device.enrolled_fingerprint) == 20
    assert device.enrolled_fingerprint == fingerprint

    # Test Reading insertion
    reading = Reading(
        device_id="TS001",
        timestamp=1750000000,
        gas_ppm=Decimal("42.381"),
        power_mW=Decimal("823.420"),
        plausibility=PlausibilityEnum.NORMAL,
        fingerprint_status=FingerprintStatusEnum.SENSOR_OK,
        previous_hash="0000000000000000000000000000000000000000000000000000000000000000",
        hash="3e7cb4f09d8aa0e71691168f187a5a3a2e379bc83f0631bfaeb24b2165215d2a"
    )
    db.add(reading)
    db.commit()
    db.refresh(reading)
    
    # Verify Reading canonical fields
    saved = db.query(Reading).filter_by(hash="3e7cb4f09d8aa0e71691168f187a5a3a2e379bc83f0631bfaeb24b2165215d2a").first()
    assert saved is not None
    assert saved.device_id == "TS001"
    assert saved.timestamp == 1750000000
    assert isinstance(saved.gas_ppm, Decimal)
    assert saved.gas_ppm == Decimal("42.381")
    assert isinstance(saved.power_mW, Decimal)
    assert saved.power_mW == Decimal("823.420")
    assert saved.plausibility == PlausibilityEnum.NORMAL
    assert saved.fingerprint_status == FingerprintStatusEnum.SENSOR_OK
    assert saved.previous_hash == "0000000000000000000000000000000000000000000000000000000000000000"

def test_foreign_key_enforcement(db):
    # Try inserting Reading without a valid Device
    reading = Reading(
        device_id="NONEXISTENT",
        timestamp=1750000000,
        gas_ppm=Decimal("1.111"),
        power_mW=Decimal("2.222"),
        plausibility=PlausibilityEnum.SUSPICIOUS,
        fingerprint_status=FingerprintStatusEnum.SENSOR_IDENTITY_MISMATCH,
        previous_hash="0"*64,
        hash="1"*64
    )
    db.add(reading)
    with pytest.raises(IntegrityError):
        db.commit()

def test_hash_uniqueness(db):
    device = Device(device_id="TS001", name="Test", enrolled_fingerprint=[])
    db.add(device)
    
    reading1 = Reading(
        device_id="TS001", timestamp=1000, gas_ppm=Decimal("1.0"), power_mW=Decimal("1.0"),
        plausibility=PlausibilityEnum.NORMAL, fingerprint_status=FingerprintStatusEnum.SENSOR_OK,
        previous_hash="0"*64, hash="a"*64
    )
    db.add(reading1)
    db.commit()
    
    reading2 = Reading(
        device_id="TS001", timestamp=1001, gas_ppm=Decimal("2.0"), power_mW=Decimal("2.0"),
        plausibility=PlausibilityEnum.NORMAL, fingerprint_status=FingerprintStatusEnum.SENSOR_OK,
        previous_hash="a"*64, hash="a"*64  # Duplicate hash
    )
    db.add(reading2)
    with pytest.raises(IntegrityError):
        db.commit()
