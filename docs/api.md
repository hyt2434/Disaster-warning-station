# REST API — Phase 1

Base URL mặc định: `http://localhost:8000`.

## `GET /api/health`

```json
{
  "backend": "online",
  "database": "connected"
}
```

## `GET /api/readings?limit=20`

Trả tối đa 100 bản ghi, sắp xếp từ mới đến cũ.

## `GET /api/readings/latest`

Trả bản ghi mới nhất; trả `null` nếu database chưa có dữ liệu.

## `POST /api/readings`

Body tối thiểu:

```json
{
  "device_id": "main-station-01",
  "temperature": 31.5,
  "humidity": 72.4,
  "gas_raw": 1380,
  "water_level_cm": 8.2,
  "status": "WARNING"
}
```

`temperature` và `humidity` là bắt buộc trong biểu mẫu giai đoạn 1. Backend tạo thiết bị nếu `device_id` chưa tồn tại và lưu thời gian UTC.

Swagger UI: `http://localhost:8000/docs`.
