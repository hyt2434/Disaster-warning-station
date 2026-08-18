import sys
from pathlib import Path

import joblib
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier


PROJECT_ROOT = Path(__file__).resolve().parent.parent
MODEL_FILE = PROJECT_ROOT / "backend" / "app" / "ml_models" / "disaster_model.pkl"

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

print("[INFO] Đang khởi tạo dữ liệu giả lập (Synthetic Data)...")

# 1. TẠO DỮ LIỆU GIẢ LẬP (Mô phỏng 4000 trường hợp)
np.random.seed(42)
n_samples = 4000

df = pd.DataFrame({
    "temperature": np.random.uniform(20, 55, n_samples),
    "humidity": np.random.uniform(30, 95, n_samples),
    "gas_filtered": np.random.uniform(900, 2000, n_samples),
    "water_danger": np.random.choice([0, 1], size=n_samples, p=[0.85, 0.15]),
})

# Nhãn 1 khi có ít nhất một điều kiện nguy hiểm giống firmware.
df["label"] = (
    (df["temperature"] >= 40)
    | (df["gas_filtered"] >= 1600)
    | (df["water_danger"] == 1)
).astype(int)

print("[INFO] Đang tiến hành huấn luyện mô hình Random Forest...")
# 2. HUẤN LUYỆN MÔ HÌNH
model = RandomForestClassifier(n_estimators=50, max_depth=5, random_state=42)
features = df[["temperature", "humidity", "gas_filtered", "water_danger"]]
feature_values = features.to_numpy()
model.fit(feature_values, df["label"])

print(f"[OK] Huấn luyện thành công! Độ chính xác trên tập mẫu: {model.score(feature_values, df['label']) * 100:.2f}%")

# 3. LƯU MÔ HÌNH VÀO ĐÚNG NƠI BACKEND SẼ ĐỌC
MODEL_FILE.parent.mkdir(parents=True, exist_ok=True)
joblib.dump(model, MODEL_FILE)
print(f"[OK] Đã lưu mô hình AI tại: {MODEL_FILE}")
