from sqlalchemy import Column, Integer, String, Numeric, BigInteger, Enum, ForeignKey, JSON
from app.database import Base
import enum

class PlausibilityEnum(str, enum.Enum):
    NORMAL = "NORMAL"
    PLAUSIBLE = "PLAUSIBLE"
    SUSPICIOUS = "SUSPICIOUS"

class FingerprintStatusEnum(str, enum.Enum):
    SENSOR_OK = "SENSOR_OK"
    SENSOR_IDENTITY_MISMATCH = "SENSOR_IDENTITY_MISMATCH"

class Device(Base):
    __tablename__ = "devices"

    device_id = Column(String(255), primary_key=True, index=True)
    name = Column(String(255), nullable=False)
    enrolled_fingerprint = Column(JSON, nullable=False)

class Reading(Base):
    __tablename__ = "readings"

    id = Column(Integer, primary_key=True, index=True)
    device_id = Column(String(255), ForeignKey("devices.device_id"), index=True, nullable=False)
    timestamp = Column(BigInteger, index=True, nullable=False)
    gas_ppm = Column(Numeric(precision=10, scale=3, asdecimal=True), nullable=False)
    power_mW = Column(Numeric(precision=10, scale=3, asdecimal=True), nullable=False)
    plausibility = Column(Enum(PlausibilityEnum), nullable=False)
    fingerprint_status = Column(Enum(FingerprintStatusEnum), nullable=False)
    previous_hash = Column(String(64), nullable=False)
    hash = Column(String(64), nullable=False, unique=True)
