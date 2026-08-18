# REST API

Base URL mặc định: `http://localhost:8000`.

## `GET /api/health`

```json
{
  "backend": "online",
  "database": "connected",
  "mqtt": "connected",
  "thingspeak": "connected",
  "pushsafer": "ready",
  "main_device": "online",
  "f7_device": "unknown",
  "system": "danger",
  "buzzer": "off",
  "buzzer_muted": true,
  "ai": "available",
  "ai_prediction": "danger"
}
```

`thingspeak` có thể là `ready`, `connected`, `rate_limited`, `disconnected` hoặc `not_configured`.

`pushsafer` có thể là `ready`, `connected`, `disconnected`, `error` hoặc `not_configured`.

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
  "distance_cm": null,
  "water_level_cm": null,
  "motion_status": "SAFE",
  "status": "DANGER",
  "buzzer": false,
  "buzzer_muted": true
}
```

`temperature` và `humidity` là bắt buộc trong biểu mẫu giai đoạn 1. Backend tạo thiết bị nếu `device_id` chưa tồn tại và lưu thời gian UTC.

`distance_cm` và `water_level_cm` nhận `null` khi JSN-SR04T không có echo. `status`,
`buzzer` và `buzzer_muted` độc lập với nhau, vì vậy DANGER + buzzer OFF + muted là hợp lệ.

## `POST /api/devices/main/buzzer`

```json
{ "state": "OFF" }
```

Backend publish plain text `ON` hoặc `OFF` đến `disaster/main/command/buzzer` với
`retain=false`. API không chờ hoặc phụ thuộc database trước khi gửi command.

Swagger UI: `http://localhost:8000/docs`.
