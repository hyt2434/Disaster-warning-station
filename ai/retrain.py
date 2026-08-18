import os
import sys
from pathlib import Path

import httpx
import joblib
import pandas as pd
from dotenv import load_dotenv
from sklearn.ensemble import RandomForestClassifier


PROJECT_ROOT = Path(__file__).resolve().parent.parent
ENV_FILE = PROJECT_ROOT / ".env"
MODEL_FILE = PROJECT_ROOT / "backend" / "app" / "ml_models" / "disaster_model.pkl"

MINIMUM_TRAINING_SAMPLES = 100
MAXIMUM_CLOUD_RECORDS = 8000
GAS_DANGER_THRESHOLD = 1600
WATER_DANGER_LEVEL_CM = 70

load_dotenv(ENV_FILE)

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")


def download_thingspeak_records() -> list[dict]:
    """Download historical fields from one ThingSpeak channel."""
    channel_id = os.getenv("THINGSPEAK_CHANNEL_ID", "").strip()
    read_api_key = os.getenv("THINGSPEAK_READ_API_KEY", "").strip()

    if not channel_id:
        raise RuntimeError("Thiếu THINGSPEAK_CHANNEL_ID trong file .env")

    url = f"https://api.thingspeak.com/channels/{channel_id}/feeds.json"
    parameters = {"results": MAXIMUM_CLOUD_RECORDS}

    if read_api_key:
        parameters["api_key"] = read_api_key

    response = httpx.get(url, params=parameters, timeout=15)
    response.raise_for_status()
    response_data = response.json()

    records = response_data.get("feeds", [])

    if not isinstance(records, list):
        raise ValueError("ThingSpeak không trả về danh sách feeds hợp lệ.")

    return records


def prepare_training_data(records: list[dict]) -> tuple[pd.DataFrame, pd.Series]:
    """Convert ThingSpeak field1-field4 to the model's sensor features."""
    data_frame = pd.DataFrame(records)
    required_fields = ["field1", "field2", "field3", "field4"]

    missing_fields = [
        field_name
        for field_name in required_fields
        if field_name not in data_frame.columns
    ]

    if missing_fields:
        missing_text = ", ".join(missing_fields)
        raise ValueError(f"ThingSpeak đang thiếu các field: {missing_text}")

    data_frame = data_frame.rename(
        columns={
            "field1": "temperature",
            "field2": "humidity",
            "field3": "gas_filtered",
            "field4": "water_level_cm",
        }
    )

    sensor_columns = [
        "temperature",
        "humidity",
        "gas_filtered",
        "water_level_cm",
    ]

    for column_name in sensor_columns:
        data_frame[column_name] = pd.to_numeric(
            data_frame[column_name],
            errors="coerce",
        )

    # Null means the sensor value is unknown, so do not replace it with zero.
    data_frame = data_frame.dropna(subset=sensor_columns).copy()

    if len(data_frame) < MINIMUM_TRAINING_SAMPLES:
        raise ValueError(
            "Không đủ dữ liệu hợp lệ để huấn luyện "
            f"(cần {MINIMUM_TRAINING_SAMPLES}, hiện có {len(data_frame)})."
        )

    # Field 5 and Field 6 are statuses already calculated by the devices.
    # Do not use them as AI inputs because Field 6 already contains the answer.
    temperature_is_dangerous = data_frame["temperature"] >= 40
    gas_is_dangerous = data_frame["gas_filtered"] >= GAS_DANGER_THRESHOLD
    water_is_dangerous = data_frame["water_level_cm"] >= WATER_DANGER_LEVEL_CM

    labels = (
        temperature_is_dangerous
        | gas_is_dangerous
        | water_is_dangerous
    ).astype(int)

    data_frame["water_danger"] = water_is_dangerous.astype(int)
    features = data_frame[
        ["temperature", "humidity", "gas_filtered", "water_danger"]
    ]

    return features, labels


def retrain_model_from_cloud() -> None:
    print("[INFO] Đang tải dữ liệu từ ThingSpeak...")
    records = download_thingspeak_records()

    if len(records) < MINIMUM_TRAINING_SAMPLES:
        print(
            "[WARNING] ThingSpeak chưa đủ dữ liệu để huấn luyện "
            f"(cần {MINIMUM_TRAINING_SAMPLES}, hiện có {len(records)})."
        )
        return

    features, labels = prepare_training_data(records)

    print(f"[INFO] Đang huấn luyện mô hình với {len(features)} mẫu hợp lệ...")
    model = RandomForestClassifier(
        n_estimators=50,
        max_depth=5,
        random_state=42,
    )
    model.fit(features.to_numpy(), labels)

    MODEL_FILE.parent.mkdir(parents=True, exist_ok=True)
    joblib.dump(model, MODEL_FILE)
    print(f"[OK] Đã lưu mô hình AI tại: {MODEL_FILE}")


def main() -> None:
    try:
        retrain_model_from_cloud()
    except RuntimeError as error:
        print(f"[ERROR] Lỗi cấu hình: {error}")
        raise SystemExit(1) from error
    except ValueError as error:
        print(f"[ERROR] Dữ liệu chưa thể dùng để huấn luyện: {error}")
        raise SystemExit(1) from error
    except httpx.HTTPError as error:
        print("[ERROR] Không thể tải dữ liệu từ ThingSpeak.")
        print("Hãy kiểm tra Channel ID, Read API Key và kết nối Internet.")
        print(f"Chi tiết: {error}")
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
