import json
import logging
import os
import threading
from datetime import datetime, timezone

import paho.mqtt.client as mqtt

from ..config import settings
from ..database import SessionLocal
from ..database.repository import create_f7_reading, create_reading
from ..schemas import F7ReadingCreate, SensorReadingCreate
from ..services import ai_prediction_service, alert_service, thingspeak_client
from .topics import (
    BACKEND_STATUS_TOPIC,
    BUZZER_STATE_TOPIC,
    F7_STATUS_TOPIC,
    F7_TELEMETRY_TOPIC,
    MAIN_STATUS_TOPIC,
    MAIN_TELEMETRY_TOPIC,
)


logger = logging.getLogger("uvicorn.error")

SENSOR_HEIGHT_CM = 100.0
F7_DATA_MAX_AGE_SECONDS = 10


def first_available_value(
    telemetry: dict,
    field_names: list[str],
    default: object = None,
) -> object:
    """Return the first field that exists, including a field containing null."""
    for field_name in field_names:
        if field_name in telemetry:
            return telemetry[field_name]

    return default


def normalize_main_telemetry(
    telemetry: dict,
    received_at: datetime | None = None,
) -> dict:
    """Convert ESP32 camelCase fields to simple backend field names."""
    distance_cm = first_available_value(telemetry, ["distanceCm", "distance"])
    water_level_cm = first_available_value(
        telemetry,
        ["waterLevelCm", "waterLevel", "water"],
    )
    water_level_percent = first_available_value(
        telemetry,
        ["waterLevelPercent"],
    )

    if water_level_percent is None and water_level_cm is not None:
        water_level_percent = float(water_level_cm) / SENSOR_HEIGHT_CM * 100.0

    gas_raw = first_available_value(
        telemetry,
        ["gasRaw", "gas", "smoke"],
        0,
    )
    gas_filtered = first_available_value(
        telemetry,
        ["gasFiltered"],
        gas_raw,
    )

    return {
        "timestamp": received_at or datetime.now(timezone.utc),
        "device_id": first_available_value(
            telemetry,
            ["deviceId"],
            "main-station-01",
        ),
        "temperature": telemetry.get("temperature", 0.0),
        "humidity": telemetry.get("humidity", 0.0),
        "gas_raw": gas_raw,
        "gas_filtered": gas_filtered,
        "distance_cm": distance_cm,
        "water_level_cm": water_level_cm,
        "water_level_percent": water_level_percent,
        "motion_status": first_available_value(
            telemetry,
            ["motion", "motionStatus"],
            "SAFE",
        ),
        "motion_source": telemetry.get("motionSource", "MQTT"),
        "motion_roll": telemetry.get("motionRoll"),
        "motion_pitch": telemetry.get("motionPitch"),
        "motion_tilt": telemetry.get("motionTilt"),
        "motion_vibration": telemetry.get("motionVibration"),
        "motion_impact": telemetry.get("motionImpact"),
        "system_status": first_available_value(
            telemetry,
            ["system", "systemStatus"],
            "SAFE",
        ),
        "buzzer": telemetry.get("buzzer")
        if isinstance(telemetry.get("buzzer"), bool)
        else None,
        "buzzer_muted": telemetry.get("buzzerMuted")
        if isinstance(telemetry.get("buzzerMuted"), bool)
        else None,
    }


class MQTTClient:
    def __init__(self) -> None:
        self._connected = threading.Event()
        self._loop_started = False
        self._main_status = "unknown"
        self._f7_status = "unknown"
        self._system_state = "unknown"
        self._buzzer_state = "unknown"
        self._buzzer_muted: bool | None = None
        self._latest_f7: dict | None = None
        self._last_main_telemetry_at: datetime | None = None
        self._latest_ai_prediction = (
            "not_run" if ai_prediction_service.is_available else "unavailable"
        )

        runtime_client_id = f"{settings.mqtt_client_id}-{os.getpid()}"
        self._client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=runtime_client_id,
            protocol=mqtt.MQTTv311,
        )

        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message = self._on_message
        self._client.reconnect_delay_set(min_delay=1, max_delay=30)

        if settings.mqtt_username:
            self._client.username_pw_set(
                username=settings.mqtt_username,
                password=settings.mqtt_password,
            )

        self._client.will_set(
            topic=BACKEND_STATUS_TOPIC,
            payload='{"status":"offline"}',
            qos=1,
            retain=True,
        )

    @property
    def is_connected(self) -> bool:
        return self._connected.is_set()

    @property
    def main_status(self) -> str:
        return self._main_status

    @property
    def f7_status(self) -> str:
        return self._f7_status

    @property
    def system_state(self) -> str:
        return self._system_state

    @property
    def buzzer_state(self) -> str:
        return self._buzzer_state

    @property
    def buzzer_muted(self) -> bool | None:
        return self._buzzer_muted

    @property
    def ai_status(self) -> str:
        return "available" if ai_prediction_service.is_available else "unavailable"

    @property
    def latest_ai_prediction(self) -> str:
        return self._latest_ai_prediction

    @property
    def latest_f7(self) -> dict | None:
        if self._latest_f7 is None:
            return None

        return self._latest_f7.copy()

    def connect(self) -> None:
        if self._loop_started:
            return

        logger.info(
            "Connecting to MQTT broker at %s:%s",
            settings.mqtt_broker_host,
            settings.mqtt_broker_port,
        )
        self._client.connect_async(
            host=settings.mqtt_broker_host,
            port=settings.mqtt_broker_port,
            keepalive=60,
        )
        self._client.loop_start()
        self._loop_started = True

    def disconnect(self) -> None:
        if not self._loop_started:
            return

        if self.is_connected:
            self._client.publish(
                BACKEND_STATUS_TOPIC,
                '{"status":"offline"}',
                qos=1,
                retain=True,
            )
            self._client.disconnect()

        self._client.loop_stop()
        self._connected.clear()
        self._loop_started = False

    def publish(
        self,
        topic: str,
        payload: str,
        qos: int = 1,
        retain: bool = False,
    ) -> None:
        if not self.is_connected:
            raise ConnectionError("Backend chưa kết nối MQTT Broker.")

        publish_result = self._client.publish(
            topic=topic,
            payload=payload,
            qos=qos,
            retain=retain,
        )

        if publish_result.rc != mqtt.MQTT_ERR_SUCCESS:
            raise ConnectionError(
                f"Không thể publish MQTT, mã lỗi: {publish_result.rc}",
            )

    def _on_connect(
        self,
        client: mqtt.Client,
        userdata: object,
        connect_flags: mqtt.ConnectFlags,
        reason_code: mqtt.ReasonCode,
        properties: mqtt.Properties | None,
    ) -> None:
        if reason_code.is_failure:
            self._connected.clear()
            logger.error("MQTT connection failed: %s", reason_code)
            return

        self._connected.set()
        logger.info("MQTT Broker connected successfully.")

        client.subscribe(
            [
                (MAIN_TELEMETRY_TOPIC, 1),
                (MAIN_STATUS_TOPIC, 1),
                (BUZZER_STATE_TOPIC, 1),
                (F7_TELEMETRY_TOPIC, 1),
                (F7_STATUS_TOPIC, 1),
            ],
        )
        client.publish(
            BACKEND_STATUS_TOPIC,
            '{"status":"online"}',
            qos=1,
            retain=True,
        )

    def _on_disconnect(
        self,
        client: mqtt.Client,
        userdata: object,
        disconnect_flags: mqtt.DisconnectFlags,
        reason_code: mqtt.ReasonCode,
        properties: mqtt.Properties | None,
    ) -> None:
        self._connected.clear()
        self._main_status = "unknown"
        self._f7_status = "unknown"
        self._system_state = "unknown"
        self._buzzer_state = "unknown"
        self._buzzer_muted = None

        if reason_code.is_failure:
            logger.warning(
                "MQTT disconnected unexpectedly: %s. Waiting to reconnect.",
                reason_code,
            )
        else:
            logger.info("MQTT disconnected normally.")

    def _save_to_postgresql(self, sensor_record: dict) -> None:
        database = SessionLocal()

        try:
            create_reading(
                database,
                SensorReadingCreate(
                    device_id=str(sensor_record["device_id"]),
                    temperature=float(sensor_record["temperature"]),
                    humidity=float(sensor_record["humidity"]),
                    gas_raw=sensor_record["gas_raw"],
                    gas_filtered=sensor_record["gas_filtered"],
                    distance_cm=sensor_record["distance_cm"],
                    water_level_cm=sensor_record["water_level_cm"],
                    water_level_percent=sensor_record["water_level_percent"],
                    angle_x=sensor_record["motion_roll"],
                    angle_y=sensor_record["motion_pitch"],
                    vibration=sensor_record["motion_vibration"],
                    motion_status=str(sensor_record["motion_status"]),
                    status=str(sensor_record["system_status"]),
                    buzzer=sensor_record["buzzer"],
                    buzzer_muted=sensor_record["buzzer_muted"],
                ),
            )
            logger.info("Đã lưu telemetry vào PostgreSQL.")
        except Exception:
            database.rollback()
            logger.exception("Không thể lưu telemetry vào PostgreSQL.")
        finally:
            database.close()

    def _run_ai_prediction(self, sensor_record: dict) -> None:
        self._latest_ai_prediction = ai_prediction_service.predict(
            temperature=sensor_record["temperature"],
            humidity=sensor_record["humidity"],
            gas_level=sensor_record["gas_filtered"],
            water_level_cm=sensor_record["water_level_cm"],
        )

    def _add_latest_f7_data(self, sensor_record: dict) -> None:
        """Attach the newest F7 values to the combined Main sensor record."""
        if self._latest_f7 is None:
            return

        received_at = self._latest_f7.get("received_at")

        if isinstance(received_at, datetime):
            data_age = datetime.now(timezone.utc) - received_at

            if data_age.total_seconds() > F7_DATA_MAX_AGE_SECONDS:
                return

        sensor_record["motion_roll"] = self._latest_f7.get("roll")
        sensor_record["motion_pitch"] = self._latest_f7.get("pitch")
        sensor_record["motion_tilt"] = self._latest_f7.get("tilt")
        sensor_record["motion_vibration"] = self._latest_f7.get("vibration")
        sensor_record["motion_impact"] = self._latest_f7.get("impact")
        sensor_record["motion_status"] = self._latest_f7.get(
            "status",
            sensor_record["motion_status"],
        )

    def _save_f7_to_postgresql(self, f7_record: dict) -> None:
        database = SessionLocal()

        try:
            create_f7_reading(
                database,
                F7ReadingCreate(
                    device_id=str(f7_record["device_id"]),
                    roll=f7_record["roll"],
                    pitch=f7_record["pitch"],
                    tilt=f7_record["tilt"],
                    vibration=f7_record["vibration"],
                    impact=f7_record["impact"],
                    status=str(f7_record["status"]),
                    recorded_at=f7_record["received_at"],
                ),
            )
            logger.info("Đã lưu telemetry F7 vào PostgreSQL.")
        except Exception:
            database.rollback()
            logger.exception("Không thể lưu telemetry F7 vào PostgreSQL.")
        finally:
            database.close()

    def _handle_main_telemetry(self, telemetry: dict) -> None:
        sensor_record = normalize_main_telemetry(telemetry)
        self._last_main_telemetry_at = sensor_record["timestamp"]

        self._main_status = "online"
        self._system_state = str(sensor_record["system_status"]).lower()

        if sensor_record["buzzer"] is not None:
            self._buzzer_state = "on" if sensor_record["buzzer"] else "off"

        if sensor_record["buzzer_muted"] is not None:
            self._buzzer_muted = bool(sensor_record["buzzer_muted"])

        if telemetry.get("motionSource") == "DIRECT":
            self._latest_f7 = {
                "device_id": "f7-station-01",
                "roll": telemetry.get("motionRoll"),
                "pitch": telemetry.get("motionPitch"),
                "tilt": telemetry.get("motionTilt"),
                "vibration": telemetry.get("motionVibration"),
                "impact": telemetry.get("motionImpact"),
                "status": str(sensor_record["motion_status"]).upper(),
                "received_at": datetime.now(timezone.utc),
            }
            self._f7_status = "direct"
            self._save_f7_to_postgresql(self._latest_f7)

        # Main and F7 publish every 2 seconds. Add the latest F7 values to the
        # same PostgreSQL snapshot and to ThingSpeak Field 7.
        self._add_latest_f7_data(sensor_record)

        self._save_to_postgresql(sensor_record)
        thingspeak_client.upload(sensor_record)
        self._run_ai_prediction(sensor_record)

        system_status = str(sensor_record["system_status"]).upper()
        alert_message = (
            f"ESP32 Main báo {system_status}. "
            f"Nhiệt độ: {sensor_record['temperature']} °C, "
            f"gas: {sensor_record['gas_filtered']}, "
            f"mực nước: {sensor_record['water_level_cm']} cm."
        )
        alert_service.notify_if_needed(
            source="main_station",
            current_status=system_status,
            message=alert_message,
        )

    def _handle_f7_telemetry(self, payload: str) -> None:
        data = json.loads(payload)
        self._latest_f7 = {
            "device_id": data.get("deviceId", "f7-station-01"),
            "roll": data.get("roll"),
            "pitch": data.get("pitch"),
            "tilt": data.get("tilt"),
            "vibration": data.get("vibration"),
            "impact": data.get("impact"),
            "status": data.get("status", "UNKNOWN").upper(),
            "received_at": datetime.now(timezone.utc),
        }
        self._f7_status = "online"

        self._save_f7_to_postgresql(self._latest_f7)

        main_data_is_recent = False

        if self._last_main_telemetry_at is not None:
            main_data_age = datetime.now(timezone.utc) - self._last_main_telemetry_at
            main_data_is_recent = (
                main_data_age.total_seconds() <= F7_DATA_MAX_AGE_SECONDS
            )

        # If Main is offline, F7 still writes one partial row containing only
        # Field 7. The shared ThingSpeak timer still limits uploads to 15 s.
        if not main_data_is_recent:
            thingspeak_client.upload({
                "motion_vibration": self._latest_f7["vibration"],
            })

        f7_status = str(self._latest_f7["status"]).upper()
        alert_message = (
            f"ESP32 F7 báo {f7_status}. "
            f"Độ nghiêng: {self._latest_f7['tilt']}, "
            f"rung: {self._latest_f7['vibration']}, "
            f"va chạm: {self._latest_f7['impact']}."
        )
        alert_service.notify_if_needed(
            source="f7_station",
            current_status=f7_status,
            message=alert_message,
        )

    def _on_message(
        self,
        client: mqtt.Client,
        userdata: object,
        message: mqtt.MQTTMessage,
    ) -> None:
        try:
            payload = message.payload.decode("utf-8")
        except UnicodeDecodeError:
            logger.warning("MQTT payload on %s is not valid UTF-8.", message.topic)
            return

        logger.info("MQTT received: topic=%s payload=%s", message.topic, payload)

        if message.topic == MAIN_STATUS_TOPIC:
            self._main_status = payload.strip().lower()
            return

        if message.topic == F7_STATUS_TOPIC:
            self._f7_status = payload.strip().lower()
            return

        if message.topic == BUZZER_STATE_TOPIC:
            self._buzzer_state = payload.strip().lower()
            return

        if message.topic == F7_TELEMETRY_TOPIC:
            try:
                self._handle_f7_telemetry(payload)
            except (json.JSONDecodeError, TypeError, ValueError) as error:
                logger.warning("F7 telemetry không hợp lệ: %s", error)
            return

        if message.topic == MAIN_TELEMETRY_TOPIC:
            try:
                telemetry = json.loads(payload)

                if not isinstance(telemetry, dict):
                    raise ValueError("Main telemetry phải là một JSON object.")

                self._handle_main_telemetry(telemetry)
            except (json.JSONDecodeError, TypeError, ValueError) as error:
                logger.warning("Main telemetry không hợp lệ: %s", error)
            except Exception:
                logger.exception("Lỗi không mong đợi khi xử lý Main telemetry.")


mqtt_client = MQTTClient()
