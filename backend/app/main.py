from contextlib import asynccontextmanager

from typing import Optional
from fastapi import Depends, FastAPI, HTTPException, status, Query
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session

from app.database import Base, engine, get_db
from app.hash_verifier import verify_hash
from app.models import (
    Device,
    FingerprintStatusEnum,
    PlausibilityEnum,
    Reading,
)
from app.schemas import (
    DeviceRegisterRequest,
    DeviceRegisterResponse,
    ReadingIngestRequest,
    ReadingIngestResponse,
    HistoricalReadingsResponse,
)


@asynccontextmanager
async def lifespan(app: FastAPI):
    Base.metadata.create_all(bind=engine)
    yield


app = FastAPI(title="TrueSense Backend", lifespan=lifespan)


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


@app.post(
    "/readings/ingest",
    response_model=ReadingIngestResponse,
    status_code=status.HTTP_200_OK,
)
def ingest_readings(
    ingest_data: ReadingIngestRequest,
    db: Session = Depends(get_db),
):
    device = (
        db.query(Device)
        .filter(Device.device_id == ingest_data.device_id)
        .first()
    )

    if device is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Device {ingest_data.device_id} is not registered",
        )

    last_reading = (
        db.query(Reading)
        .filter(Reading.device_id == ingest_data.device_id)
        .order_by(Reading.id.desc())
        .first()
    )

    expected_previous_hash = (
        last_reading.hash if last_reading is not None else "0" * 64
    )

    for record in ingest_data.readings:
        if record.device_id != ingest_data.device_id:
            raise HTTPException(
                status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
                detail=(
                    f"Device ID mismatch for record at timestamp "
                    f"{record.timestamp}"
                ),
            )

        is_valid = verify_hash(
            device_id=record.device_id,
            timestamp=record.timestamp,
            gas_ppm=record.gas_ppm,
            power_mW=record.power_mW,
            plausibility=record.plausibility,
            fingerprint_status=record.fingerprint_status,
            previous_hash=record.previous_hash,
            expected_hash=record.hash,
        )

        if not is_valid:
            db.rollback()
            raise HTTPException(
                status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
                detail=(
                    "Hash verification failed for record at timestamp "
                    f"{record.timestamp}. Computed hash does not match "
                    "provided hash."
                ),
            )

        if record.previous_hash != expected_previous_hash:
            db.rollback()

            if last_reading is None and expected_previous_hash == "0" * 64:
                detail = (
                    "Hash-chain genesis verification failed for "
                    f"record at timestamp {record.timestamp}."
                )
            else:
                detail = (
                    "Hash-chain continuity verification failed for "
                    f"record at timestamp {record.timestamp}."
                )

            raise HTTPException(
                status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
                detail=detail,
            )

        reading = Reading(
            device_id=record.device_id,
            timestamp=record.timestamp,
            gas_ppm=record.gas_ppm,
            power_mW=record.power_mW,
            plausibility=PlausibilityEnum(record.plausibility),
            fingerprint_status=FingerprintStatusEnum(record.fingerprint_status),
            previous_hash=record.previous_hash,
            hash=record.hash,
        )

        db.add(reading)

        expected_previous_hash = record.hash
        last_reading = reading

    try:
        db.commit()
    except IntegrityError:
        db.rollback()
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail="Reading batch contains a duplicate hash.",
        )

    return ReadingIngestResponse(
        status="success",
        accepted_count=len(ingest_data.readings),
        last_synced_timestamp=ingest_data.readings[-1].timestamp,
    )

@app.get(
    "/readings/{device_id}",
    response_model=HistoricalReadingsResponse,
    status_code=status.HTTP_200_OK,
)
def get_readings(
    device_id: str,
    limit: int = Query(100, ge=1, le=1000),
    start_time: Optional[int] = Query(None),
    end_time: Optional[int] = Query(None),
    db: Session = Depends(get_db),
):
    device = (
        db.query(Device)
        .filter(Device.device_id == device_id)
        .first()
    )

    if device is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Device {device_id} is not registered",
        )

    query = db.query(Reading).filter(Reading.device_id == device_id)

    if start_time is not None:
        query = query.filter(Reading.timestamp >= start_time)
    if end_time is not None:
        query = query.filter(Reading.timestamp <= end_time)

    total_records = query.count()

    records = query.order_by(Reading.timestamp.desc()).limit(limit + 1).all()
    readings_to_return = records[:limit]

    response_readings = []

    for i, rec in enumerate(readings_to_return):
        is_valid_hash = verify_hash(
            device_id=rec.device_id,
            timestamp=rec.timestamp,
            gas_ppm=rec.gas_ppm,
            power_mW=rec.power_mW,
            plausibility=rec.plausibility.value,
            fingerprint_status=rec.fingerprint_status.value,
            previous_hash=rec.previous_hash,
            expected_hash=rec.hash,
        )

        is_valid_chain = False
        if i + 1 < len(records):
            preceding_rec = records[i + 1]
            if rec.previous_hash == preceding_rec.hash:
                is_valid_chain = True
        else:
            preceding_db_rec = (
                db.query(Reading)
                .filter(Reading.device_id == device_id)
                .filter(Reading.timestamp < rec.timestamp)
                .order_by(Reading.timestamp.desc())
                .first()
            )
            if preceding_db_rec is not None:
                if rec.previous_hash == preceding_db_rec.hash:
                    is_valid_chain = True
            else:
                if rec.previous_hash == "0" * 64:
                    is_valid_chain = True

        response_readings.append({
            "device_id": rec.device_id,
            "timestamp": rec.timestamp,
            "gas_ppm": rec.gas_ppm,
            "power_mW": rec.power_mW,
            "plausibility": rec.plausibility.value,
            "fingerprint_status": rec.fingerprint_status.value,
            "previous_hash": rec.previous_hash,
            "hash": rec.hash,
            "is_valid_hash": is_valid_hash,
            "is_valid_chain": is_valid_chain
        })

    return HistoricalReadingsResponse(
        device_id=device_id,
        total_records=total_records,
        readings=response_readings
    )
