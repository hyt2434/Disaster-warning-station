from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field


class SensorReadingCreate(BaseModel):
    device_id: str = Field(default="main-station-01", min_length=1, max_length=100)
    temperature: float = Field(ge=-40, le=100)
    humidity: float = Field(ge=0, le=100)
    gas_raw: int | None = Field(default=None, ge=0)
    water_level_cm: float | None = Field(default=None, ge=0)
    status: str = Field(default="NORMAL", min_length=1, max_length=30)
    recorded_at: datetime | None = None


class SensorReadingResponse(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    device_id: str
    temperature: float | None
    humidity: float | None
    gas_raw: int | None
    water_level_cm: float | None
    status: str
    recorded_at: datetime
