from backend.app.services.alerts import (
    AlertService,
    build_f7_danger_message,
    build_main_danger_message,
    build_pushsafer_payload,
)


def test_pushsafer_payload_contains_required_fields() -> None:
    payload = build_pushsafer_payload(
        title="Disaster Warning - DANGER",
        message="ESP32 Main báo DANGER.",
    )

    assert payload["t"] == "Disaster Warning - DANGER"
    assert payload["m"] == "ESP32 Main báo DANGER."
    assert "k" in payload
    assert "d" in payload


def test_main_danger_message_only_lists_dangerous_values() -> None:
    message = build_main_danger_message({
        "temperature": 42.5,
        "gas_average": 1000,
        "water_level_cm": 72.0,
        "motion_status": "SAFE",
    })

    assert "Nhiệt độ: 42.5 °C" in message
    assert "Mực nước: 72.0 cm" in message
    assert "Khói / gas" not in message


def test_f7_danger_message_explains_motion_cause() -> None:
    message = build_f7_danger_message({
        "tilt": 35.0,
        "vibration": 0.5,
        "impact": 9.2,
    })

    assert "Độ nghiêng F7: 35.0 độ" in message
    assert "Va đập F7: 9.2 m/s²" in message
    assert "Độ rung F7" not in message


def test_pushsafer_only_sends_once_for_each_danger_event(monkeypatch) -> None:
    service = AlertService()
    sent_messages = []

    def fake_send(title: str, message: str) -> bool:
        sent_messages.append((title, message))
        return True

    monkeypatch.setattr(service, "_send", fake_send)

    assert service.notify_if_needed("main", "WARNING", "warning") is False
    assert service.notify_if_needed("main", "DANGER", "first danger") is True
    assert service.notify_if_needed("main", "DANGER", "same danger") is False
    assert service.notify_if_needed("main", "WARNING", "still active") is False
    assert service.notify_if_needed("main", "DANGER", "same event") is False
    assert service.notify_if_needed("main", "SAFE", "safe") is False
    assert service.notify_if_needed("main", "DANGER", "new danger") is True

    assert sent_messages == [
        ("Disaster Warning - DANGER", "first danger"),
        ("Disaster Warning - DANGER", "new danger"),
    ]
