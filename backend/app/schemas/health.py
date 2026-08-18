from pydantic import BaseModel


class HealthResponse(BaseModel):
    backend: str
    database: str
    mqtt: str
    thingspeak: str
    pushsafer: str
    main_device: str
    f7_device: str
    system: str
    buzzer: str
    buzzer_muted: bool | None
    ai: str
    ai_prediction: str
