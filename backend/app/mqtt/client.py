import logging
import threading

import requests

import os          
import joblib      
import numpy as np

import json
from datetime import datetime, timezone

import paho.mqtt.client as mqtt

from ..config import settings
from ..database import SessionLocal
from ..database.repository import create_reading
from ..schemas import SensorReadingCreate
from .topics import (
    BACKEND_STATUS_TOPIC,
    MAIN_STATUS_TOPIC,
    MAIN_TELEMETRY_TOPIC,
    COMMAND_BUZZER_TOPIC,
    F7_STATUS_TOPIC,
    F7_TELEMETRY_TOPIC,
)


logger = logging.getLogger("uvicorn.error")

MODEL_PATH = Path(__file__).resolve().parent.parent / "ml_models" / "disaster_model.pkl"
WATER_DANGER_DISTANCE_CM = 30.0
WATER_DANGER_LEVEL_CM = 70.0

try:
    ai_model = joblib.load(MODEL_PATH)
    logger.info("🧠 Đã tải thành công 'Bộ não' AI vào Backend!")
except Exception as e:
    logger.error("❌ Không tìm thấy file mô hình AI: %s", e)
    ai_model = None


class MQTTClient:
    def __init__(self) -> None:
        self._connected = threading.Event()
        self._loop_started = False
        self._main_status = "unknown"
        self._f7_status = "unknown"
        self._buzzer_state = "unknown"
        self._latest_f7: dict | None = None
        self._latest_ai_prediction = "not_run" if ai_model is not None else "unavailable"

        # PID giúp mỗi backend local có Client ID riêng, tránh "session taken over".
        self._runtime_client_id = f"{settings.mqtt_client_id}-{os.getpid()}"

        self._client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=settings.mqtt_client_id,
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
        self._buzzer_state = "unknown"

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
        if message.topic == MAIN_TELEMETRY_TOPIC:
            try:
                # Chuyển đổi payload string thành dictionary
                data = json.loads(payload)
                self._main_status = "online"

                if isinstance(data.get("buzzer"), bool):
                    self._buzzer_state = "on" if data["buzzer"] else "off"

                # Khi F7 mất Wi-Fi gia đình, nó gửi dữ liệu trực tiếp đến ESP32 Main.
                # Main chuyển tiếp các giá trị này trong telemetry của chính nó.
                if data.get("motionSource") == "DIRECT":
                    self._latest_f7 = {
                        "device_id": "f7-station-01",
                        "roll": None,
                        "pitch": None,
                        "tilt": data.get("motionTilt"),
                        "vibration": data.get("motionVibration"),
                        "impact": data.get("motionImpact"),
                        "status": data.get("motionStatus", "UNKNOWN").upper(),
                        "received_at": datetime.now(timezone.utc),
                    }
                    self._f7_status = "direct"
                
                # Đọc payload mới; vẫn hỗ trợ tên cũ để demo không bị gián đoạn.
                gas_raw = data.get("gasRaw", data.get("gas", data.get("smoke", 0)))
                gas_filtered = data.get("gasFiltered", gas_raw)
                distance_cm = data.get("distanceCm")
                water_level_cm = data.get(
                    "waterLevelCm",
                    data.get("waterLevel", data.get("water", 0.0)),
                )
                water_level_percent = data.get("waterLevelPercent")

                sensor_record = {
                    "timestamp": datetime.now(timezone.utc),
                    "device_id": data.get("deviceId", "main-station-01"),
                    "temperature": data.get("temperature", 0.0),
                    "humidity": data.get("humidity", 0.0),
                    "gas_raw": gas_raw,
                    "gas_filtered": gas_filtered,
                    "distance_cm": distance_cm,
                    "water_level_cm": water_level_cm,
                    "water_level_percent": water_level_percent,
                    "motion_status": data.get("motionStatus", "NORMAL"),
                    "motion_source": data.get("motionSource", "NONE"),
                    "motion_tilt": data.get("motionTilt"),
                    "motion_vibration": data.get("motionVibration"),
                    "motion_impact": data.get("motionImpact"),
                    "system_status": data.get("systemStatus", "NORMAL"),
                }

                # PostgreSQL là nguồn dữ liệu mà REST API/dashboard đang đọc.
                database = SessionLocal()
                try:
                    system_status = sensor_record["system_status"].upper()
                    if system_status == "SAFE":
                        system_status = "NORMAL"

                    create_reading(
                        database,
                        SensorReadingCreate(
                            device_id=sensor_record["device_id"],
                            temperature=float(sensor_record["temperature"]),
                            humidity=float(sensor_record["humidity"]),
                            gas_raw=sensor_record["gas_raw"],
                            gas_filtered=sensor_record["gas_filtered"],
                            distance_cm=sensor_record["distance_cm"],
                            water_level_cm=sensor_record["water_level_cm"],
                            water_level_percent=sensor_record["water_level_percent"],
                            vibration=sensor_record["motion_vibration"],
                            status=system_status,
                        ),
                    )
                    logger.info("Đã lưu telemetry MQTT vào PostgreSQL.")
                except Exception as database_error:
                    database.rollback()
                    logger.error(
                        "Không thể lưu telemetry MQTT vào PostgreSQL: %s",
                        database_error,
                    )
                finally:
                    database.close()
                
                # MongoDB là Cloud storage của hệ thống. Khi Atlas tạm mất,
                # store giữ record trong RAM và tự đẩy bù sau khi reconnect.
                if mongo_store.store(sensor_record):
                    logger.info("Đã lưu telemetry lên MongoDB Cloud.")
                else:
                    logger.warning("Telemetry MongoDB đang chờ đồng bộ.")
            
                if ai_model is not None:
                    # 1. Đưa số liệu vào mảng theo đúng thứ tự lúc train: [Nhiệt độ, Độ ẩm, Khói, Nước]
                    # Model hiện được train với water là cờ 0/1.
                    if distance_cm is not None:
                        water_danger = (
                            1.0
                            if float(distance_cm) <= WATER_DANGER_DISTANCE_CM
                            else 0.0
                        )
                    elif water_level_cm is not None:
                        # Hỗ trợ bản ghi cũ chưa có distance_cm.
                        water_danger = (
                            1.0
                            if float(water_level_cm) >= WATER_DANGER_LEVEL_CM
                            else 0.0
                        )
                    else:
                        water_danger = 0.0

                    input_features = np.array([[
                        sensor_record["temperature"],
                        sensor_record["humidity"],
                        sensor_record["gas_filtered"],
                        water_danger
                    ]])
                    
                    # 2. AI đưa ra phán đoán (0 là An toàn, 1 là Nguy hiểm)
                    prediction = ai_model.predict(input_features)
                    
                    # 3. Ra quyết định
                    if prediction[0] == 1.0:
                        self._latest_ai_prediction = "danger"
                        logger.warning("🚨 AI CẢNH BÁO NGUY HIỂM! Chuẩn bị bật còi báo động!")
                        # Publish lệnh ON xuống Topic của ESP32 để kích hoạt còi
                        client.publish(
                            topic=COMMAND_BUZZER_TOPIC,
                            payload="ON",
                            qos=1
                        )
                    else:
                        self._latest_ai_prediction = "safe"
                        logger.info("✅ AI đánh giá: Môi trường an toàn.")

            except json.JSONDecodeError:
                logger.error("❌ Dữ liệu telemetry không phải là định dạng JSON hợp lệ.")
            except Exception as error:
                logger.error("Lỗi khi xử lý telemetry MQTT: %s", error)

mqtt_client = MQTTClient()
