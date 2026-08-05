import logging

from fastapi import APIRouter, Depends, HTTPException, Query, status
from sqlalchemy.exc import SQLAlchemyError
from sqlalchemy.orm import Session

from ..database import get_db
from ..database.repository import create_reading, get_latest_reading, list_readings
from ..schemas import SensorReadingCreate, SensorReadingResponse


logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/readings", tags=["Sensor readings"])


def database_error() -> HTTPException:
    return HTTPException(
        status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
        detail="Không thể kết nối PostgreSQL. Hãy kiểm tra DATABASE_URL và trạng thái database.",
    )


@router.get("", response_model=list[SensorReadingResponse])
def readings(
    limit: int = Query(default=20, ge=1, le=100),
    database: Session = Depends(get_db),
) -> list[SensorReadingResponse]:
    try:
        return list_readings(database, limit)
    except SQLAlchemyError as error:
        logger.warning("Cannot list readings: %s", error)
        raise database_error() from error


@router.get("/latest", response_model=SensorReadingResponse | None)
def latest_reading(database: Session = Depends(get_db)) -> SensorReadingResponse | None:
    try:
        return get_latest_reading(database)
    except SQLAlchemyError as error:
        logger.warning("Cannot get latest reading: %s", error)
        raise database_error() from error


@router.post("", response_model=SensorReadingResponse, status_code=status.HTTP_201_CREATED)
def add_reading(
    payload: SensorReadingCreate,
    database: Session = Depends(get_db),
) -> SensorReadingResponse:
    try:
        return create_reading(database, payload)
    except SQLAlchemyError as error:
        database.rollback()
        logger.warning("Cannot save reading: %s", error)
        raise database_error() from error
