import joblib
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MODEL_FILE = PROJECT_ROOT / "backend" / "app" / "ml_models" / "disaster_model.pkl"


def save_trained_model(model, filepath=DEFAULT_MODEL_FILE):
    """Lưu mô hình ra file nén .pkl"""
    model_file = Path(filepath)
    model_file.parent.mkdir(parents=True, exist_ok=True)
    joblib.dump(model, model_file)
    print(f"💾 Đã đóng gói và lưu mô hình tại: {model_file.resolve()}")
