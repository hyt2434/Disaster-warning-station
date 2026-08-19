import sys
from pathlib import Path

import joblib
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split


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
    "gas_average": np.random.uniform(900, 2000, n_samples),
    "water_level_cm": np.random.uniform(0, 77, n_samples),
})

# Nhãn 1 khi có ít nhất một điều kiện nguy hiểm giống firmware.
df["label"] = (
    (df["temperature"] >= 40)
    | (df["gas_average"] >= 1600)
    | (df["water_level_cm"] >= 70)
).astype(int)

print("[INFO] Đang tiến hành huấn luyện mô hình Random Forest...")
# 2. HUẤN LUYỆN MÔ HÌNH
model = RandomForestClassifier(n_estimators=50, max_depth=5, random_state=42)
features = df[["temperature", "gas_average", "water_level_cm"]]
train_features, test_features, train_labels, test_labels = train_test_split(
    features.to_numpy(),
    df["label"],
    test_size=0.2,
    random_state=42,
    stratify=df["label"],
)
model.fit(train_features, train_labels)

print(
    "[OK] Huấn luyện thành công! "
    f"Điểm trên tập test: {model.score(test_features, test_labels) * 100:.2f}%"
)

# 3. LƯU MÔ HÌNH VÀO ĐÚNG NƠI BACKEND SẼ ĐỌC
MODEL_FILE.parent.mkdir(parents=True, exist_ok=True)
joblib.dump(model, MODEL_FILE)
print(f"[OK] Đã lưu mô hình AI tại: {MODEL_FILE}")
