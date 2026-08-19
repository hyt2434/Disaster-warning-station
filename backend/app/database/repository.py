from datetime import datetime, timezone

from sqlalchemy import select
from sqlalchemy.orm import Session

from ..schemas import F7ReadingCreate, SensorReadingCreate
from .models import Device, F7Reading, SensorReading


def list_readings(database: Session, limit: int) -> list[SensorReading]:
    statement = select(SensorReading).order_by(SensorReading.recorded_at.desc()).limit(limit)
    return list(database.scalars(statement))


def get_latest_reading(database: Session) -> SensorReading | None:
    statement = select(SensorReading).order_by(SensorReading.recorded_at.desc()).limit(1)
    return database.scalar(statement)


def list_f7_readings(database: Session, limit: int) -> list[F7Reading]:
    statement = select(F7Reading).order_by(F7Reading.recorded_at.desc()).limit(limit)
    return list(database.scalars(statement))


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
        gas_filtered=payload.gas_filtered,
        distance_cm=payload.distance_cm,
        water_level_cm=payload.water_level_cm,
        water_level_percent=payload.water_level_percent,
        angle_x=payload.angle_x,
        angle_y=payload.angle_y,
        vibration=payload.vibration,
        motion_status=payload.motion_status.upper() if payload.motion_status else None,
        status=payload.status.upper(),
        buzzer=payload.buzzer,
        buzzer_muted=payload.buzzer_muted,
        recorded_at=payload.recorded_at or now,
    )
    database.add(reading)
    database.commit()
    database.refresh(reading)
    return reading


def create_f7_reading(database: Session, payload: F7ReadingCreate) -> F7Reading:
    now = datetime.now(timezone.utc)
    device = database.scalar(select(Device).where(Device.device_id == payload.device_id))

    if device is None:
        device = Device(
            device_id=payload.device_id,
            device_name="F7 Motion Station",
            device_type="f7_station",
            online=True,
            last_seen=now,
        )
        database.add(device)
    else:
        device.online = True
        device.last_seen = now

    reading = F7Reading(
        device_id=payload.device_id,
        roll=payload.roll,
        pitch=payload.pitch,
        tilt=payload.tilt,
        vibration=payload.vibration,
        impact=payload.impact,
        status=payload.status.upper(),
        recorded_at=payload.recorded_at or now,
    )
    database.add(reading)
    database.commit()
    database.refresh(reading)
    return reading
