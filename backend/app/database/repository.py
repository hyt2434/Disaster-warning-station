from datetime import datetime, timezone

from sqlalchemy import select
from sqlalchemy.orm import Session

from ..schemas import SensorReadingCreate
from .models import Device, SensorReading


def list_readings(database: Session, limit: int) -> list[SensorReading]:
    statement = select(SensorReading).order_by(SensorReading.recorded_at.desc()).limit(limit)
    return list(database.scalars(statement))


def get_latest_reading(database: Session) -> SensorReading | None:
    statement = select(SensorReading).order_by(SensorReading.recorded_at.desc()).limit(1)
    return database.scalar(statement)


def create_reading(database: Session, payload: SensorReadingCreate) -> SensorReading:
    now = datetime.now(timezone.utc)
    device = database.scalar(select(Device).where(Device.device_id == payload.device_id))

    if device is None:
        device = Device(
            device_id=payload.device_id,
            device_name="Main Station",
            device_type="main_station",
            online=True,
            last_seen=now,
        )
        database.add(device)
    else:
        device.online = True
        device.last_seen = now

    reading = SensorReading(
        device_id=payload.device_id,
        temperature=payload.temperature,
        humidity=payload.humidity,
        gas_raw=payload.gas_raw,
        water_level_cm=payload.water_level_cm,
        status=payload.status.upper(),
        recorded_at=payload.recorded_at or now,
    )
    database.add(reading)
    database.commit()
    database.refresh(reading)
    return reading
