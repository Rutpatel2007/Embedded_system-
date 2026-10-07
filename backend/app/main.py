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
    ChainVerificationResponse,
    AlertsResponse,
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

    records = query.order_by(Reading.id.desc()).limit(limit + 1).all()
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
                .filter(Reading.id < rec.id)
                .order_by(Reading.id.desc())
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


@app.get(
    "/verify/{device_id}",
    response_model=ChainVerificationResponse,
    status_code=status.HTTP_200_OK,
)
def verify_chain(device_id: str, db: Session = Depends(get_db)):
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

    readings = (
        db.query(Reading)
        .filter(Reading.device_id == device_id)
        .order_by(Reading.id.asc())
        .all()
    )

    tampered_records = 0
    chain_breaks = 0
    expected_previous_hash = "0" * 64

    for record in readings:
        is_valid = verify_hash(
            device_id=record.device_id,
            timestamp=record.timestamp,
            gas_ppm=record.gas_ppm,
            power_mW=record.power_mW,
            plausibility=record.plausibility.value,
            fingerprint_status=record.fingerprint_status.value,
            previous_hash=record.previous_hash,
            expected_hash=record.hash,
        )

        if not is_valid:
            tampered_records += 1

        if record.previous_hash != expected_previous_hash:
            chain_breaks += 1

        expected_previous_hash = record.hash

    chain_valid = (tampered_records == 0) and (chain_breaks == 0)

    return ChainVerificationResponse(
        device_id=device_id,
        chain_valid=chain_valid,
        total_records_checked=len(readings),
        tampered_records=tampered_records,
        chain_breaks=chain_breaks,
    )


@app.get(
    "/alerts",
    response_model=AlertsResponse,
    status_code=status.HTTP_200_OK,
)
def get_alerts(device_id: Optional[str] = None, db: Session = Depends(get_db)):
    devices = []
    if device_id:
        device = db.query(Device).filter(Device.device_id == device_id).first()
        if not device:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"Device {device_id} is not registered",
            )
        devices.append(device_id)
    else:
        all_devices = db.query(Device).all()
        devices = [d.device_id for d in all_devices]
        
    alerts = []
    alert_counter = 1
    
    for dev_id in devices:
        readings = (
            db.query(Reading)
            .filter(Reading.device_id == dev_id)
            .order_by(Reading.id.asc())
            .all()
        )
        expected_prev_hash = "0" * 64
        
        for rec in readings:
            # Physical Plausibility Alert
            if rec.plausibility == PlausibilityEnum.SUSPICIOUS:
                alerts.append({
                    "alert_id": alert_counter,
                    "device_id": dev_id,
                    "timestamp": rec.timestamp,
                    "type": "PHYSICAL_PLAUSIBILITY_SUSPICIOUS",
                    "severity": "HIGH",
                    "message": f"Gas spike detected ({rec.gas_ppm} PPM) with no corresponding equipment power activity ({rec.power_mW} mW)."
                })
                alert_counter += 1
                
            # Fingerprint Mismatch Alert
            if rec.fingerprint_status == FingerprintStatusEnum.SENSOR_IDENTITY_MISMATCH:
                alerts.append({
                    "alert_id": alert_counter,
                    "device_id": dev_id,
                    "timestamp": rec.timestamp,
                    "type": "SENSOR_IDENTITY_MISMATCH",
                    "severity": "CRITICAL",
                    "message": "Warm-up curve similarity fell below threshold. Possible unauthorized sensor replacement."
                })
                alert_counter += 1
                
            # Data Tampering (Hash mismatch)
            is_valid = verify_hash(
                device_id=rec.device_id,
                timestamp=rec.timestamp,
                gas_ppm=rec.gas_ppm,
                power_mW=rec.power_mW,
                plausibility=rec.plausibility.value,
                fingerprint_status=rec.fingerprint_status.value,
                previous_hash=rec.previous_hash,
                expected_hash=rec.hash,
            )
            if not is_valid:
                alerts.append({
                    "alert_id": alert_counter,
                    "device_id": dev_id,
                    "timestamp": rec.timestamp,
                    "type": "DATA_TAMPERING",
                    "severity": "CRITICAL",
                    "message": "Data tampering detected: invalid record hash."
                })
                alert_counter += 1
                
            # Data Tampering (Chain break)
            if rec.previous_hash != expected_prev_hash:
                alerts.append({
                    "alert_id": alert_counter,
                    "device_id": dev_id,
                    "timestamp": rec.timestamp,
                    "type": "DATA_TAMPERING",
                    "severity": "CRITICAL",
                    "message": "Data tampering detected: hash chain break."
                })
                alert_counter += 1
                
            expected_prev_hash = rec.hash
            
    # Deterministic newest-first ordering
    alerts.sort(key=lambda x: (-x["timestamp"], -x["alert_id"]))
    
    return AlertsResponse(alerts=alerts)
