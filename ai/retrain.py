import os
import sys
from pathlib import Path

import joblib
import pandas as pd
from dotenv import load_dotenv
from pymongo import ASCENDING, MongoClient
from pymongo.collection import Collection
from pymongo.errors import ConfigurationError, PyMongoError
from sklearn.ensemble import RandomForestClassifier


PROJECT_ROOT = Path(__file__).resolve().parent.parent
ENV_FILE = PROJECT_ROOT / ".env"
MODEL_FILE = PROJECT_ROOT / "backend" / "app" / "ml_models" / "disaster_model.pkl"
REQUIRED_COLUMNS = ["temperature", "humidity", "smoke_level", "water_level"]
MINIMUM_TRAINING_SAMPLES = 100

load_dotenv(ENV_FILE)

# Bảo đảm thông báo tiếng Việt hiển thị đúng trong PowerShell trên Windows.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")


def connect_to_cloud_collection() -> tuple[MongoClient, Collection]:
    mongo_uri = os.getenv("MONGODB_URI", "").strip()
    database_name = os.getenv("MONGODB_DATABASE").strip()
    collection_name = os.getenv("MONGODB_COLLECTION", "sensor_readings").strip()

    if not mongo_uri:
        raise RuntimeError("Thiếu MONGODB_URI trong file .env")

    client = MongoClient(
        mongo_uri,
        serverSelectionTimeoutMS=5000,
        connectTimeoutMS=5000,
        socketTimeoutMS=10000,
        retryWrites=True,
    )

    try:
        client.admin.command("ping")
        database = client[database_name]

        if collection_name not in database.list_collection_names():
            database.create_collection(collection_name)
            print(f"[OK] Đã tạo collection mới: {database_name}.{collection_name}")

        collection = database[collection_name]
        collection.create_index([("timestamp", ASCENDING)])

        print(f"[OK] Đã kết nối MongoDB Atlas: {database_name}.{collection_name}")
        return client, collection
    except Exception:
        client.close()
        raise


def prepare_training_data(records: list[dict]) -> tuple[pd.DataFrame, pd.Series]:
    data_frame = pd.DataFrame(records)

    missing_columns = [
        column_name
        for column_name in REQUIRED_COLUMNS
        if column_name not in data_frame.columns
    ]
    if missing_columns:
        missing_text = ", ".join(missing_columns)
        raise ValueError(f"Dữ liệu MongoDB đang thiếu các cột: {missing_text}")

    for column_name in REQUIRED_COLUMNS:
        data_frame[column_name] = pd.to_numeric(
            data_frame[column_name],
            errors="coerce",
        )

    data_frame = data_frame.dropna(subset=REQUIRED_COLUMNS).copy()

    if len(data_frame) < MINIMUM_TRAINING_SAMPLES:
        raise ValueError(
            "Không đủ bản ghi hợp lệ để huấn luyện "
            f"(cần {MINIMUM_TRAINING_SAMPLES}, hiện có {len(data_frame)})."
        )

    temperature_is_dangerous = data_frame["temperature"] > 50
    smoke_is_dangerous = data_frame["smoke_level"] > 700
    water_is_dangerous = data_frame["water_level"] >= 40

    labels = (
        temperature_is_dangerous
        | smoke_is_dangerous
        | water_is_dangerous
    ).astype(int)

    # Backend dự đoán nước theo cờ nhị phân: 0 là an toàn, 1 là nguy hiểm.
    data_frame["water_danger"] = water_is_dangerous.astype(int)

    features = data_frame[
        ["temperature", "humidity", "smoke_level", "water_danger"]
    ]
    return features, labels


def retrain_model_from_cloud() -> None:
    client = None

    try:
        client, collection = connect_to_cloud_collection()

        print("[INFO] Đang tải dữ liệu thực tế từ MongoDB Atlas...")
        records = list(collection.find({}, {"_id": 0, "timestamp": 0}))

        if len(records) < MINIMUM_TRAINING_SAMPLES:
            print(
                "[WARNING] Collection đã sẵn sàng nhưng chưa đủ dữ liệu để huấn luyện "
                f"(cần {MINIMUM_TRAINING_SAMPLES}, hiện có {len(records)})."
            )
            print("Hãy chạy backend và ESP32 để telemetry được lưu lên MongoDB.")
            return

        features, labels = prepare_training_data(records)

        print(f"[INFO] Đang huấn luyện mô hình với {len(features)} mẫu hợp lệ...")
        model = RandomForestClassifier(
            n_estimators=50,
            max_depth=5,
            random_state=42,
        )
        model.fit(features, labels)

        MODEL_FILE.parent.mkdir(parents=True, exist_ok=True)
        joblib.dump(model, MODEL_FILE)
        print(f"[OK] Đã lưu mô hình AI tại: {MODEL_FILE}")
    finally:
        if client is not None:
            client.close()


def main() -> None:
    try:
        retrain_model_from_cloud()
    except RuntimeError as error:
        print(f"[ERROR] Lỗi cấu hình: {error}")
        raise SystemExit(1) from error
    except ValueError as error:
        print(f"[ERROR] Dữ liệu chưa thể dùng để huấn luyện: {error}")
        raise SystemExit(1) from error
    except ConfigurationError as error:
        print("[ERROR] MongoDB Atlas URI không hợp lệ hoặc hostname cluster không tồn tại.")
        print("Hãy vào Atlas > Connect > Drivers và sao chép lại connection string.")
        print(f"Chi tiết: {error}")
        raise SystemExit(1) from error
    except PyMongoError as error:
        print("[ERROR] Không thể kết nối hoặc thao tác với MongoDB Atlas.")
        print("Hãy kiểm tra Network Access, Database User và quyền readWrite.")
        print(f"Chi tiết: {error}")
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
