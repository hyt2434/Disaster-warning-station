import os
import tempfile
from pathlib import Path
from uuid import uuid4

from fastapi.testclient import TestClient


database_path = Path(tempfile.gettempdir()) / f"dws-test-{uuid4().hex}.sqlite"
os.environ["DATABASE_URL"] = f"sqlite:///{database_path.as_posix()}"

from backend.app.application import app  # noqa: E402


def test_health_and_reading_flow() -> None:
    with TestClient(app) as client:
        health = client.get("/api/health")
        assert health.status_code == 200
        health_data = health.json()
        assert health_data["backend"] == "online"
        assert health_data["database"] == "connected"
        assert health_data["mqtt"] in {"connected", "disconnected"}
        assert health_data["mongodb"] in {
            "connected",
            "disconnected",
            "not_configured",
        }
        assert health_data["mongodb_pending"] >= 0
        assert health_data["main_device"] in {"online", "offline", "unknown"}
        assert health_data["f7_device"] in {"online", "direct", "offline", "unknown"}
        assert health_data["buzzer"] in {"on", "off", "unknown"}
        assert health_data["ai"] in {"available", "unavailable"}
        assert health_data["ai_prediction"] in {
            "safe",
            "danger",
            "not_run",
            "unavailable",
        }

        latest_f7 = client.get("/api/devices/f7/latest")
        assert latest_f7.status_code == 200
        assert latest_f7.json() is None

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
                "gas_filtered": 1372.5,
                "distance_cm": 18.0,
                "water_level_cm": 8.2,
                "water_level_percent": 27.3,
                "vibration": 0.4,
                "status": "warning",
            },
        )
        assert created.status_code == 201
        assert created.json()["status"] == "WARNING"

        readings = client.get("/api/readings", params={"limit": 20})
        assert readings.status_code == 200
        assert len(readings.json()) == 1
        assert readings.json()[0]["device_id"] == "main-station-test"
        assert readings.json()[0]["gas_filtered"] == 1372.5
        assert readings.json()[0]["distance_cm"] == 18.0
        assert readings.json()[0]["water_level_percent"] == 27.3
        assert readings.json()[0]["vibration"] == 0.4
