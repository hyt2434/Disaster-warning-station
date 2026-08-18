from pydantic import BaseModel


class HealthResponse(BaseModel):
    backend: str
    database: str
    mqtt: str
    mongodb: str
    mongodb_pending: int
