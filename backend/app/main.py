import logging
import sys
from contextlib import asynccontextmanager
from pathlib import Path

import uvicorn
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy.exc import SQLAlchemyError

# Hỗ trợ cả hai cách chạy:
# - từ backend/app: python main.py
# - từ backend: python -m app.main
if __package__ in (None, ""):
    backend_directory = Path(__file__).resolve().parent.parent
    if str(backend_directory) not in sys.path:
        sys.path.insert(0, str(backend_directory))

    from app.api import health_router, readings_router
    from app.config import settings
    from app.database import create_tables
    from app.mqtt import mqtt_client

    uvicorn_target = "main:app"
else:
    from .api import health_router, readings_router
    from .config import settings
    from .database import create_tables
    from .mqtt import mqtt_client

    uvicorn_target = "app.main:app"


logger = logging.getLogger(__name__)


@asynccontextmanager
async def lifespan(_: FastAPI):
    try:
        create_tables()
    except SQLAlchemyError as error:
        logger.warning("Database is not ready: %s", error)
    mqtt_client.connect()
    logger.info("🚀 Backend Services (MQTT, ThingSpeak) started successfully!")
    try:
        yield
    finally:
        mqtt_client.disconnect()

app = FastAPI(
    title="Disaster Warning Station API",
    version="0.1.0",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.allowed_origins,
    allow_credentials=True,
    allow_methods=["GET", "POST", "OPTIONS"],
    allow_headers=["*"],
)

app.include_router(health_router)
app.include_router(readings_router)


if __name__ == "__main__":
    uvicorn.run(uvicorn_target, host="127.0.0.1", port=8000, reload=True)
