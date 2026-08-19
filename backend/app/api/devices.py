from fastapi import APIRouter, HTTPException, status

from ..mqtt import mqtt_client
from ..mqtt.topics import BUZZER_COMMAND_TOPIC
from ..schemas.devices import (
    BuzzerCommand,
    BuzzerCommandResponse,
    F7TelemetryResponse,
)


router = APIRouter(prefix="/api/devices", tags=["Devices"])


@router.get("/f7/latest", response_model=F7TelemetryResponse | None)
def latest_f7_telemetry() -> dict | None:
    return mqtt_client.latest_f7


@router.post("/main/buzzer", response_model=BuzzerCommandResponse)
def control_main_buzzer(command: BuzzerCommand) -> BuzzerCommandResponse:
    if not mqtt_client.is_connected:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="Backend chưa kết nối MQTT nên không thể gửi lệnh đến ESP32.",
        )

    if mqtt_client.main_status != "online":
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="ESP32 Main đang offline nên không thể nhận lệnh buzzer.",
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
