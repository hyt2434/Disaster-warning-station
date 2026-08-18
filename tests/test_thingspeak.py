from backend.app.services.thingspeak import build_thingspeak_payload
from ai.retrain import prepare_training_data


def test_thingspeak_payload_uses_six_simple_fields() -> None:
    sensor_record = {
        "temperature": 32.5,
        "humidity": 68.0,
        "gas_filtered": 1420,
        "water_level_cm": 65.0,
        "motion_status": "WARNING",
        "system_status": "DANGER",
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
