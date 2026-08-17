import pandas as pd

def clean_sensor_data(df: pd.DataFrame) -> pd.DataFrame:
    """Làm sạch và chuẩn hóa dữ liệu cảm biến"""
    # Loại bỏ các dòng có giá trị null
    df = df.dropna()
    
    # Lọc bỏ các giá trị dị thường (outliers) ví dụ nhiệt độ âm hoặc quá 100 độ C
    df = df[(df['temperature'] >= 0) & (df['temperature'] <= 100)]
    df = df[(df['humidity'] >= 0) & (df['humidity'] <= 100)]
    
    return df