from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", env_file_encoding="utf-8", extra="ignore")

    database_url: str = ""
    mqtt_broker_host: str = "localhost"
    mqtt_broker_port: int = 1883


settings = Settings()