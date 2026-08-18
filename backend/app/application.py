import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy.exc import SQLAlchemyError

from .api import health_router, readings_router
from .config import settings
from .database import create_tables
from .database.mongodb import mongo_store
from .mqtt import mqtt_client


logger = logging.getLogger(__name__)


@asynccontextmanager
async def lifespan(_: FastAPI):
    try:
        create_tables()
    except SQLAlchemyError as error:
        logger.warning("Database is not ready: %s", error)

    mongo_store.connect()
    mqtt_client.connect()
    logger.info("Backend started; MQTT connection is running in the background.")

    try:
        yield
    finally:
        mqtt_client.disconnect()
        mongo_store.close()


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
