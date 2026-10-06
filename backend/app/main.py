from fastapi import Depends, FastAPI, HTTPException, status
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session

from app.database import get_db
from app.models import Device
from app.schemas import DeviceRegisterRequest, DeviceRegisterResponse

app = FastAPI(title="TrueSense Backend")


@app.get("/health")
def health_check():
    return {"status": "healthy"}


@app.post(
    "/devices/register",
    response_model=DeviceRegisterResponse,
    status_code=status.HTTP_201_CREATED,
)
def register_device(
    device_data: DeviceRegisterRequest,
    db: Session = Depends(get_db),
):
    existing_device = (
        db.query(Device)
        .filter(Device.device_id == device_data.device_id)
        .first()
    )

    if existing_device is not None:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="Device already registered",
        )

    device = Device(
        device_id=device_data.device_id,
        name=device_data.name,
        enrolled_fingerprint=device_data.enrolled_fingerprint,
    )

    db.add(device)

    try:
        db.commit()
        db.refresh(device)
    except IntegrityError:
        db.rollback()
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="Device already registered",
        )

    return device
