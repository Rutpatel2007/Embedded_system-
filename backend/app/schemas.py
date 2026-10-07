from decimal import Decimal

from pydantic import BaseModel, Field, field_validator

import re


class DeviceRegisterRequest(BaseModel):
    device_id: str
    name: str = Field(min_length=1)
    enrolled_fingerprint: list[float] = Field(min_length=20, max_length=20)

    @field_validator("device_id")
    @classmethod
    def validate_device_id(cls, value: str) -> str:
        if not re.fullmatch(r"TS\d{3}", value):
            raise ValueError("device_id must match format TS###")
        return value


class DeviceRegisterResponse(BaseModel):
    status: str
    device_id: str
    message: str


class ReadingIngestItem(BaseModel):
    device_id: str
    timestamp: int
    gas_ppm: Decimal
    power_mW: Decimal
    plausibility: str
    fingerprint_status: str
    previous_hash: str = Field(min_length=64, max_length=64)
    hash: str = Field(min_length=64, max_length=64)

    @field_validator("device_id")
    @classmethod
    def validate_device_id(cls, value: str) -> str:
        if not re.fullmatch(r"TS\d{3}", value):
            raise ValueError("device_id must match format TS###")
        return value

    @field_validator("previous_hash", "hash")
    @classmethod
    def validate_hash_format(cls, value: str) -> str:
        if not re.fullmatch(r"[0-9a-fA-F]{64}", value):
            raise ValueError("hash must be exactly 64 hexadecimal characters")
        return value.lower()

    @field_validator("plausibility")
    @classmethod
    def validate_plausibility(cls, value: str) -> str:
        allowed = {"NORMAL", "PLAUSIBLE", "SUSPICIOUS"}
        if value not in allowed:
            raise ValueError("invalid plausibility value")
        return value

    @field_validator("fingerprint_status")
    @classmethod
    def validate_fingerprint_status(cls, value: str) -> str:
        allowed = {"SENSOR_OK", "SENSOR_IDENTITY_MISMATCH"}
        if value not in allowed:
            raise ValueError("invalid fingerprint_status value")
        return value


class ReadingIngestRequest(BaseModel):
    device_id: str
    readings: list[ReadingIngestItem] = Field(min_length=1)


class ReadingIngestResponse(BaseModel):
    status: str
    accepted_count: int
    last_synced_timestamp: int
class HistoricalReadingItem(BaseModel):
    device_id: str
    timestamp: int
    gas_ppm: float
    power_mW: float
    plausibility: str
    fingerprint_status: str
    previous_hash: str = Field(min_length=64, max_length=64)
    hash: str = Field(min_length=64, max_length=64)
    is_valid_hash: bool
    is_valid_chain: bool

class HistoricalReadingsResponse(BaseModel):
    device_id: str
    total_records: int
    readings: list[HistoricalReadingItem]
class ChainVerificationResponse(BaseModel):
    device_id: str
    chain_valid: bool
    total_records_checked: int
    tampered_records: int
    chain_breaks: int

class AlertItem(BaseModel):
    alert_id: int
    device_id: str
    timestamp: int
    type: str
    severity: str
    message: str

class AlertsResponse(BaseModel):
    alerts: list[AlertItem]
