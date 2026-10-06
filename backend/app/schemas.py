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
    device_id: str
    name: str
    enrolled_fingerprint: list[float]
