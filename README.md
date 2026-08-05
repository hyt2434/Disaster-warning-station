# Disaster Warning Station

Monorepo cho trạm IoT cảnh báo sớm cháy, ngập và bất thường môi trường.

## Phạm vi hiện tại

Phiên bản đầu tiên chỉ triển khai một luồng nhỏ, có thể kiểm tra độc lập:

`React → FastAPI REST API → PostgreSQL → FastAPI → React`

Website hiển thị trạng thái kết nối database, dữ liệu cảm biến mới nhất, 20 bản ghi gần nhất và có biểu mẫu nhập dữ liệu thủ công để kiểm tra việc ghi PostgreSQL. Dữ liệu hiển thị không phải dữ liệu mock trong frontend.

MQTT, WebSocket, firmware thật, điều khiển buzzer, AI/DS và push notification được để dành cho các giai đoạn sau. Dự án hiện không dùng IFTTT.

## Cấu trúc chính

- `backend/`: FastAPI, SQLAlchemy, REST API và kết nối PostgreSQL.
- `frontend/`: React, Vite và JavaScript dashboard.
- `infrastructure/database/`: schema PostgreSQL tương ứng với SQLAlchemy models.
- `firmware/`, `ai/`, `infrastructure/mqtt-broker/`: khung cho giai đoạn sau, chưa tham gia luồng hiện tại.
- `docs/`: tài liệu API và database.
- `tests/`: test tích hợp tối thiểu cho API/database.

## 1. Chuẩn bị PostgreSQL

Có thể dùng PostgreSQL cài trên máy hoặc một PostgreSQL cloud. Tạo database và user, sau đó chạy [schema.sql](infrastructure/database/schema.sql), hoặc để backend tự tạo hai bảng khi khởi động.

Ví dụ URL local:

```text
postgresql://disaster_warning_user:your_password@localhost:5432/disaster_warning_station
```

File cấu hình dùng chung nằm tại `.env` ở thư mục gốc repository. Điền `DATABASE_URL` theo PostgreSQL trên máy, ví dụ:

```text
DATABASE_URL=postgresql://postgres:your_password@localhost:5432/DisasterWarning
```

Backend và frontend đều đọc file này. Không commit `.env` vì file chứa mật khẩu database.

Nếu máy đã có Docker, có thể khởi động riêng PostgreSQL bằng lệnh tùy chọn:

```powershell
docker compose up -d database
```

## 2. Chạy backend

```powershell
cd backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python -m app.main
```

Kiểm tra tại `http://localhost:8000/api/health` và tài liệu API tại `http://localhost:8000/docs`.

## 3. Chạy frontend

Mở terminal khác:

```powershell
cd frontend
npm install
npm run dev
```

Mở `http://localhost:5173`. Frontend bắt buộc dùng `VITE_API_BASE_URL` trong `.env`; dự án không hard-code URL backend dự phòng.

## Kiểm tra

```powershell
cd frontend
npm run build
```

```powershell
python -m pytest tests/integration/test_readings_api.py
```

Test tích hợp dùng SQLite tạm để kiểm tra logic API mà không thay thế cấu hình PostgreSQL của ứng dụng.

## API hiện có

- `GET /api/health`: trạng thái backend và database.
- `GET /api/readings?limit=20`: danh sách bản ghi mới nhất.
- `GET /api/readings/latest`: bản ghi mới nhất hoặc `null`.
- `POST /api/readings`: lưu một bản ghi cảm biến.

Xem chi tiết tại [docs/api.md](docs/api.md) và [docs/database.md](docs/database.md).

## Giới hạn hiện tại

- Biểu mẫu nhập tay chỉ dùng để chứng minh kết nối database trước khi ESP32/MQTT được tích hợp.
- Chưa có realtime WebSocket; nhấn “Làm mới” để đọc lại dữ liệu.
- Chưa có authentication, phân quyền, chart, command, notification hoặc AI.
- Đây là mô hình hỗ trợ giám sát, không thay thế thiết bị an toàn đã được kiểm định.
