import json
import logging
import os
import threading
from datetime import datetime, timezone
from pathlib import Path

import joblib
import numpy as np
import paho.mqtt.client as mqtt

from ..config import settings
from ..database import SessionLocal
from ..database.mongodb import mongo_store
from ..database.repository import create_reading
from ..schemas import SensorReadingCreate
from .topics import (
    BACKEND_STATUS_TOPIC,
    BUZZER_STATE_TOPIC,
    MAIN_STATUS_TOPIC,
    MAIN_TELEMETRY_TOPIC,
    F7_STATUS_TOPIC,
    F7_TELEMETRY_TOPIC,
)


logger = logging.getLogger("uvicorn.error")

MODEL_PATH = Path(__file__).resolve().parent.parent / "ml_models" / "disaster_model.pkl"
SENSOR_HEIGHT_CM = 100.0
WATER_DANGER_DISTANCE_CM = 30.0
WATER_DANGER_LEVEL_CM = 70.0


def first_available_value(
    telemetry: dict,
    field_names: list[str],
    default: object = None,
) -> object:
    """Read the first field that exists, including fields whose value is null."""
    for field_name in field_names:
        if field_name in telemetry:
            return telemetry[field_name]

    return default


def normalize_main_telemetry(
    telemetry: dict,
    received_at: datetime | None = None,
) -> dict:
    """Convert ESP32 camelCase fields to the backend's snake_case names."""
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


try:
    ai_model = joblib.load(MODEL_PATH)
    logger.info("🧠 Đã tải mô hình AI vào backend từ: %s", MODEL_PATH)
except FileNotFoundError:
    logger.error("❌ Không tìm thấy file mô hình AI tại: %s", MODEL_PATH)
    logger.error("Hãy chạy: python ai/train.py")
    ai_model = None
except Exception as error:
    logger.error("❌ Không thể tải mô hình AI tại %s: %s", MODEL_PATH, error)
    ai_model = None


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
        self._latest_ai_prediction = "not_run" if ai_model is not None else "unavailable"

        # PID giúp mỗi backend local có Client ID riêng, tránh "session taken over".
        self._runtime_client_id = f"{settings.mqtt_client_id}-{os.getpid()}"

        self._client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=self._runtime_client_id,
            protocol=mqtt.MQTTv311,
        )

        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message = self._on_message

        # Thời gian thử kết nối lại tăng dần từ 1 đến tối đa 30 giây.
        self._client.reconnect_delay_set(
            min_delay=1,
            max_delay=30,
        )

        if settings.mqtt_username:
            self._client.username_pw_set(
                username=settings.mqtt_username,
                password=settings.mqtt_password,
            )

        # Broker tự phát trạng thái offline nếu backend mất kết nối bất ngờ.
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
    def buzzer_state(self) -> str:
        return self._buzzer_state

    @property
    def system_state(self) -> str:
        return self._system_state

    @property
    def buzzer_muted(self) -> bool | None:
        return self._buzzer_muted

    @property
    def ai_status(self) -> str:
        return "available" if ai_model is not None else "unavailable"

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
            "Connecting to MQTT broker at %s:%s with client ID %s",
            settings.mqtt_broker_host,
            settings.mqtt_broker_port,
            self._runtime_client_id,
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

    def _save_main_telemetry_to_postgresql(self, sensor_record: dict) -> None:
        """Save one normalized telemetry record without affecting MQTT control."""
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
                    vibration=sensor_record["motion_vibration"],
                    motion_status=str(sensor_record["motion_status"]),
                    status=str(sensor_record["system_status"]),
                    buzzer=sensor_record["buzzer"],
                    buzzer_muted=sensor_record["buzzer_muted"],
                ),
            )
            logger.info("Đã lưu telemetry MQTT vào PostgreSQL.")
        except Exception:
            database.rollback()
            logger.exception("Không thể lưu telemetry MQTT vào PostgreSQL.")
        finally:
            database.close()

    def _save_main_telemetry_to_mongodb(self, sensor_record: dict) -> None:
        """Store Cloud telemetry independently from PostgreSQL."""
        try:
            if mongo_store.store(sensor_record):
                logger.info("Đã lưu telemetry lên MongoDB Cloud.")
            else:
                logger.warning("Telemetry MongoDB đang chờ đồng bộ.")
        except Exception:
            logger.exception("Không thể xử lý telemetry cho MongoDB Cloud.")

    def _run_ai_prediction(self, sensor_record: dict) -> None:
        """Predict environmental risk; never control the buzzer from here."""
        if ai_model is None:
            return

        distance_cm = sensor_record["distance_cm"]
        water_level_cm = sensor_record["water_level_cm"]

        required_sensor_values = [
            sensor_record["temperature"],
            sensor_record["humidity"],
            sensor_record["gas_filtered"],
        ]

        if any(value is None for value in required_sensor_values):
            self._latest_ai_prediction = "insufficient_data"
            logger.info("AI bỏ qua bản ghi vì thiếu dữ liệu nhiệt độ, độ ẩm hoặc gas.")
            return

        if distance_cm is None and water_level_cm is None:
            self._latest_ai_prediction = "insufficient_data"
            logger.info("AI bỏ qua bản ghi vì cảm biến mực nước không có dữ liệu hợp lệ.")
            return

        if distance_cm is not None:
            water_is_dangerous = float(distance_cm) <= WATER_DANGER_DISTANCE_CM
        else:
            water_is_dangerous = float(water_level_cm) >= WATER_DANGER_LEVEL_CM

        model_input = np.array(
            [[
                float(sensor_record["temperature"]),
                float(sensor_record["humidity"]),
                float(sensor_record["gas_filtered"]),
                1.0 if water_is_dangerous else 0.0,
            ]]
        )

        try:
            prediction = ai_model.predict(model_input)
            self._latest_ai_prediction = "danger" if prediction[0] == 1 else "safe"
        except Exception:
            self._latest_ai_prediction = "not_run"
            logger.exception("Không thể chạy dự đoán AI cho telemetry mới.")

    def _handle_main_telemetry(self, telemetry: dict) -> None:
        """Update state, persist telemetry and run AI as separate steps."""
        sensor_record = normalize_main_telemetry(telemetry)

        self._main_status = "online"
        self._system_state = str(sensor_record["system_status"]).lower()

        if sensor_record["buzzer"] is not None:
            self._buzzer_state = "on" if sensor_record["buzzer"] else "off"

        if sensor_record["buzzer_muted"] is not None:
            self._buzzer_muted = bool(sensor_record["buzzer_muted"])

        # Keep compatibility with the older Main firmware that forwarded
        # detailed F7 values while the C3 node used the direct UDP fallback.
        if telemetry.get("motionSource") == "DIRECT":
            self._latest_f7 = {
                "device_id": "f7-station-01",
                "roll": None,
                "pitch": None,
                "tilt": telemetry.get("motionTilt"),
                "vibration": telemetry.get("motionVibration"),
                "impact": telemetry.get("motionImpact"),
                "status": str(sensor_record["motion_status"]).upper(),
                "received_at": datetime.now(timezone.utc),
            }
            self._f7_status = "direct"

        self._save_main_telemetry_to_postgresql(sensor_record)
        self._save_main_telemetry_to_mongodb(sensor_record)
        self._run_ai_prediction(sensor_record)

    def _on_message(
        self,
        client: mqtt.Client,
        userdata: object,
        message: mqtt.MQTTMessage,
    ) -> None:
        try:
            payload = message.payload.decode("utf-8")
        except UnicodeDecodeError:
            logger.warning(
                "MQTT message on %s is not valid UTF-8.",
                message.topic,
            )
            return

        logger.info(
            "MQTT message received: topic=%s payload=%s",
            message.topic,
            payload,
        )

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
            except (json.JSONDecodeError, TypeError, ValueError) as error:
                logger.warning("F7 telemetry không hợp lệ: %s", error)
            return

        if message.topic == MAIN_TELEMETRY_TOPIC:
            try:
                telemetry = json.loads(payload)

                if not isinstance(telemetry, dict):
                    raise ValueError("Telemetry Main phải là một JSON object.")

                self._handle_main_telemetry(telemetry)
            except (json.JSONDecodeError, TypeError, ValueError) as error:
                logger.warning("Telemetry Main không hợp lệ: %s", error)
            except Exception:
                logger.exception("Lỗi không mong đợi khi xử lý telemetry Main.")

            return


mqtt_client = MQTTClient()
