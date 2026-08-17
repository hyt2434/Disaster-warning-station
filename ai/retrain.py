import os
import joblib
import pandas as pd
from pymongo import MongoClient
from sklearn.ensemble import RandomForestClassifier

# 1. Kết nối đến MongoDB Atlas của bạn
MONGO_URI = "mongodb+srv://uoprewamnkl_db_user:FmmRWFda7iQqisp5@disastercluster.iwjloi5.mongodb.net/?appName=DisasterCluster"
client = MongoClient(MONGO_URI)
db = client["disaster_db"] # Tên database của bạn
collection = db["sensor_readings"] # Tên collection lưu telemetry

def retrain_model_from_cloud():
    print("🔄 Đang tải dữ liệu thực tế từ MongoDB Atlas...")
    
    # Lấy toàn bộ dữ liệu từ Cloud xuống DataFrame
    data = list(collection.find({}, {"_id": 0, "timestamp": 0}))
    if len(data) < 100:
        print("⚠️ Chưa đủ dữ liệu thực tế để huấn luyện lại (cần ít nhất 100 mẫu).")
        return
        
    df = pd.DataFrame(data)
    
    # Gán nhãn tự động dựa trên quy tắc an toàn thực tế tại chỗ
    # Ví dụ: Nhiệt độ > 50 hoặc khói > 700 được coi là nguy hiểm (label = 1)
    df['label'] = 0
    df.loc[(df['temperature'] > 50) | (df['smoke_level'] > 700), 'label'] = 1
    
    X = df[['temperature', 'humidity', 'smoke_level', 'water_level']]
    y = df['label']
    
    print(f"🧠 Đang huấn luyện mô hình với {len(df)} mẫu dữ liệu thực tế...")
    model = RandomForestClassifier(n_estimators=50, max_depth=5, random_state=42)
    model.fit(X, y)
    
    # Lưu đè file .pkl mới vào backend
    model_path = os.path.join("backend", "app", "ml_models", "disaster_model.pkl")
    os.makedirs(os.path.dirname(model_path), exist_ok=True)
    joblib.dump(model, model_path)
    print(f"✅ Đã cập nhật 'Bộ não' AI mới từ dữ liệu thực tế tại vị trí lắp đặt! Lưu tại: {model_path}")
git
if __name__ == "__main__":
    retrain_model_from_cloud()