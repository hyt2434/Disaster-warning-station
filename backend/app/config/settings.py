from pathlib import Path

import dotenv
from pydantic_settings import BaseSettings, SettingsConfigDict


PROJECT_ROOT = Path(__file__).resolve().parents[3]
ENV_FILE = PROJECT_ROOT / ".env"

# Đọc các biến trong .env vào môi trường trước khi khởi tạo Settings.
dotenv.load_dotenv(ENV_FILE)


class Settings(BaseSettings):
    model_config = SettingsConfigDict(extra="ignore")

    app_env: str = "development"
    fastapi_host: str = "127.0.0.1"
    fastapi_port: int = 8000
    database_url: str
    cors_origins: str = "http://localhost:5173"

    mqtt_broker_host: str = "127.0.0.1"
    mqtt_broker_port: int = 1883
    mqtt_username: str = ""
    mqtt_password: str = ""
    mqtt_client_id: str = "disaster-warning-backend"

    thingspeak_write_api_key: str = ""
    thingspeak_channel_id: str = ""
    thingspeak_read_api_key: str = ""

    pushsafer_private_key: str = ""
    pushsafer_device_id: str = "a"

    @property
    def allowed_origins(self) -> list[str]:
        return [origin.strip() for origin in self.cors_origins.split(",") if origin.strip()]


settings = Settings()
