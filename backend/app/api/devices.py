from fastapi import APIRouter, Depends, HTTPException, Query, status
from sqlalchemy.exc import SQLAlchemyError
from sqlalchemy.orm import Session

from ..database import get_db
from ..database.repository import list_f7_readings
from ..mqtt import mqtt_client
from ..mqtt.topics import BUZZER_COMMAND_TOPIC
from ..schemas.devices import (
    BuzzerCommand,
    BuzzerCommandResponse,
    F7ReadingResponse,
    F7TelemetryResponse,
)


router = APIRouter(prefix="/api/devices", tags=["Devices"])


@router.get("/f7/latest", response_model=F7TelemetryResponse | None)
def latest_f7_telemetry() -> dict | None:
    return mqtt_client.latest_f7


@router.get("/f7/readings", response_model=list[F7ReadingResponse])
def f7_reading_history(
    limit: int = Query(default=20, ge=1, le=100),
    database: Session = Depends(get_db),
) -> list[F7ReadingResponse]:
    try:
        return list_f7_readings(database, limit)
    except SQLAlchemyError as error:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="Không thể đọc lịch sử F7 từ PostgreSQL.",
        ) from error


@router.post("/main/buzzer", response_model=BuzzerCommandResponse)
def control_main_buzzer(command: BuzzerCommand) -> BuzzerCommandResponse:
    if not mqtt_client.is_connected:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="Backend chưa kết nối MQTT nên không thể gửi lệnh đến ESP32.",
        )

    try:
        mqtt_client.publish(
            topic=BUZZER_COMMAND_TOPIC,
            payload=command.state,
            qos=1,
            retain=False,
        )
    except ConnectionError as error:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail=str(error),
        ) from error

    return BuzzerCommandResponse(
        requested_state=command.state,
        message=f"Đã gửi lệnh {command.state} đến ESP32 Main.",
    )
