import logging
import time

import httpx

from ..config import settings


logger = logging.getLogger("uvicorn.error")

THINGSPEAK_URL = "https://api.thingspeak.com/update.json"
MINIMUM_UPLOAD_INTERVAL_SECONDS = 15

STATUS_NUMBER = {
    "NORMAL": 0,
    "SAFE": 0,
    "WARNING": 1,
    "DANGER": 2,
}


def build_thingspeak_payload(sensor_record: dict, api_key: str) -> dict:
    """Convert one normalized sensor record to ThingSpeak fields."""
    motion_status = str(sensor_record.get("motion_status", "SAFE")).upper()
    system_status = str(sensor_record.get("system_status", "SAFE")).upper()

    payload = {
        "api_key": api_key,
        "field1": sensor_record.get("temperature"),
        "field2": sensor_record.get("humidity"),
        "field3": sensor_record.get("gas_filtered"),
        "field4": sensor_record.get("water_level_cm"),
        "field5": STATUS_NUMBER.get(motion_status, 0),
        "field6": STATUS_NUMBER.get(system_status, 0),
    }

    # ThingSpeak should receive no field when a sensor value is unknown.
    return {
        field_name: field_value
        for field_name, field_value in payload.items()
        if field_value is not None
    }


class ThingSpeakClient:
    def __init__(self) -> None:
        self._last_upload_time = 0.0
        self._status = "ready" if settings.thingspeak_write_api_key else "not_configured"

    @property
    def status(self) -> str:
        return self._status

    def upload(self, sensor_record: dict) -> bool:
        """Upload at most one record every 15 seconds."""
        if not settings.thingspeak_write_api_key:
            self._status = "not_configured"
            return False

        current_time = time.monotonic()
        elapsed_seconds = current_time - self._last_upload_time

        if elapsed_seconds < MINIMUM_UPLOAD_INTERVAL_SECONDS:
            return False

        payload = build_thingspeak_payload(
            sensor_record,
            settings.thingspeak_write_api_key,
        )

        try:
            response = httpx.post(
                THINGSPEAK_URL,
                data=payload,
                timeout=5,
            )
            self._last_upload_time = current_time

            response_text = response.text.strip()
            upload_was_accepted = (
                response.status_code == 200
                and response_text not in {"0", '"0"', "null"}
            )

            if upload_was_accepted:
                self._status = "connected"
                logger.info("Đã đồng bộ telemetry lên ThingSpeak Cloud.")
                return True

            self._status = "rate_limited"
            logger.warning(
                "ThingSpeak từ chối dữ liệu. Hãy kiểm tra giới hạn 15 giây và Write API Key."
            )
            return False
        except httpx.HTTPError as error:
            self._last_upload_time = current_time
            self._status = "disconnected"
            logger.warning("Không thể gửi dữ liệu lên ThingSpeak: %s", error)
            return False


thingspeak_client = ThingSpeakClient()
