# Backend

FastAPI backend cho giai đoạn web + PostgreSQL.

## Cấu trúc `app`

- `main.py`: khởi tạo FastAPI, CORS, lifespan và đăng ký router.
- `config/`: dùng `python-dotenv` để đọc `.env`, sau đó Pydantic kiểm tra cấu hình.
- `database/`: kết nối SQLAlchemy, models và repository.
- `schemas/`: các Pydantic request/response model.
- `api/`: các REST API router theo chức năng.
- `mqtt/`: khung MQTT dành cho giai đoạn sau.
- `services/`: các service nghiệp vụ; notification hiện chưa triển khai.
- `websocket/`: khung quản lý WebSocket dành cho giai đoạn sau.

## Chạy trực tiếp

Từ thư mục `backend/app`:

```powershell
python main.py
```

`app/main.py` có phần bootstrap nhỏ để Python tìm thấy package `app` khi file được chạy trực tiếp.

Bạn vẫn có thể chạy theo dạng package từ thư mục `backend`:

```powershell
python -m app.main
```

Backend đọc `.env` tại thư mục gốc repository. Nếu database chưa sẵn sàng, server vẫn khởi động và `/api/health` trả `database: disconnected`.

Luồng nạp cấu hình trong `config/settings.py`:

```python
import dotenv

dotenv.load_dotenv(ENV_FILE)
settings = Settings()
```

MQTT, WebSocket, AI, buzzer và notification chưa được nối vào ứng dụng ở giai đoạn này.
