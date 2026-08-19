from datetime import datetime, timedelta, timezone
from types import SimpleNamespace

import pytest

from backend.app.mqtt.client import MQTTClient, normalize_main_telemetry
from backend.app.mqtt.topics import MAIN_STATUS_TOPIC


def main_telemetry() -> dict:
    return {
        "deviceId": "main-station-01",
        "temperature": 30.0,
        "humidity": 70.0,
        "gas": 900,
        "distanceCm": 50.0,
        "waterLevelCm": 50.0,
        "motion": "SAFE",
        "system": "SAFE",
        "buzzer": False,
        "buzzerMuted": False,
    }


def test_danger_and_muted_buzzer_are_independent_states() -> None:
    telemetry = {
        "deviceId": "main-station-01",
        "temperature": 33.2,
        "humidity": 69.3,
        "gas": 942,
        "distanceCm": 51.4,
        "waterLevelCm": 48.6,
        "motion": "SAFE",
        "system": "DANGER",
        "buzzer": False,
        "buzzerMuted": True,
    }

    normalized = normalize_main_telemetry(telemetry)

    assert normalized["system_status"] == "DANGER"
    assert normalized["buzzer"] is False
    assert normalized["buzzer_muted"] is True


def test_invalid_water_measurement_stays_null() -> None:
    telemetry = {
        "deviceId": "main-station-01",
        "temperature": 30.0,
        "humidity": 70.0,
        "gas": 900,
        "distanceCm": None,
        "waterLevelCm": None,
        "motion": "SAFE",
        "system": "SAFE",
        "buzzer": False,
        "buzzerMuted": False,
    }

    normalized = normalize_main_telemetry(telemetry)

    assert normalized["distance_cm"] is None
    assert normalized["water_level_cm"] is None
    assert normalized["f7_roll"] is None
    assert normalized["f7_status"] is None


def test_only_fresh_f7_telemetry_populates_f7_database_fields() -> None:
    mqtt_client = object.__new__(MQTTClient)
    sensor_record = normalize_main_telemetry(main_telemetry())
    mqtt_client._latest_f7 = {
        "roll": 1.2,
        "pitch": -2.0,
        "tilt": 4.1,
        "vibration": 0.3,
        "impact": 0.8,
        "status": "DANGER",
        "received_at": datetime.now(timezone.utc),
    }

    mqtt_client._add_latest_f7_data(sensor_record)

    assert sensor_record["motion_status"] == "SAFE"
    assert sensor_record["f7_roll"] == 1.2
    assert sensor_record["f7_status"] == "DANGER"

    stale_record = normalize_main_telemetry(main_telemetry())
    mqtt_client._latest_f7["received_at"] = (
        datetime.now(timezone.utc) - timedelta(seconds=11)
    )

    mqtt_client._add_latest_f7_data(stale_record)

    assert stale_record["f7_roll"] is None
    assert stale_record["f7_status"] is None


def test_old_status_alias_is_rejected() -> None:
    telemetry = main_telemetry()
    telemetry["system"] = "NORMAL"

    with pytest.raises(ValueError, match="SAFE, WARNING hoặc DANGER"):
        normalize_main_telemetry(telemetry)


def test_main_offline_status_clears_stale_runtime_state() -> None:
    mqtt_client = object.__new__(MQTTClient)
    mqtt_client._main_status = "online"
    mqtt_client._system_state = "danger"
    mqtt_client._buzzer_state = "on"
    mqtt_client._buzzer_muted = False
    message = SimpleNamespace(
        topic=MAIN_STATUS_TOPIC,
        payload=b"offline",
    )

    mqtt_client._on_message(None, None, message)

    assert mqtt_client.main_status == "offline"
    assert mqtt_client.system_state == "unknown"
    assert mqtt_client.buzzer_state == "unknown"
    assert mqtt_client.buzzer_muted is None
