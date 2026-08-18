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
        assert health_data["system"] in {"safe", "warning", "danger", "unknown"}
        assert health_data["buzzer"] in {"on", "off", "unknown"}
        assert health_data["buzzer_muted"] in {True, False, None}
        assert health_data["ai"] in {"available", "unavailable"}
        assert health_data["ai_prediction"] in {
            "safe",
            "danger",
            "insufficient_data",
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
                "distance_cm": 28.0,
                "water_level_cm": 72.0,
                "water_level_percent": 72.0,
                "vibration": 0.4,
                "motion_status": "safe",
                "status": "warning",
                "buzzer": False,
                "buzzer_muted": True,
            },
        )
        assert created.status_code == 201
        assert created.json()["status"] == "WARNING"
        assert created.json()["motion_status"] == "SAFE"
        assert created.json()["buzzer"] is False
        assert created.json()["buzzer_muted"] is True

        null_water_reading = client.post(
            "/api/readings",
            json={
                "device_id": "main-station-test",
                "temperature": 31.5,
                "humidity": 72.4,
                "gas_raw": 1380,
                "distance_cm": None,
                "water_level_cm": None,
                "status": "danger",
                "buzzer": False,
                "buzzer_muted": True,
            },
        )
        assert null_water_reading.status_code == 201
        assert null_water_reading.json()["distance_cm"] is None
        assert null_water_reading.json()["water_level_cm"] is None
        assert null_water_reading.json()["status"] == "DANGER"
        assert null_water_reading.json()["buzzer"] is False
        assert null_water_reading.json()["buzzer_muted"] is True

        readings = client.get("/api/readings", params={"limit": 20})
        assert readings.status_code == 200
        assert len(readings.json()) == 2

        first_reading = readings.json()[1]
        assert first_reading["device_id"] == "main-station-test"
        assert first_reading["gas_filtered"] == 1372.5
        assert first_reading["distance_cm"] == 28.0
        assert first_reading["water_level_percent"] == 72.0
        assert first_reading["vibration"] == 0.4
