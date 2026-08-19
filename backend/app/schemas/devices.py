from datetime import datetime
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field


class F7TelemetryResponse(BaseModel):
    device_id: str
    roll: float | None = None
    pitch: float | None = None
    tilt: float | None = None
    vibration: float | None = None
    impact: float | None = None
    status: str
    received_at: datetime


class F7ReadingCreate(BaseModel):
    device_id: str = Field(default="f7-station-01", min_length=1, max_length=100)
    roll: float | None = None
    pitch: float | None = None
    tilt: float | None = Field(default=None, ge=0)
    vibration: float | None = Field(default=None, ge=0)
    impact: float | None = Field(default=None, ge=0)
    status: str = Field(default="NORMAL", min_length=1, max_length=30)
    recorded_at: datetime | None = None


class F7ReadingResponse(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    device_id: str
    roll: float | None
    pitch: float | None
    tilt: float | None
    vibration: float | None
    impact: float | None
    status: str
    recorded_at: datetime


class BuzzerCommand(BaseModel):
    state: Literal["ON", "OFF"]


class BuzzerCommandResponse(BaseModel):
    requested_state: str
    message: str
