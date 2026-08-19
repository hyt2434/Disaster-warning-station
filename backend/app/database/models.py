from datetime import datetime, timezone

from sqlalchemy import Boolean, DateTime, Float, ForeignKey, Index, Integer, String
from sqlalchemy.orm import Mapped, mapped_column

from .connection import Base


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


class Device(Base):
    __tablename__ = "devices"

    id: Mapped[int] = mapped_column(primary_key=True)
    device_id: Mapped[str] = mapped_column(String(100), unique=True, index=True)
    device_name: Mapped[str] = mapped_column(String(150))
    device_type: Mapped[str] = mapped_column(String(50), default="main_station")
    online: Mapped[bool] = mapped_column(Boolean, default=True)
    last_seen: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utc_now)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utc_now)


class SensorReading(Base):
    __tablename__ = "sensor_readings"
    __table_args__ = (
        Index("ix_sensor_readings_device_recorded", "device_id", "recorded_at"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    device_id: Mapped[str] = mapped_column(
        String(100), ForeignKey("devices.device_id", ondelete="CASCADE"), index=True
    )
    temperature: Mapped[float | None] = mapped_column(Float, nullable=True)
    humidity: Mapped[float | None] = mapped_column(Float, nullable=True)
    gas_raw: Mapped[int | None] = mapped_column(Integer, nullable=True)
    gas_filtered: Mapped[float | None] = mapped_column(Float, nullable=True)
    distance_cm: Mapped[float | None] = mapped_column(Float, nullable=True)
    water_level_cm: Mapped[float | None] = mapped_column(Float, nullable=True)
    water_level_percent: Mapped[float | None] = mapped_column(Float, nullable=True)
    f7_roll: Mapped[float | None] = mapped_column(Float, nullable=True)
    f7_pitch: Mapped[float | None] = mapped_column(Float, nullable=True)
    f7_tilt: Mapped[float | None] = mapped_column(Float, nullable=True)
    f7_vibration: Mapped[float | None] = mapped_column(Float, nullable=True)
    f7_impact: Mapped[float | None] = mapped_column(Float, nullable=True)
    f7_status: Mapped[str | None] = mapped_column(String(30), nullable=True)
    status: Mapped[str] = mapped_column(String(30), default="NORMAL")
    buzzer: Mapped[bool | None] = mapped_column(Boolean, nullable=True)
    buzzer_muted: Mapped[bool | None] = mapped_column(Boolean, nullable=True)
    recorded_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utc_now, index=True
    )
