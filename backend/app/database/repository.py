from datetime import datetime, timezone

from sqlalchemy import select
from sqlalchemy.orm import Session

from ..schemas import SensorReadingCreate
from .models import SensorReading


def list_readings(database: Session, limit: int) -> list[SensorReading]:
    statement = select(SensorReading).order_by(SensorReading.recorded_at.desc()).limit(limit)
    return list(database.scalars(statement))


def get_latest_reading(database: Session) -> SensorReading | None:
    statement = select(SensorReading).order_by(SensorReading.recorded_at.desc()).limit(1)
    return database.scalar(statement)


def create_reading(database: Session, payload: SensorReadingCreate) -> SensorReading:
    now = datetime.now(timezone.utc)
    reading = SensorReading(
        device_id=payload.device_id,
        temperature=payload.temperature,
        humidity=payload.humidity,
        gas_average=payload.gas_average,
        distance_cm=payload.distance_cm,
        water_level_cm=payload.water_level_cm,
        f7_roll=payload.f7_roll,
        f7_pitch=payload.f7_pitch,
        f7_tilt=payload.f7_tilt,
        f7_vibration=payload.f7_vibration,
        f7_impact=payload.f7_impact,
        f7_status=payload.f7_status.upper() if payload.f7_status else None,
        status=payload.status.upper(),
        buzzer=payload.buzzer,
        buzzer_muted=payload.buzzer_muted,
        recorded_at=payload.recorded_at or now,
    )
    database.add(reading)
    database.commit()
    database.refresh(reading)
    return reading
