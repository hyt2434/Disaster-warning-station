import os
import tempfile
from pathlib import Path
from uuid import uuid4

from fastapi.testclient import TestClient


database_path = Path(tempfile.gettempdir()) / f"dws-test-{uuid4().hex}.sqlite"
os.environ["DATABASE_URL"] = f"sqlite:///{database_path.as_posix()}"

from backend.app.main import app  # noqa: E402


def test_health_and_reading_flow() -> None:
    with TestClient(app) as client:
        health = client.get("/api/health")
        assert health.status_code == 200
        assert health.json() == {"backend": "online", "database": "connected"}

        empty_latest = client.get("/api/readings/latest")
        assert empty_latest.status_code == 200
        assert empty_latest.json() is None

        created = client.post(
            "/api/readings",
            json={
                "device_id": "main-station-test",
                "temperature": 31.5,
                "humidity": 72.4,
                "gas_raw": 1380,
                "water_level_cm": 8.2,
                "status": "warning",
            },
        )
        assert created.status_code == 201
        assert created.json()["status"] == "WARNING"

        readings = client.get("/api/readings", params={"limit": 20})
        assert readings.status_code == 200
        assert len(readings.json()) == 1
        assert readings.json()[0]["device_id"] == "main-station-test"
