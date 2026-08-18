from pydantic import BaseModel


class HealthResponse(BaseModel):
    backend: str
    database: str
    mqtt: str
    mongodb: str
    mongodb_pending: int
    main_device: str
    f7_device: str
    system: str
    buzzer: str
    buzzer_muted: bool | None
    ai: str
    ai_prediction: str
