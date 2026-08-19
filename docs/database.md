# Cơ sở dữ liệu PostgreSQL

## Hai bảng đang dùng

### `devices`

Lưu mã thiết bị, tên, loại, trạng thái online và lần xuất hiện gần nhất.

### `sensor_readings`

Lưu một ảnh chụp tổng hợp của Main và dữ liệu F7 mới nhất theo thời gian.

Các cột của ESP32 Main:

- `distance_cm`, `water_level_cm`: nullable khi JSN-SR04T không có echo;
- `status`: trạng thái nguy cơ tổng hợp SAFE/WARNING/DANGER;
- `buzzer`: trạng thái vật lý thực tế của còi;
- `buzzer_muted`: trạng thái tắt tiếng của alarm event hiện tại.

Các cột của ESP32 F7:

- `f7_roll`, `f7_pitch`: góc hiện tại của MPU6050;
- `f7_tilt`: độ nghiêng so với vị trí lúc calibration;
- `f7_vibration`: độ dao động trung bình trong nhóm mẫu;
- `f7_impact`: mức thay đổi gia tốc dùng để nhận biết va đập;
- `f7_status`: `NORMAL`, `WARNING` hoặc `DANGER`.

Main và F7 cùng gửi mỗi 2 giây. Backend giữ gói F7 mới nhất trong bộ nhớ rồi ghép vào bản ghi Main tiếp theo. Backend chỉ dùng gói F7 không quá 10 giây, tránh lưu dữ liệu cũ sau khi F7 mất kết nối.

Không còn bảng riêng cho F7. Toàn bộ lịch sử của Main và F7 được đọc chung qua `GET /api/readings`.

Khi tạo database mới thủ công, chạy `infrastructure/database/schema.sql` trước khi khởi động backend.

Các index chính:

- `device_id`
- `recorded_at`
- `(device_id, recorded_at)`

## Khởi tạo

File `infrastructure/database/schema.sql` tạo đúng hai bảng `devices` và `sensor_readings` trong một transaction. Chạy file đúng một lần trên database trống; không cần chạy thêm `ALTER TABLE`. Nếu chạy nhầm lần thứ hai, PostgreSQL sẽ báo bảng đã tồn tại và rollback transaction.

## Cấu hình

```text
DATABASE_URL=postgresql://user:password@host:5432/database_name
```

Với PostgreSQL cloud có yêu cầu SSL, dùng URL do nhà cung cấp cấp sẵn, thường có `sslmode=require`. Secret chỉ đặt trong `.env`, không ghi vào source.
