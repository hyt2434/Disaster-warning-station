from pydantic import BaseModel


class HealthResponse(BaseModel):
    backend: str
    database: str
    mqtt: str
