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
)


logger = logging.getLogger("uvicorn.error")

MODEL_PATH = os.path.join(os.path.dirname(__file__), "..", "ml_models", "disaster_model.pkl")
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

        if reason_code.is_failure:
            logger.warning(
                "MQTT disconnected unexpectedly: %s. Waiting to reconnect.",
                reason_code,
            )
        else:
            logger.info("MQTT disconnected normally.")

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
                
                # Tạo bản ghi vớgi Time-Series tiêu chuẩn (UTC)
                gas_value = data.get("gas", data.get("smoke", 0.0))
                water_level = data.get("waterLevel", data.get("water", 0.0))

                sensor_record = {
                    "timestamp": datetime.now(timezone.utc),
                    "device_id": data.get("deviceId", "main-station-01"),
                    "temperature": data.get("temperature", 0.0),
                    "humidity": data.get("humidity", 0.0),
                    "smoke_level": gas_value,
                    "water_level": water_level,
                    "motion_status": data.get("motionStatus", "NORMAL"),
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
                            gas_raw=int(sensor_record["smoke_level"]),
                            water_level_cm=float(sensor_record["water_level"]),
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

                THINGSPEAK_URL = "https://api.thingspeak.com/update.json"
                
                # Ánh xạ trạng thái chữ thành số nguyên để ThingSpeak vẽ biểu đồ
                status_map = {"NORMAL": 0, "SAFE": 0, "WARNING": 1, "DANGER": 2}
                mot_status_num = status_map.get(sensor_record["motion_status"].upper(), 0)
                sys_status_num = status_map.get(sensor_record["system_status"].upper(), 0)

                ts_payload = {
                    "api_key": "66B5A8QBEOB5FQUK", # Write API Key của bạn
                    "field1": sensor_record["temperature"],
                    "field2": sensor_record["humidity"],
                    "field3": sensor_record["smoke_level"],
                    "field4": sensor_record["water_level"],
                    "field5": mot_status_num,
                    "field6": sys_status_num
                }

                try:
                    ts_response = requests.post(THINGSPEAK_URL, data=ts_payload, timeout=5)
                    # ThingSpeak trả về "0" nếu bị chặn do gọi quá nhanh (Rate limit 15s)
                    if ts_response.status_code == 200 and ts_response.text != "0":
                        logger.info("☁️ Đã đồng bộ dữ liệu lên ThingSpeak Cloud thành công!")
                    else:
                        logger.warning("⚠️ ThingSpeak từ chối dữ liệu (có thể do gửi quá nhanh, giới hạn 15s/lần).")
                except requests.exceptions.RequestException as e:
                    logger.error("❌ Không thể kết nối tới ThingSpeak Cloud: %s", e)

                  # 1. Đưa số liệu vào mảng theo đúng thứ tự lúc train: [Nhiệt độ, Độ ẩm, Khói, Nước]
                    # Model hiện được train với water là cờ 0/1.
                if ai_model is not None:
                    water_danger = 1.0 if water_level >= 40.0 else 0.0

                    input_features = np.array([[
                        sensor_record["temperature"],
                        sensor_record["humidity"],
                        sensor_record["smoke_level"],
                        water_danger
                    ]])
                    
                    # 2. AI đưa ra phán đoán (0 là An toàn, 1 là Nguy hiểm)
                    prediction = ai_model.predict(input_features)
                    
                    # 3. Ra quyết định
                    if prediction[0] == 1.0:
                        logger.warning("🚨 AI CẢNH BÁO NGUY HIỂM! Chuẩn bị bật còi báo động!")
                        # Publish lệnh ON xuống Topic của ESP32 để kích hoạt còi
                        client.publish(
                            topic=COMMAND_BUZZER_TOPIC,
                            payload="ON",
                            qos=1
                        )
                    else:
                        logger.info("✅ AI đánh giá: Môi trường an toàn.")

            except json.JSONDecodeError:
                logger.error("❌ Dữ liệu telemetry không phải là định dạng JSON hợp lệ.")
            except Exception as error:
                logger.error("Lỗi khi xử lý telemetry MQTT: %s", error)

mqtt_client = MQTTClient()
