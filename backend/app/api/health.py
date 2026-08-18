from fastapi import APIRouter

from ..database import database_is_available
from ..database.mongodb import mongo_store
from ..schemas import HealthResponse
from ..mqtt import mqtt_client

router = APIRouter(prefix="/api", tags=["System"])


@router.get("/health", response_model=HealthResponse)
def health_check() -> HealthResponse:
    return HealthResponse(
        backend="online",
        database="connected" if database_is_available() else "disconnected",
        mqtt="connected" if mqtt_client.is_connected else "disconnected",
        mongodb=mongo_store.status,
        mongodb_pending=mongo_store.pending_count,
    )
