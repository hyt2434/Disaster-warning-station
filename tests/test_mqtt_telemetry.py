from backend.app.mqtt.client import normalize_main_telemetry


def test_danger_and_muted_buzzer_are_independent_states() -> None:
    telemetry = {
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
    assert normalized["water_level_percent"] is None
