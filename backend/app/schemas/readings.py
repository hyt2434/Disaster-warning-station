from datetime import datetime
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator


class SensorReadingCreate(BaseModel):
    device_id: str = Field(default="main-station-01", min_length=1, max_length=100)
    temperature: float = Field(ge=-40, le=100)
    humidity: float = Field(ge=0, le=100)
    gas_average: float | None = Field(default=None, ge=0)
    distance_cm: float | None = Field(default=None, ge=0)
    water_level_cm: float | None = Field(default=None, ge=0)
    f7_roll: float | None = None
    f7_pitch: float | None = None
    f7_tilt: float | None = Field(default=None, ge=0)
    f7_vibration: float | None = Field(default=None, ge=0)
    f7_impact: float | None = Field(default=None, ge=0)
    f7_status: Literal["SAFE", "WARNING", "DANGER"] | None = None
    status: Literal["SAFE", "WARNING", "DANGER"] = "SAFE"
    buzzer: bool | None = None
    buzzer_muted: bool | None = None
    recorded_at: datetime | None = None

    @field_validator("f7_status", "status", mode="before")
    @classmethod
    def normalize_status(cls, value: object) -> object:
        return value.upper() if isinstance(value, str) else value


class SensorReadingResponse(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    device_id: str
    temperature: float | None
    humidity: float | None
    gas_average: float | None
    distance_cm: float | None
    water_level_cm: float | None
    f7_roll: float | None
    f7_pitch: float | None
    f7_tilt: float | None
    f7_vibration: float | None
    f7_impact: float | None
    f7_status: str | None
    status: str
    buzzer: bool | None
    buzzer_muted: bool | None
    recorded_at: datetime
