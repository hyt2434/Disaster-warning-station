from fastapi import APIRouter

from ..database import database_is_available
from ..schemas import HealthResponse


router = APIRouter(prefix="/api", tags=["System"])


@router.get("/health", response_model=HealthResponse)
def health_check() -> HealthResponse:
    return HealthResponse(
        backend="online",
        database="connected" if database_is_available() else "disconnected",
    )
