import logging
import threading

import os          
import joblib      
import numpy as np

import json
from datetime import datetime, timezone

import paho.mqtt.client as mqtt

from ..config import settings
from ..database.mongodb import collection
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
                
                # Tạo bản ghi với Time-Series tiêu chuẩn (UTC)
                sensor_record = {
                    "timestamp": datetime.now(timezone.utc),
                    "temperature": data.get("temperature", 0.0),
                    "humidity": data.get("humidity", 0.0),
                    "smoke_level": data.get("smoke", 0.0),
                    "water_level": data.get("water", 0.0)
                }
                
                # Thực hiện lệnh ghi vào Cloud
                collection.insert_one(sensor_record)
                logger.info("✅ Đã lưu dữ liệu telemetry lên MongoDB Cloud thành công!")
            
                if ai_model is not None:
                    # 1. Đưa số liệu vào mảng theo đúng thứ tự lúc train: [Nhiệt độ, Độ ẩm, Khói, Nước]
                    input_features = np.array([[
                        sensor_record["temperature"],
                        sensor_record["humidity"],
                        sensor_record["smoke_level"],
                        sensor_record["water_level"]
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
            except Exception as e:
                logger.error("❌ Lỗi hệ thống khi lưu vào MongoDB: %s", e)

mqtt_client = MQTTClient()