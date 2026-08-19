from types import SimpleNamespace

import pytest
from fastapi import HTTPException, status

import backend.app.api.devices as devices_api
from backend.app.schemas.devices import BuzzerCommand


def test_buzzer_command_is_rejected_when_main_is_offline(monkeypatch) -> None:
    offline_mqtt_client = SimpleNamespace(
        is_connected=True,
        main_status="offline",
    )
    monkeypatch.setattr(devices_api, "mqtt_client", offline_mqtt_client)

    with pytest.raises(HTTPException) as exception_info:
        devices_api.control_main_buzzer(BuzzerCommand(state="OFF"))

    assert exception_info.value.status_code == status.HTTP_503_SERVICE_UNAVAILABLE
    assert exception_info.value.detail == (
        "ESP32 Main đang offline nên không thể nhận lệnh buzzer."
    )
