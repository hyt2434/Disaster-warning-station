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
  "f7_roll": 1.2,
  "f7_pitch": -2.5,
  "f7_tilt": 3.1,
  "f7_vibration": 0.25,
  "f7_impact": 0.8,
  "f7_status": "NORMAL",
  "status": "DANGER",
  "buzzer": false,
  "buzzer_muted": true
}
```

`temperature` và `humidity` là bắt buộc trong biểu mẫu giai đoạn 1. Backend tạo thiết bị nếu `device_id` chưa tồn tại và lưu thời gian UTC.

`distance_cm` và `water_level_cm` nhận `null` khi JSN-SR04T không có echo. `status`,
`buzzer` và `buzzer_muted` độc lập với nhau, vì vậy DANGER + buzzer OFF + muted là hợp lệ.

## `GET /api/readings/thingspeak-prediction`

Backend tải tối đa 20 bản ghi ThingSpeak gần nhất, ước lượng các cảm biến sau 5 phút
và đưa kết quả vào model AI:

```json
{
  "source": "ThingSpeak",
  "prediction_minutes": 5,
  "sample_count": 20,
  "system_prediction": "WARNING",
  "model_result": "safe",
  "model_available": true,
  "causes": [
    {
      "field": "field3",
      "sensor": "Khói / gas",
      "level": "WARNING",
      "predicted_value": 1450,
      "unit": "ADC",
      "threshold": 1300
    }
  ],
  "predicted_fields": {
    "field1": 32.4,
    "field2": 70.0,
    "field3": 1450,
    "field4": 55.0,
    "field5": 0,
    "field6": 1,
    "field7": 0.25
  }
}
```

`system_prediction` là `NORMAL`, `WARNING`, `DANGER` hoặc `INSUFFICIENT_DATA`.
Field 6 không được đưa vào model vì nó đã chứa kết quả trạng thái do ESP32 tính. Model Random Forest tiếp tục dùng cảm biến môi trường của Main; xu hướng F7 được kiểm tra theo ngưỡng và kết hợp vào `system_prediction`.

## `GET /api/readings/thingspeak-history`

Trả tối đa 20 bản ghi gần nhất được đọc trực tiếp từ ThingSpeak để frontend F4 vẽ chart:

```json
{
  "source": "ThingSpeak",
  "sample_count": 20,
  "readings": [
    {
      "recorded_at": "2026-08-19T10:00:00Z",
      "field1": 31.5,
      "field2": 70.0,
      "field3": 1200.0,
      "field4": 55.0,
      "field5": 0.0,
      "field6": 0.0,
      "field7": 0.25
    }
  ]
}
```

Kết quả gồm các Field 1–7 của một channel ThingSpeak. Frontend dùng Field 7 để vẽ biểu đồ độ rung F7.

## `POST /api/devices/main/buzzer`

```json
{ "state": "OFF" }
```

Backend publish plain text `ON` hoặc `OFF` đến `disaster/main/command/buzzer` với
`retain=false`. API không chờ hoặc phụ thuộc database trước khi gửi command.

Swagger UI: `http://localhost:8000/docs`.
