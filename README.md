# Disaster Warning Station

Đồ án demo trạm IoT cảnh báo cháy, ngập và bất thường môi trường.

## Phạm vi hiện tại

Hệ thống được phát triển để chạy local:

```text
ESP32 → Mosquitto MQTT → FastAPI
                           ├── PostgreSQL
                           ├── MongoDB Atlas
                           ├── mô hình AI
                           └── lệnh điều khiển buzzer

React local → FastAPI REST API → PostgreSQL
```

- Frontend React chạy tại `http://localhost:5173`.
- Backend FastAPI chạy tại `http://localhost:8000`.
- PostgreSQL lưu thiết bị và lịch sử dữ liệu từ REST API lẫn MQTT.
- MQTT nhận telemetry từ ESP32.
- Code mới có tích hợp MongoDB Atlas và mô hình AI để đánh giá dữ liệu MQTT.
- Push notification chưa được triển khai; `AlertService` hiện vẫn là khung.

Đây là đồ án demo local, chưa có cấu hình triển khai production.

## Cấu trúc repository

```text
Disaster-warning-station/
├── backend/          FastAPI, PostgreSQL, MongoDB, MQTT và AI runtime
├── frontend/         React + Vite dashboard
├── firmware/         Firmware hai ESP32 và công cụ kiểm tra kết nối
├── ai/               Script huấn luyện, đánh giá và retrain mô hình
├── infrastructure/   PostgreSQL schema và Mosquitto local
├── docs/             Tài liệu kỹ thuật còn sử dụng
├── tests/            Integration test
├── docker-compose.yml
└── README.md
```

Các file model `*.pkl` là artifact được sinh khi train/retrain nên không lưu trong Git. Backend vẫn tìm model runtime tại `backend/app/ml_models/disaster_model.pkl`; cần sinh file này trên máy trước khi sử dụng chức năng dự đoán AI.

## Chạy PostgreSQL và backend

PostgreSQL local có thể được cài trực tiếp hoặc chạy bằng Docker. Cấu hình kết nối nằm trong `.env`:

```dotenv
DATABASE_URL=postgresql://<user>:<password>@localhost:5432/<database>
MONGODB_URI=mongodb+srv://<user>:<password>@<cluster>/?appName=<app>
MONGODB_DATABASE=DisasterDB
MONGODB_COLLECTION=sensor_data
```

MongoDB Atlas là Cloud storage bắt buộc của demo. Nếu Atlas tạm mất kết nối, backend giữ tối đa 10.000 telemetry trong RAM và tự đẩy bù khi kết nối lại; PostgreSQL local vẫn giữ dữ liệu cho dashboard.

Chạy backend:

```powershell
cd backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python app.py
```

Kiểm tra:

- Health: `http://localhost:8000/api/health`
- Swagger UI: `http://localhost:8000/docs`

## Chạy frontend

```powershell
cd frontend
npm install
npm run dev
```

Mở `http://localhost:5173`.

## Chạy hạ tầng local bằng Docker

Nếu máy có Docker:

```powershell
docker compose up -d database mqtt-broker
```

`docker-compose.yml` chỉ phục vụ môi trường local, không phải cấu hình production.

## Kiểm tra

```powershell
python -m pytest tests/integration/test_readings_api.py
```

```powershell
cd frontend
npm run build
```

## Tài liệu

- [REST API](docs/api.md)
- [PostgreSQL](docs/database.md)
- [Firmware ESP32](docs/firmware.md)
- [MQTT topics và payload](docs/mqtt-contract.md)
- [Cài đặt MQTT local](docs/mqtt-local-setup.md)
- [Hướng dẫn setup demo và fallback Wi-Fi](docs/demo-setup.md)

## Trạng thái cần hoàn thiện

- MongoDB Atlas, model AI và logic suy luận MQTT đã được thêm trong hai commit mới nhất.
- Push notification chưa có implementation.
- Topic MQTT của firmware và backend đã được đồng bộ về root `disaster/`.
- Telemetry MQTT từ Main được lưu vào PostgreSQL để dashboard đọc, đồng thời vẫn đi qua MongoDB/AI.
- Các dependency MongoDB/AI đã được thêm vào `backend/requirements.txt`.
- Model `*.pkl` được tạo local và bị Git ignore; AI tạm bỏ qua dự đoán nếu chưa có model runtime.
- MongoDB đọc credential từ `.env`; không lưu URI hoặc mật khẩu Cloud trong source.
