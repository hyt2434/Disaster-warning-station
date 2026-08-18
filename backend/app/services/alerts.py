import logging

import httpx

from ..config import settings


logger = logging.getLogger("uvicorn.error")

PUSHSAFER_URL = "https://www.pushsafer.com/api"
ALERT_STATUSES = {"WARNING", "DANGER"}


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
    """Send one notification when a source enters WARNING or DANGER."""

    def __init__(self) -> None:
        self._last_status_by_source: dict[str, str] = {}
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
        previous_status = self._last_status_by_source.get(source)
        self._last_status_by_source[source] = normalized_status

        if normalized_status not in ALERT_STATUSES:
            return False

        # ESP32 publishes frequently. Only notify when the status changes so the
        # same warning does not consume many Pushsafer API calls.
        if normalized_status == previous_status:
            return False

        title = f"Disaster Warning - {normalized_status}"
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
