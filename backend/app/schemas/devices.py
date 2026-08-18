from datetime import datetime
from typing import Literal

from pydantic import BaseModel


class F7TelemetryResponse(BaseModel):
    device_id: str
    roll: float | None = None
    pitch: float | None = None
    tilt: float | None = None
    vibration: float | None = None
    impact: float | None = None
    status: str
    received_at: datetime


class BuzzerCommand(BaseModel):
    state: Literal["ON", "OFF"]


class BuzzerCommandResponse(BaseModel):
    requested_state: str
    message: str
