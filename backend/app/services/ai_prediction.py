import logging
from pathlib import Path

import joblib
import numpy as np


logger = logging.getLogger("uvicorn.error")

MODEL_PATH = Path(__file__).resolve().parent.parent / "ml_models" / "disaster_model.pkl"
WATER_DANGER_LEVEL_CM = 70.0


class AIPredictionService:
    """Load the trained model once and use it for current or future sensor data."""

    def __init__(self) -> None:
        self._model = None
        self._load_model()

    @property
    def is_available(self) -> bool:
        return self._model is not None

    def _load_model(self) -> None:
        try:
            self._model = joblib.load(MODEL_PATH)
            logger.info("Đã tải mô hình AI từ: %s", MODEL_PATH)
        except FileNotFoundError:
            logger.error("Không tìm thấy file mô hình AI tại: %s", MODEL_PATH)
            logger.error("Hãy chạy: python ai/train.py hoặc python ai/retrain.py")
        except Exception as error:
            logger.error("Không thể tải mô hình AI tại %s: %s", MODEL_PATH, error)

    def predict(
        self,
        temperature: float | None,
        humidity: float | None,
        gas_level: float | None,
        water_level_cm: float | None,
    ) -> str:
        """Return danger, safe, insufficient_data, unavailable or not_run."""
        if self._model is None:
            return "unavailable"

        if (
            temperature is None
            or humidity is None
            or gas_level is None
            or water_level_cm is None
        ):
            return "insufficient_data"

        water_is_dangerous = water_level_cm >= WATER_DANGER_LEVEL_CM
        model_input = np.array([[
            float(temperature),
            float(humidity),
            float(gas_level),
            1.0 if water_is_dangerous else 0.0,
        ]])

        try:
            prediction = self._model.predict(model_input)
            return "danger" if prediction[0] == 1 else "safe"
        except Exception:
            logger.exception("Không thể chạy dự đoán bằng mô hình AI.")
            return "not_run"


ai_prediction_service = AIPredictionService()
