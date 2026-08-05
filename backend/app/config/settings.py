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
    database_url: str
    cors_origins: str = "http://localhost:5173"
    mqtt_broker_host: str = "localhost"
    mqtt_broker_port: int = 1883

    @property
    def allowed_origins(self) -> list[str]:
        return [origin.strip() for origin in self.cors_origins.split(",") if origin.strip()]


settings = Settings()
