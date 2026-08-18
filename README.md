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
- PostgreSQL lưu thiết bị và lịch sử dữ liệu được gửi qua REST API.
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
├── models/           Model sinh ra khi chạy script train từ thư mục gốc
├── infrastructure/   PostgreSQL schema và Mosquitto local
├── docs/             Tài liệu kỹ thuật còn sử dụng
├── tests/            Integration test
├── docker-compose.yml
└── README.md
```

Backend tải model runtime từ `backend/app/ml_models/disaster_model.pkl`. Các bản model khác được giữ nguyên theo code vừa pull để phục vụ quá trình train/retrain.

## Chạy PostgreSQL và backend

PostgreSQL local có thể được cài trực tiếp hoặc chạy bằng Docker. Cấu hình kết nối nằm trong `.env`:

```dotenv
DATABASE_URL=postgresql://<user>:<password>@localhost:5432/<database>
```

Chạy backend:

```powershell
cd backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python -m app.main
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

## Lưu ý với code vừa pull

- MongoDB Atlas, model AI và logic suy luận MQTT đã được thêm trong hai commit mới nhất.
- Push notification chưa có implementation.
- Topic MQTT trong firmware/tài liệu và topic backend đang chưa đồng nhất hoàn toàn.
- Dependency MongoDB/AI chưa được khai báo đủ trong `backend/requirements.txt`.
- Chuỗi kết nối MongoDB hiện nằm trực tiếp trong source; cần đổi credential và chuyển sang `.env` trước khi chia sẻ repository.

Các điểm trên được giữ nguyên để không thay đổi phần tích hợp vừa pull.
