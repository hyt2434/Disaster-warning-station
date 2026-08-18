import logging
import time
from datetime import datetime

import httpx

from ..config import settings
from .ai_prediction import ai_prediction_service


logger = logging.getLogger("uvicorn.error")

THINGSPEAK_URL = "https://api.thingspeak.com/update.json"
MINIMUM_UPLOAD_INTERVAL_SECONDS = 15
TREND_HISTORY_RESULTS = 20
PREDICTION_MINUTES = 5
TREND_REFRESH_INTERVAL_SECONDS = 15

TEMPERATURE_WARNING = 35.0
TEMPERATURE_DANGER = 40.0
GAS_WARNING = 1300.0
GAS_DANGER = 1600.0
WATER_WARNING = 60.0
WATER_DANGER = 70.0
MOTION_WARNING = 1
MOTION_DANGER = 2

STATUS_NUMBER = {
    "NORMAL": 0,
    "SAFE": 0,
    "WARNING": 1,
    "DANGER": 2,
}

TREND_FIELDS = [
    {
        "field": "field1",
        "minimum": -40,
        "maximum": 100,
        "is_status": False,
    },
    {
        "field": "field2",
        "minimum": 0,
        "maximum": 100,
        "is_status": False,
    },
    {
        "field": "field3",
        "minimum": 0,
        "maximum": 4095,
        "is_status": False,
    },
    {
        "field": "field4",
        "minimum": 0,
        "maximum": 77,
        "is_status": False,
    },
    {
        "field": "field5",
        "minimum": 0,
        "maximum": 2,
        "is_status": True,
    },
    {
        "field": "field6",
        "minimum": 0,
        "maximum": 2,
        "is_status": True,
    },
]


def _parse_thingspeak_time(value: str) -> datetime:
    """Convert one ThingSpeak ISO timestamp to datetime."""
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def _limit_value(value: float, minimum: float, maximum: float) -> float:
    return max(minimum, min(value, maximum))


def calculate_five_minute_values(feeds: list[dict]) -> dict:
    """Estimate the six ThingSpeak field values five minutes from now."""
    field_predictions = []

    for field_config in TREND_FIELDS:
        field_name = field_config["field"]
        valid_points = []

        for feed in feeds:
            raw_value = feed.get(field_name)
            recorded_at = feed.get("created_at")

            if raw_value in {None, ""} or not recorded_at:
                continue

            try:
                valid_points.append((
                    _parse_thingspeak_time(recorded_at),
                    float(raw_value),
                ))
            except (TypeError, ValueError):
                continue

        valid_points.sort(key=lambda point: point[0])

        if not valid_points:
            field_predictions.append({"field": field_name, "predicted": None})
            continue

        first_time, first_value = valid_points[0]
        latest_time, latest_value = valid_points[-1]
        elapsed_minutes = (latest_time - first_time).total_seconds() / 60

        if len(valid_points) < 2 or elapsed_minutes <= 0:
            predicted_value = latest_value
        else:
            change_per_minute = (latest_value - first_value) / elapsed_minutes
            predicted_value = latest_value + change_per_minute * PREDICTION_MINUTES

        predicted_value = _limit_value(
            predicted_value,
            field_config["minimum"],
            field_config["maximum"],
        )

        if field_config["is_status"]:
            predicted_value = round(predicted_value)

        field_predictions.append({
            "field": field_name,
            "predicted": round(predicted_value, 2),
        })

    return {
        "source": "ThingSpeak",
        "prediction_minutes": PREDICTION_MINUTES,
        "sample_count": len(feeds),
        "fields": field_predictions,
    }


def _add_cause(
    causes: list[dict],
    field_name: str,
    sensor_name: str,
    predicted_value: float,
    unit: str,
    warning_threshold: float,
    danger_threshold: float,
) -> None:
    """Add one warning cause when a predicted value crosses a threshold."""
    if predicted_value >= danger_threshold:
        causes.append({
            "field": field_name,
            "sensor": sensor_name,
            "level": "DANGER",
            "predicted_value": predicted_value,
            "unit": unit,
            "threshold": danger_threshold,
        })
    elif predicted_value >= warning_threshold:
        causes.append({
            "field": field_name,
            "sensor": sensor_name,
            "level": "WARNING",
            "predicted_value": predicted_value,
            "unit": unit,
            "threshold": warning_threshold,
        })


def build_system_prediction(future_data: dict) -> dict:
    """Use future ThingSpeak values and the trained model for one system result."""
    fields_by_name = {
        field["field"]: field
        for field in future_data["fields"]
    }

    temperature = fields_by_name["field1"]["predicted"]
    humidity = fields_by_name["field2"]["predicted"]
    gas_level = fields_by_name["field3"]["predicted"]
    water_level = fields_by_name["field4"]["predicted"]
    motion_status = fields_by_name["field5"]["predicted"]

    model_result = ai_prediction_service.predict(
        temperature=temperature,
        humidity=humidity,
        gas_level=gas_level,
        water_level_cm=water_level,
    )

    causes = []

    if temperature is not None:
        _add_cause(
            causes,
            "field1",
            "Nhiệt độ",
            temperature,
            "°C",
            TEMPERATURE_WARNING,
            TEMPERATURE_DANGER,
        )

    if gas_level is not None:
        _add_cause(
            causes,
            "field3",
            "Khói / gas",
            gas_level,
            "ADC",
            GAS_WARNING,
            GAS_DANGER,
        )

    if water_level is not None:
        _add_cause(
            causes,
            "field4",
            "Mực nước",
            water_level,
            "cm",
            WATER_WARNING,
            WATER_DANGER,
        )

    if motion_status is not None:
        _add_cause(
            causes,
            "field5",
            "Rung / nghiêng",
            motion_status,
            "status",
            MOTION_WARNING,
            MOTION_DANGER,
        )

    has_danger_cause = any(cause["level"] == "DANGER" for cause in causes)
    has_warning_cause = any(cause["level"] == "WARNING" for cause in causes)

    if model_result == "danger" or has_danger_cause:
        system_prediction = "DANGER"
    elif has_warning_cause:
        system_prediction = "WARNING"
    elif model_result == "safe":
        system_prediction = "NORMAL"
    else:
        system_prediction = "INSUFFICIENT_DATA"

    return {
        "source": future_data["source"],
        "prediction_minutes": future_data["prediction_minutes"],
        "sample_count": future_data["sample_count"],
        "system_prediction": system_prediction,
        "model_result": model_result,
        "model_available": ai_prediction_service.is_available,
        "causes": causes,
        "predicted_fields": {
            field_name: field["predicted"]
            for field_name, field in fields_by_name.items()
        },
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


def normalize_thingspeak_history(feeds: list[dict]) -> list[dict]:
    """Convert ThingSpeak strings to values that the frontend can chart."""
    history = []

    for feed in feeds:
        record = {
            "recorded_at": feed.get("created_at"),
        }

        for field_number in range(1, 7):
            field_name = f"field{field_number}"
            raw_value = feed.get(field_name)

            try:
                if raw_value in {None, ""}:
                    record[field_name] = None
                else:
                    record[field_name] = float(raw_value)
            except (TypeError, ValueError):
                record[field_name] = None

        history.append(record)

    return history


class ThingSpeakClient:
    def __init__(self) -> None:
        self._last_upload_time = 0.0
        self._last_cloud_download_time = 0.0
        self._cached_feeds: list[dict] = []
        self._cached_prediction: dict | None = None
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

    def _download_recent_feeds(self) -> list[dict]:
        """Download once every 15 seconds and share data with chart and AI."""
        if not settings.thingspeak_channel_id:
            raise RuntimeError("Chưa cấu hình THINGSPEAK_CHANNEL_ID trong .env")

        current_time = time.monotonic()
        cache_age = current_time - self._last_cloud_download_time

        if self._cached_feeds and cache_age < TREND_REFRESH_INTERVAL_SECONDS:
            return self._cached_feeds

        url = (
            "https://api.thingspeak.com/channels/"
            f"{settings.thingspeak_channel_id}/feeds.json"
        )
        parameters = {"results": TREND_HISTORY_RESULTS}

        if settings.thingspeak_read_api_key:
            parameters["api_key"] = settings.thingspeak_read_api_key

        response = httpx.get(url, params=parameters, timeout=10)
        response.raise_for_status()
        response_data = response.json()
        feeds = response_data.get("feeds", [])

        if not isinstance(feeds, list):
            raise ValueError("ThingSpeak không trả về danh sách feeds hợp lệ.")

        self._cached_feeds = feeds
        self._last_cloud_download_time = current_time
        return feeds

    def get_recent_history(self) -> dict:
        """Return cloud history for the frontend time-series chart."""
        feeds = self._download_recent_feeds()
        return {
            "source": "ThingSpeak",
            "sample_count": len(feeds),
            "readings": normalize_thingspeak_history(feeds),
        }

    def get_five_minute_prediction(self) -> dict:
        """Use the same cloud history to predict the system after five minutes."""
        feeds = self._download_recent_feeds()
        future_values = calculate_five_minute_values(feeds)
        self._cached_prediction = build_system_prediction(future_values)
        return self._cached_prediction


thingspeak_client = ThingSpeakClient()
