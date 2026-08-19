from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field


class SensorReadingCreate(BaseModel):
    device_id: str = Field(default="main-station-01", min_length=1, max_length=100)
    temperature: float = Field(ge=-40, le=100)
    humidity: float = Field(ge=0, le=100)
    gas_raw: int | None = Field(default=None, ge=0)
    gas_filtered: float | None = Field(default=None, ge=0)
    distance_cm: float | None = Field(default=None, ge=0)
    water_level_cm: float | None = Field(default=None, ge=0)
    water_level_percent: float | None = Field(default=None, ge=0, le=100)
    angle_x: float | None = None
    angle_y: float | None = None
    vibration: float | None = Field(default=None, ge=0)
    motion_status: str | None = Field(default=None, max_length=30)
    status: str = Field(default="NORMAL", min_length=1, max_length=30)
    buzzer: bool | None = None
    buzzer_muted: bool | None = None
    recorded_at: datetime | None = None


class SensorReadingResponse(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    device_id: str
    temperature: float | None
    humidity: float | None
    gas_raw: int | None
    gas_filtered: float | None
    distance_cm: float | None
    water_level_cm: float | None
    water_level_percent: float | None
    angle_x: float | None
    angle_y: float | None
    vibration: float | None
    motion_status: str | None
    status: str
    buzzer: bool | None
    buzzer_muted: bool | None
    recorded_at: datetime
