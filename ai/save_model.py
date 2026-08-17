import joblib
import os

def save_trained_model(model, filepath='models/disaster_model.pkl'):
    """Lưu mô hình ra file nén .pkl"""
    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    joblib.dump(model, filepath)
    print(f"💾 Đã đóng gói và lưu mô hình tại: {filepath}")