from datetime import datetime, timedelta, timezone

from backend.app.services.thingspeak import (
    build_system_prediction,
    build_thingspeak_payload,
    calculate_five_minute_values,
    normalize_thingspeak_history,
)
from backend.app.services.ai_prediction import ai_prediction_service
from ai.retrain import prepare_training_data


def test_thingspeak_payload_uses_seven_simple_fields() -> None:
    sensor_record = {
        "temperature": 32.5,
        "humidity": 68.0,
        "gas_filtered": 1420,
        "water_level_cm": 65.0,
        "motion_status": "WARNING",
        "system_status": "DANGER",
        "motion_vibration": 0.25,
    }

    payload = build_thingspeak_payload(sensor_record, "test-key")

    assert payload == {
        "api_key": "test-key",
        "field1": 32.5,
        "field2": 68.0,
        "field3": 1420,
        "field4": 65.0,
        "field5": 1,
        "field6": 2,
        "field7": 0.25,
    }


def test_thingspeak_payload_does_not_change_null_water_to_zero() -> None:
    sensor_record = {
        "temperature": 30.0,
        "humidity": 70.0,
        "gas_filtered": 900,
        "water_level_cm": None,
        "motion_status": "SAFE",
        "system_status": "SAFE",
    }

    payload = build_thingspeak_payload(sensor_record, "test-key")

    assert "field4" not in payload
    assert payload["field5"] == 0
    assert payload["field6"] == 0


def test_f7_can_upload_only_field7_when_main_is_offline() -> None:
    payload = build_thingspeak_payload(
        {"motion_vibration": 0.42},
        "test-key",
    )

    assert payload == {
        "api_key": "test-key",
        "field7": 0.42,
    }


def test_ai_training_reads_thingspeak_field_mapping() -> None:
    records = [
        {
            "field1": "30.0",
            "field2": "70.0",
            "field3": "900",
            "field4": "40.0",
        }
        for _ in range(99)
    ]
    records.append(
        {
            "field1": "42.0",
            "field2": "70.0",
            "field3": "1700",
            "field4": "72.0",
        }
    )

    features, labels = prepare_training_data(records)

    assert list(features.columns) == [
        "temperature",
        "humidity",
        "gas_filtered",
        "water_danger",
    ]
    assert len(features) == 100
    assert labels.iloc[-1] == 1


def test_future_value_estimation_reads_all_seven_thingspeak_fields() -> None:
    start_time = datetime(2026, 8, 19, 10, 0, tzinfo=timezone.utc)
    feeds = []

    for index in range(20):
        feeds.append({
            "created_at": (start_time + timedelta(seconds=index * 15)).isoformat(),
            "field1": str(30 + index * 0.1),
            "field2": "70",
            "field3": str(1000 + index * 5),
            "field4": str(50 + index * 0.1),
            "field5": "0",
            "field6": "0" if index < 10 else "1",
            "field7": str(index * 0.02),
        })

    result = calculate_five_minute_values(feeds)

    assert result["prediction_minutes"] == 5
    assert result["sample_count"] == 20
    assert len(result["fields"]) == 7
    assert result["fields"][0]["predicted"] > 31.9
    assert result["fields"][1]["predicted"] == 70
    assert result["fields"][5]["predicted"] == 2


def test_thingspeak_history_is_ready_for_frontend_chart() -> None:
    feeds = [{
        "created_at": "2026-08-19T10:00:00Z",
        "field1": "31.5",
        "field2": "70",
        "field3": "1200",
        "field4": "55",
        "field5": "0",
        "field6": "1",
        "field7": "0.25",
    }]

    history = normalize_thingspeak_history(feeds)

    assert history[0]["recorded_at"] == "2026-08-19T10:00:00Z"
    assert history[0]["field1"] == 31.5
    assert history[0]["field6"] == 1.0
    assert history[0]["field7"] == 0.25


def test_future_values_are_sent_to_ai_and_warning_cause_is_explained(
    monkeypatch,
) -> None:
    received_model_values = {}

    def fake_model_prediction(**sensor_values) -> str:
        received_model_values.update(sensor_values)
        return "safe"

    monkeypatch.setattr(ai_prediction_service, "predict", fake_model_prediction)

    future_data = {
        "source": "ThingSpeak",
        "prediction_minutes": 5,
        "sample_count": 20,
        "fields": [
            {"field": "field1", "predicted": 36.0},
            {"field": "field2", "predicted": 70.0},
            {"field": "field3", "predicted": 1100.0},
            {"field": "field4", "predicted": 55.0},
            {"field": "field5", "predicted": 0},
            {"field": "field6", "predicted": 2},
            {"field": "field7", "predicted": 0.2},
        ],
    }

    result = build_system_prediction(future_data)

    assert received_model_values["temperature"] == 36.0
    assert received_model_values["gas_level"] == 1100.0
    assert result["system_prediction"] == "WARNING"
    assert result["causes"][0]["sensor"] == "Nhiệt độ"
    assert result["causes"][0]["level"] == "WARNING"


def test_f7_vibration_trend_can_make_system_danger(monkeypatch) -> None:
    monkeypatch.setattr(ai_prediction_service, "predict", lambda **_: "safe")

    main_future_data = {
        "source": "ThingSpeak",
        "prediction_minutes": 5,
        "sample_count": 20,
        "fields": [
            {"field": "field1", "predicted": 30.0},
            {"field": "field2", "predicted": 70.0},
            {"field": "field3", "predicted": 900.0},
            {"field": "field4", "predicted": 40.0},
            {"field": "field5", "predicted": 0},
            {"field": "field6", "predicted": 0},
            {"field": "field7", "predicted": 2.5},
        ],
    }

    result = build_system_prediction(main_future_data)

    assert result["system_prediction"] == "DANGER"
    assert any(cause["sensor"] == "Rung F7" for cause in result["causes"])
