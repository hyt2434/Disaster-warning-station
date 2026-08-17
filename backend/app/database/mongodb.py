from pymongo import MongoClient

MONGO_URI = "mongodb+srv://uoprewamnkl_db_user:FmmRWFda7iQqisp5@disastercluster.iwjloi5.mongodb.net/?appName=DisasterCluster"

try:
    print("Đang kết nối đến MongoDB Atlas...")
    client = MongoClient(MONGO_URI)
    db = client["DisasterDB"]             
    collection = db["sensor_data"]        
    print("✅ Kết nối MongoDB thành công!")
except Exception as e:
    print(f"❌ Lỗi kết nối MongoDB: {e}")