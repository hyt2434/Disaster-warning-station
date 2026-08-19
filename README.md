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
- PostgreSQL lưu các snapshot Main, kèm F7 mới nhất nếu còn fresh.
- MQTT nhận telemetry từ ESP32.
- ThingSpeak lưu dữ liệu Cloud và cung cấp lịch sử để retrain AI.
- Frontend lấy lịch sử ThingSpeak về và hiển thị biểu đồ Field 1–7 tại F4.
- Pushsafer gửi nguyên nhân khi Main chuyển sang `DANGER`; F7 tự gửi khi Main offline để tránh cảnh báo trùng.

Đây là đồ án demo local, chưa có cấu hình triển khai production.

## Cấu trúc repository

```text
Disaster-warning-station/
├── backend/          FastAPI, PostgreSQL, ThingSpeak, MQTT và AI runtime
├── frontend/         React + Vite dashboard
├── firmware/         Firmware ESP32 Main và ESP32-C3 F7
├── ai/               Script train synthetic và retrain từ ThingSpeak
├── infrastructure/   PostgreSQL schema dùng khi tạo database mới
├── docs/             Tài liệu kỹ thuật còn sử dụng
├── tests/            Integration test
└── README.md
```

Các file model `*.pkl` là artifact được sinh khi train/retrain nên không lưu trong Git. Backend vẫn tìm model runtime tại `backend/app/ml_models/disaster_model.pkl`; cần sinh file này trên máy trước khi sử dụng chức năng dự đoán AI.

Sau cleanup feature AI, model cũ dùng 4 feature không còn tương thích. Chạy lại `python ai/train.py` hoặc `python ai/retrain.py`; backend chủ động báo AI `unavailable` thay vì chạy sai schema.

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

PostgreSQL lưu snapshot Main và ghép F7 mới nhất nếu telemetry F7 không quá 10 giây. ThingSpeak nhận tối đa một mẫu mỗi 15 giây. Script `python ai/retrain.py` tải Field 1, 3 và 4 từ ThingSpeak và chỉ train khi có ít nhất 100 mẫu hợp lệ.

Frontend F5 gọi backend để đọc 20 bản ghi gần nhất trên ThingSpeak. Backend ngoại suy đại lượng vật lý đến +5 phút, đưa temperature, gas average và water level vào Random Forest classifier rồi kết hợp ngưỡng F7. Field 5/6 là category nên giữ giá trị mới nhất; Field 6 không làm đầu vào model vì đã là trạng thái do thiết bị tính.

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
python -m pytest
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
