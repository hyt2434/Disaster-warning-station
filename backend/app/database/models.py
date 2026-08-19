from datetime import datetime, timezone

from sqlalchemy import Boolean, DateTime, Float, Index, String
from sqlalchemy.orm import Mapped, mapped_column

from .connection import Base


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


class SensorReading(Base):
    __tablename__ = "sensor_readings"
    __table_args__ = (
        Index("ix_sensor_readings_device_recorded", "device_id", "recorded_at"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    device_id: Mapped[str] = mapped_column(String(100), index=True)
    temperature: Mapped[float | None] = mapped_column(Float, nullable=True)
    humidity: Mapped[float | None] = mapped_column(Float, nullable=True)
    gas_average: Mapped[float | None] = mapped_column(Float, nullable=True)
    distance_cm: Mapped[float | None] = mapped_column(Float, nullable=True)
    water_level_cm: Mapped[float | None] = mapped_column(Float, nullable=True)
    f7_roll: Mapped[float | None] = mapped_column(Float, nullable=True)
    f7_pitch: Mapped[float | None] = mapped_column(Float, nullable=True)
    f7_tilt: Mapped[float | None] = mapped_column(Float, nullable=True)
    f7_vibration: Mapped[float | None] = mapped_column(Float, nullable=True)
    f7_impact: Mapped[float | None] = mapped_column(Float, nullable=True)
    f7_status: Mapped[str | None] = mapped_column(String(30), nullable=True)
    status: Mapped[str] = mapped_column(String(30), default="SAFE")
    buzzer: Mapped[bool | None] = mapped_column(Boolean, nullable=True)
    buzzer_muted: Mapped[bool | None] = mapped_column(Boolean, nullable=True)
    recorded_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utc_now, index=True
    )
