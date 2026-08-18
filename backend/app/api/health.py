from fastapi import APIRouter

from ..database import database_is_available
from ..mqtt import mqtt_client
from ..schemas import HealthResponse
from ..services import alert_service, thingspeak_client

router = APIRouter(prefix="/api", tags=["System"])


@router.get("/health", response_model=HealthResponse)
def health_check() -> HealthResponse:
    return HealthResponse(
        backend="online",
        database="connected" if database_is_available() else "disconnected",
        mqtt="connected" if mqtt_client.is_connected else "disconnected",
        thingspeak=thingspeak_client.status,
        pushsafer=alert_service.status,
        main_device=mqtt_client.main_status,
        f7_device=mqtt_client.f7_status,
        system=mqtt_client.system_state,
        buzzer=mqtt_client.buzzer_state,
        buzzer_muted=mqtt_client.buzzer_muted,
        ai=mqtt_client.ai_status,
        ai_prediction=mqtt_client.latest_ai_prediction,
    )
