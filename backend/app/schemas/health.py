from pydantic import BaseModel


class HealthResponse(BaseModel):
    backend: str
    database: str
    mqtt: str
    mongodb: str
    mongodb_pending: int
    main_device: str
    f7_device: str
    buzzer: str
    ai: str
    ai_prediction: str
