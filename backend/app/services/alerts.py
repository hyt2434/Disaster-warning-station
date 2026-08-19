import logging

import httpx

from ..config import settings


logger = logging.getLogger("uvicorn.error")

PUSHSAFER_URL = "https://www.pushsafer.com/api"

TEMPERATURE_DANGER = 40.0
GAS_DANGER = 1600.0
WATER_DANGER = 70.0
TILT_DANGER = 30.0
VIBRATION_DANGER = 2.0
IMPACT_DANGER = 8.0


def _number(value: object) -> float | None:
    """Return a number when a telemetry value is valid."""
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _add_danger_value(
    causes: list[str],
    label: str,
    value: object,
    unit: str,
    danger_threshold: float,
) -> None:
    """Add a cause only when its value reaches the DANGER threshold."""
    numeric_value = _number(value)

    if numeric_value is None or numeric_value < danger_threshold:
        return

    causes.append(
        f"{label}: {numeric_value:.1f} {unit} "
        f"(ngưỡng nguy hiểm từ {danger_threshold:g} {unit})"
    )


def build_main_danger_message(sensor_record: dict) -> str:
    """Explain which Main or F7 value made the whole system dangerous."""
    causes: list[str] = []

    _add_danger_value(
        causes,
        "Nhiệt độ",
        sensor_record.get("temperature"),
        "°C",
        TEMPERATURE_DANGER,
    )
    _add_danger_value(
        causes,
        "Khói / gas",
        sensor_record.get("gas_filtered"),
        "ADC",
        GAS_DANGER,
    )
    _add_danger_value(
        causes,
        "Mực nước",
        sensor_record.get("water_level_cm"),
        "cm",
        WATER_DANGER,
    )

    motion_is_dangerous = str(
        sensor_record.get("motion_status", "")
    ).upper() == "DANGER"

    if motion_is_dangerous:
        cause_count_before_motion = len(causes)

        _add_danger_value(
            causes,
            "Độ nghiêng F7",
            sensor_record.get("motion_tilt"),
            "độ",
            TILT_DANGER,
        )
        _add_danger_value(
            causes,
            "Độ rung F7",
            sensor_record.get("motion_vibration"),
            "m/s²",
            VIBRATION_DANGER,
        )
        _add_danger_value(
            causes,
            "Va đập F7",
            sensor_record.get("motion_impact"),
            "m/s²",
            IMPACT_DANGER,
        )

        if len(causes) == cause_count_before_motion:
            causes.append("Cảm biến chuyển động F7 đang báo DANGER")

    if not causes:
        causes.append("ESP32 Main báo DANGER nhưng không có đủ giá trị cảm biến")

    return "Phát hiện trạng thái NGUY HIỂM.\nNguyên nhân:\n- " + "\n- ".join(causes)


def build_f7_danger_message(f7_record: dict) -> str:
    """Explain which MPU6050 value made F7 dangerous."""
    causes: list[str] = []

    _add_danger_value(
        causes,
        "Độ nghiêng F7",
        f7_record.get("tilt"),
        "độ",
        TILT_DANGER,
    )
    _add_danger_value(
        causes,
        "Độ rung F7",
        f7_record.get("vibration"),
        "m/s²",
        VIBRATION_DANGER,
    )
    _add_danger_value(
        causes,
        "Va đập F7",
        f7_record.get("impact"),
        "m/s²",
        IMPACT_DANGER,
    )

    if not causes:
        causes.append("ESP32 F7 báo DANGER nhưng không có đủ giá trị cảm biến")

    return "F7 phát hiện trạng thái NGUY HIỂM.\nNguyên nhân:\n- " + "\n- ".join(causes)


def build_pushsafer_payload(title: str, message: str) -> dict:
    """Build the small form body required by the Pushsafer API."""
    return {
        "k": settings.pushsafer_private_key,
        "d": settings.pushsafer_device_id or "a",
        "t": title,
        "m": message,
        "s": "8",
        "v": "2",
        "i": "5",
        "c": "#FF0000",
    }


class AlertService:
    """Send one phone notification for each DANGER event."""

    def __init__(self) -> None:
        self._danger_active_by_source: dict[str, bool] = {}
        self._status = "ready" if settings.pushsafer_private_key else "not_configured"

    @property
    def status(self) -> str:
        return self._status

    def notify_if_needed(
        self,
        source: str,
        current_status: str,
        message: str,
    ) -> bool:
        normalized_status = current_status.upper()

        # SAFE/NORMAL ends the current alarm event. WARNING does not send a
        # phone notification and does not reset an existing DANGER event.
        if normalized_status in {"SAFE", "NORMAL"}:
            self._danger_active_by_source[source] = False
            return False

        if normalized_status != "DANGER":
            return False

        if self._danger_active_by_source.get(source, False):
            return False

        self._danger_active_by_source[source] = True
        title = "Disaster Warning - DANGER"
        return self._send(title, message)

    def _send(self, title: str, message: str) -> bool:
        if not settings.pushsafer_private_key:
            self._status = "not_configured"
            return False

        payload = build_pushsafer_payload(title, message)

        try:
            response = httpx.post(PUSHSAFER_URL, data=payload, timeout=5)
            response_data = response.json()

            if response.status_code == 200 and response_data.get("status") == 1:
                self._status = "connected"
                logger.info("Đã gửi cảnh báo qua Pushsafer.")
                return True

            self._status = "error"
            logger.warning(
                "Pushsafer từ chối thông báo: %s",
                response_data.get("error", response.text),
            )
            return False
        except (httpx.HTTPError, ValueError) as error:
            self._status = "disconnected"
            logger.warning("Không thể gửi thông báo Pushsafer: %s", error)
            return False


alert_service = AlertService()
