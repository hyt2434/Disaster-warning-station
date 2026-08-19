# Cơ sở dữ liệu PostgreSQL

## Schema hiện tại

Database chỉ có bảng `sensor_readings`. Mỗi row là một snapshot do ESP32 Main gửi, kèm telemetry F7 mới nhất nếu gói F7 còn mới không quá 10 giây.

Các nhóm cột:

- Main: `device_id`, `temperature`, `humidity`, `gas_average`, `distance_cm`, `water_level_cm`;
- F7 nullable: `f7_roll`, `f7_pitch`, `f7_tilt`, `f7_vibration`, `f7_impact`, `f7_status`;
- trạng thái Main: `status`, `buzzer`, `buzzer_muted`, `recorded_at`.

`gas_average` là trung bình của 5 lần đọc ADC MQ-2 trong một chu kỳ, không phải ppm. `distance_cm` và `water_level_cm` là `null` khi JSN-SR04T không có echo.

Chỉ telemetry trực tiếp từ topic F7 mới được ghi vào các cột `f7_*`. Nếu chưa có gói F7 hoặc gói đã quá 10 giây, toàn bộ `f7_*` là `null`; số `0` chỉ có nghĩa là F7 thực sự đo được 0.

F7 không tạo row PostgreSQL riêng. Khi Main offline, F7 vẫn có thể cập nhật cache backend và gửi Field 7 lên ThingSpeak, nhưng lịch sử PostgreSQL không có snapshot mới cho đến khi Main gửi telemetry trở lại.

Các index chính:

- `device_id`;
- `recorded_at`;
- `(device_id, recorded_at)`.

## Khởi tạo và thay đổi schema

SQLAlchemy models và `create_tables()` là runtime source of truth. Chỉ cần tạo một database rỗng rồi chạy backend; backend sẽ tạo table còn thiếu.

`infrastructure/database/schema.sql` là bản tham khảo và là lựa chọn thiết lập thủ công cho database PostgreSQL trống, không phải bước bắt buộc thứ hai.

Quan trọng: sau lần cleanup/gộp F7 này, database dùng schema cũ phải được recreate hoặc migrate thủ công. `Base.metadata.create_all()` chỉ tạo table chưa tồn tại, không tự `ALTER` table cũ và không xóa bảng/cột cũ.

Với demo không cần giữ dữ liệu, cách đơn giản là backup nếu cần, drop database cũ, tạo database rỗng rồi khởi động backend. Không chạy thao tác này trên database cần bảo toàn dữ liệu.

## Cấu hình

```text
DATABASE_URL=postgresql://user:password@host:5432/database_name
```

Với PostgreSQL cloud yêu cầu SSL, dùng URL do nhà cung cấp cấp, thường có `sslmode=require`. Secret chỉ đặt trong `.env`, không ghi vào source.
