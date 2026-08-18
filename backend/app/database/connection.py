from collections.abc import Generator

from sqlalchemy import create_engine, inspect, text
from sqlalchemy.orm import DeclarativeBase, Session, sessionmaker

from ..config import settings


class Base(DeclarativeBase):
    pass


connect_args = {"check_same_thread": False} if settings.database_url.startswith("sqlite") else {}
engine = create_engine(settings.database_url, pool_pre_ping=True, connect_args=connect_args)
SessionLocal = sessionmaker(bind=engine, autoflush=False, expire_on_commit=False)


def create_tables() -> None:
    # Import tại đây để đăng ký models mà không tạo circular import.
    from . import models  # noqa: F401

    Base.metadata.create_all(bind=engine)
    add_missing_sensor_reading_columns()


def add_missing_sensor_reading_columns() -> None:
    """Add the three Alarm Event Mute columns to an existing demo database."""
    database_inspector = inspect(engine)

    if not database_inspector.has_table("sensor_readings"):
        return

    existing_columns = {
        column["name"]
        for column in database_inspector.get_columns("sensor_readings")
    }
    required_columns = {
        "motion_status": "VARCHAR(30)",
        "buzzer": "BOOLEAN",
        "buzzer_muted": "BOOLEAN",
    }

    with engine.begin() as connection:
        for column_name, column_type in required_columns.items():
            if column_name in existing_columns:
                continue

            if engine.dialect.name == "postgresql":
                statement = (
                    f"ALTER TABLE sensor_readings "
                    f"ADD COLUMN IF NOT EXISTS {column_name} {column_type}"
                )
            else:
                statement = (
                    f"ALTER TABLE sensor_readings "
                    f"ADD COLUMN {column_name} {column_type}"
                )

            connection.execute(text(statement))


def database_is_available() -> bool:
    try:
        with engine.connect() as connection:
            connection.execute(text("SELECT 1"))
        return True
    except Exception:
        return False


def get_db() -> Generator[Session, None, None]:
    database = SessionLocal()
    try:
        yield database
    finally:
        database.close()
