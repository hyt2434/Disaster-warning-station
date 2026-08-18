# Cơ sở dữ liệu PostgreSQL

## Bảng đang dùng

### `devices`

Lưu mã thiết bị, tên, loại, trạng thái online và lần xuất hiện gần nhất.

### `sensor_readings`

Lưu dữ liệu cảm biến theo thời gian. Các cột chưa dùng ở giao diện hiện tại vẫn được để `NULL` để có thể bổ sung MQTT/F6/F7 sau này mà không phải đổi bảng ngay.

Các index chính:

- `device_id`
- `recorded_at`
- `(device_id, recorded_at)`

## Khởi tạo

Backend gọi `Base.metadata.create_all()` khi khởi động. Nếu muốn tạo thủ công, chạy `infrastructure/database/schema.sql` trên PostgreSQL.

## Cấu hình

```text
DATABASE_URL=postgresql://user:password@host:5432/database_name
```

Với PostgreSQL cloud có yêu cầu SSL, dùng URL do nhà cung cấp cấp sẵn, thường có `sslmode=require`. Secret chỉ đặt trong `.env`, không ghi vào source.
