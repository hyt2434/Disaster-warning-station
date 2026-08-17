import pandas as pd
import numpy as np
from sklearn.ensemble import RandomForestClassifier
import joblib
import os

print("🚀 Đang khởi tạo dữ liệu giả lập (Synthetic Data)...")

# 1. TẠO DỮ LIỆU GIẢ LẬP (Mô phỏng 2000 trường hợp)
np.random.seed(42)
n_samples = 2000

# Tạo dữ liệu trạng thái BÌNH THƯỜNG (Label = 0)
normal_temp = np.random.normal(30, 5, 1000)      # Nhiệt độ quanh 30 độ C
normal_hum = np.random.normal(70, 10, 1000)      # Độ ẩm 70%
normal_smoke = np.random.normal(200, 50, 1000)   # Khói thấp (MQ-2)
normal_water = np.zeros(1000)                    # Không ngập
normal_labels = np.zeros(1000)

# Tạo dữ liệu trạng thái NGUY HIỂM / CHÁY / NGẬP (Label = 1)
danger_temp = np.random.normal(60, 15, 1000)     # Nhiệt độ cao > 50 độ C
danger_hum = np.random.normal(40, 15, 1000)      # Độ ẩm thấp
danger_smoke = np.random.normal(800, 150, 1000)  # Khói dày đặc
danger_water = np.random.choice([0, 1], size=1000, p=[0.7, 0.3]) # Có tỷ lệ ngập nước
danger_labels = np.ones(1000)

# Gộp dữ liệu lại thành bảng
X = np.vstack([
    np.column_stack((normal_temp, normal_hum, normal_smoke, normal_water)),
    np.column_stack((danger_temp, danger_hum, danger_smoke, danger_water))
])
y = np.concatenate([normal_labels, danger_labels])

df = pd.DataFrame(X, columns=['temperature', 'humidity', 'smoke', 'water'])
df['label'] = y

print("🧠 Đang tiến hành huấn luyện mô hình Random Forest...")
# 2. HUẤN LUYỆN MÔ HÌNH
model = RandomForestClassifier(n_estimators=50, max_depth=5, random_state=42)
model.fit(df[['temperature', 'humidity', 'smoke', 'water']], df['label'])

print(f"✅ Huấn luyện thành công! Độ chính xác (Accuracy) trên tập mẫu: {model.score(df[['temperature', 'humidity', 'smoke', 'water']], df['label']) * 100:.2f}%")

# 3. LƯU MÔ HÌNH RA FILE .pkl
# Tạo thư mục models nếu chưa có
os.makedirs('models', exist_ok=True)
model_path = 'models/disaster_model.pkl'

joblib.dump(model, model_path)
print(f"💾 Đã lưu bộ não AI tại: {model_path}")