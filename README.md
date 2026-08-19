# Disaster Warning Station

Đồ án demo trạm IoT cảnh báo cháy, ngập và bất thường môi trường.

## Phạm vi hiện tại

Hệ thống được phát triển để chạy local:

```text
ESP32 → Mosquitto MQTT → FastAPI
                           ├── PostgreSQL
                           ├── ThingSpeak Cloud
                           ├── mô hình AI
                           ├── Pushsafer notification
                           └── lệnh điều khiển buzzer

React local → FastAPI REST API → PostgreSQL
```

- Frontend React chạy tại `http://localhost:5173`.
- Backend FastAPI chạy tại `http://localhost:8000`.
- PostgreSQL lưu thiết bị và lịch sử dữ liệu từ REST API lẫn MQTT.
- MQTT nhận telemetry từ ESP32.
- ThingSpeak lưu dữ liệu Cloud và cung cấp lịch sử để retrain AI.
- Frontend lấy lịch sử ThingSpeak về và hiển thị biểu đồ Field 1–7 tại F4.
- Pushsafer gửi nguyên nhân khi Main hoặc F7 chuyển sang `DANGER`.

Đây là đồ án demo local, chưa có cấu hình triển khai production.

## Cấu trúc repository

```text
Disaster-warning-station/
├── backend/          FastAPI, PostgreSQL, ThingSpeak, MQTT và AI runtime
├── frontend/         React + Vite dashboard
├── firmware/         Firmware hai ESP32 và công cụ kiểm tra kết nối
├── ai/               Script huấn luyện, đánh giá và retrain mô hình
├── infrastructure/   PostgreSQL schema dùng khi tạo database mới
├── docs/             Tài liệu kỹ thuật còn sử dụng
├── tests/            Integration test
└── README.md
```

Các file model `*.pkl` là artifact được sinh khi train/retrain nên không lưu trong Git. Backend vẫn tìm model runtime tại `backend/app/ml_models/disaster_model.pkl`; cần sinh file này trên máy trước khi sử dụng chức năng dự đoán AI.

## Chạy PostgreSQL và backend

PostgreSQL và Mosquitto được cài và chạy trực tiếp trên máy. Cấu hình kết nối nằm trong `.env`:

```dotenv
DATABASE_URL=postgresql://<user>:<password>@localhost:5432/<database>
THINGSPEAK_WRITE_API_KEY=<write_api_key>
THINGSPEAK_CHANNEL_ID=<channel_id>
THINGSPEAK_READ_API_KEY=<read_api_key_if_private>
PUSHSAFER_PRIVATE_KEY=<private_key>
PUSHSAFER_DEVICE_ID=a
```

PostgreSQL lưu toàn bộ telemetry để website đọc. ThingSpeak nhận tối đa một mẫu mỗi 15 giây. Script `python ai/retrain.py` tải Field 1–4 từ ThingSpeak và chỉ train khi có ít nhất 100 mẫu hợp lệ.

Frontend F5 gọi backend để đọc 20 bản ghi gần nhất trên ThingSpeak. Backend ước lượng dữ liệu sau 5 phút, đưa Field 1–4 vào model Random Forest và kiểm tra Field 5 cho chuyển động. Frontend chỉ hiển thị kết luận toàn hệ thống cùng dữ liệu gây `WARNING`/`DANGER`. Field 6 không dùng làm đầu vào vì đây là trạng thái hệ thống đã được thiết bị tính.

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
- [Cài đặt ThingSpeak và Pushsafer](docs/cloud-notifications-setup.md)
- [Hướng dẫn setup demo](docs/demo-setup.md)

## Trạng thái cần hoàn thiện

- ThingSpeak Cloud và model AI đã được tích hợp vào luồng MQTT.
- Pushsafer đã được tích hợp; cần điền Private Key và Device ID trong `.env`.
- Topic MQTT của firmware và backend đã được đồng bộ về root `disaster/`.
- Telemetry MQTT từ Main được lưu vào PostgreSQL, gửi lên ThingSpeak và đi qua AI.
- Một channel ThingSpeak dùng Field 1–6 cho nhiệt độ, độ ẩm, gas, nước, motion và system; Field 7 lưu độ rung F7.
- Model `*.pkl` được tạo local và bị Git ignore; AI tạm bỏ qua dự đoán nếu chưa có model runtime.
- ThingSpeak API key được đọc từ `.env`, không lưu trong source được commit.
