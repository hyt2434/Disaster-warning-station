from backend.app.services.alerts import build_pushsafer_payload


def test_pushsafer_payload_contains_required_fields() -> None:
    payload = build_pushsafer_payload(
        title="Disaster Warning - DANGER",
        message="ESP32 Main báo DANGER.",
    )

    assert payload["t"] == "Disaster Warning - DANGER"
    assert payload["m"] == "ESP32 Main báo DANGER."
    assert "k" in payload
    assert "d" in payload
