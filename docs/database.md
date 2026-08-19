# Cơ sở dữ liệu PostgreSQL

## Bảng đang dùng

### `devices`

Lưu mã thiết bị, tên, loại, trạng thái online và lần xuất hiện gần nhất.

### `sensor_readings`

Lưu một ảnh chụp tổng hợp của Main và dữ liệu F7 mới nhất theo thời gian.

Các cột liên quan firmware Main mới:

- `distance_cm`, `water_level_cm`: nullable khi JSN-SR04T không có echo;
- `angle_x`, `angle_y`: lần lượt là roll và pitch mới nhất của F7;
- `vibration`: độ rung mới nhất của F7;
- `motion_status`: trạng thái chuyển động do Main nhận từ F7;
- `status`: trạng thái nguy cơ tổng hợp SAFE/WARNING/DANGER;
- `buzzer`: trạng thái vật lý thực tế của còi;
- `buzzer_muted`: trạng thái tắt tiếng của alarm event hiện tại.

Main và F7 cùng gửi mỗi 2 giây. Backend chỉ ghép dữ liệu F7 vào bản ghi Main khi gói F7 mới nhất không quá 10 giây, tránh lưu lại một giá trị cũ sau khi F7 đã mất kết nối.

Ba cột `motion_status`, `buzzer`, `buzzer_muted` đã có sẵn trong model và
`infrastructure/database/schema.sql`. Bản demo mới tạo database từ đầu nên backend không chạy
`ALTER TABLE` khi khởi động.

### `f7_readings`

Mỗi MQTT telemetry từ F7 tạo một bản ghi riêng gồm:

- `roll`, `pitch`: góc hiện tại của MPU6050;
- `tilt`: độ nghiêng so với vị trí lúc calibration;
- `vibration`: độ dao động trung bình trong nhóm mẫu;
- `impact`: mức thay đổi gia tốc dùng để nhận biết va đập;
- `status`: `NORMAL`, `WARNING` hoặc `DANGER`;
- `recorded_at`: thời gian backend nhận dữ liệu.

API kiểm tra lịch sử F7:

```text
GET http://localhost:8000/api/devices/f7/readings?limit=20
```

Khi tạo database mới thủ công, chạy `infrastructure/database/schema.sql` trước khi khởi động backend.

Các index chính:

- `device_id`
- `recorded_at`
- `(device_id, recorded_at)`

## Khởi tạo

File `infrastructure/database/schema.sql` chứa toàn bộ cấu trúc cuối cùng trong một transaction. Chạy file đúng một lần trên database trống; không cần chạy thêm `ALTER TABLE`. Nếu chạy nhầm lần thứ hai, PostgreSQL sẽ báo bảng đã tồn tại và rollback transaction.

## Cấu hình

```text
DATABASE_URL=postgresql://user:password@host:5432/database_name
```

Với PostgreSQL cloud có yêu cầu SSL, dùng URL do nhà cung cấp cấp sẵn, thường có `sslmode=require`. Secret chỉ đặt trong `.env`, không ghi vào source.
