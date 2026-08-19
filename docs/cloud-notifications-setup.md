# Cài đặt ThingSpeak và Pushsafer

Backend là nơi kết nối hai dịch vụ Cloud. ESP32 chỉ gửi telemetry qua MQTT, vì vậy không đặt API key trong firmware.

## 1. ThingSpeak

### Tạo channel

1. Đăng nhập [ThingSpeak](https://thingspeak.mathworks.com/).
2. Chọn **Channels → My Channels → New Channel**.
3. Đặt tên channel, ví dụ `Disaster Warning Station`.
4. Bật và đặt tên bảy field đúng thứ tự sau, rồi chọn **Save Channel**.

| Field | Tên đề xuất | Dữ liệu backend gửi |
|---|---|---|
| Field 1 | Temperature | Nhiệt độ (°C) |
| Field 2 | Humidity | Độ ẩm (%) |
| Field 3 | Gas Level | Giá trị MQ-2 đã lọc |
| Field 4 | Water Level | Mực nước (cm) |
| Field 5 | Motion Status | SAFE = 0, WARNING = 1, DANGER = 2 |
| Field 6 | System Status | SAFE = 0, WARNING = 1, DANGER = 2 |
| Field 7 | F7 Vibration | Độ rung của MPU6050 (m/s²) |

### Lấy ID và API key

Trong channel vừa tạo:

- `Channel ID` nằm ở trang channel.
- `Write API Key` nằm trong tab **API Keys** và cho phép backend ghi dữ liệu.
- `Read API Key` cũng nằm trong tab **API Keys**. Key này cần cho `ai/retrain.py` nếu channel là private; channel public có thể để trống.

Điền vào file `.env` ở thư mục gốc:

```dotenv
THINGSPEAK_WRITE_API_KEY=YOUR_WRITE_API_KEY
THINGSPEAK_CHANNEL_ID=YOUR_CHANNEL_ID
THINGSPEAK_READ_API_KEY=YOUR_READ_API_KEY
```

Không thêm dấu ngoặc `< >` và không commit file `.env`.

### Kiểm tra ThingSpeak

Khởi động MQTT, backend và hai ESP32. Chờ ít nhất 15 giây rồi mở tab **Private View** của channel. Field 1–7 phải bắt đầu có dữ liệu. Khi Main mất kết nối nhưng F7 vẫn online, backend tiếp tục tạo bản ghi Cloud chỉ có Field 7.

Bạn cũng có thể kiểm tra riêng Write API Key trong PowerShell:

```powershell
$thingSpeakBody = @{
    api_key = "YOUR_WRITE_API_KEY"
    field1 = 30.5
    field2 = 70
    field7 = 0.25
}
Invoke-RestMethod -Method Post -Uri "https://api.thingspeak.com/update.json" -Body $thingSpeakBody
```

Nếu thành công, ThingSpeak trả về bản ghi mới. Nếu bị từ chối, kiểm tra Write API Key và chờ đủ 15 giây trước lần gửi tiếp theo.

Frontend F5 không đọc API key trực tiếp. Backend dùng Channel ID và Read API Key để lấy 20 bản ghi gần nhất. Field 1–4 đi vào model AI; Field 5 và Field 7 giúp kiểm tra xu hướng chuyển động và rung F7 sau 5 phút. Sau đó backend tổng hợp thành một kết quả toàn hệ thống.

Frontend F4 cho phép chọn biểu đồ Field 1–7 của cùng một channel. ESP32 và monitor
local cập nhật mỗi 2 giây; biểu đồ ThingSpeak cập nhật khoảng 15 giây/lần theo giới hạn Cloud.

## 2. Pushsafer

### Tạo tài khoản và đăng ký điện thoại

1. Tạo tài khoản tại [Pushsafer](https://www.pushsafer.com/).
2. Cài ứng dụng Pushsafer trên điện thoại, đăng nhập hoặc làm theo hướng dẫn đăng ký thiết bị trong dashboard.
3. Trong dashboard, sao chép **Private Key**.
4. Ghi lại **Device ID** của điện thoại. Nếu muốn gửi cho mọi thiết bị trong tài khoản, dùng `a`.

Điền vào `.env`:

```dotenv
PUSHSAFER_PRIVATE_KEY=YOUR_PRIVATE_KEY
PUSHSAFER_DEVICE_ID=a
```

Nếu chỉ gửi tới một điện thoại, thay `a` bằng Device ID, ví dụ `52`. Sau khi sửa `.env`, phải dừng và chạy lại backend.

### Logic thông báo trong dự án

| Nguồn | Khi nào gửi |
|---|---|
| ESP32 Main | `system_status` mới chuyển sang `WARNING` hoặc `DANGER` |
| ESP32 F7 | `status` mới chuyển sang `WARNING` hoặc `DANGER` |

Backend không gửi lặp lại khi ESP32 liên tục gửi cùng một trạng thái. Khi thiết bị trở về `SAFE`, lần `WARNING` hoặc `DANGER` tiếp theo sẽ gửi một thông báo mới. Cách này giúp tránh làm phiền và tránh tiêu hao lượt API Pushsafer.

### Kiểm tra Pushsafer độc lập

Chạy trong PowerShell, thay key thật của bạn:

```powershell
$pushsaferBody = @{
    k = "YOUR_PRIVATE_KEY"
    d = "a"
    t = "Disaster Warning Station"
    m = "Kiểm tra thông báo từ backend"
}
Invoke-RestMethod -Method Post -Uri "https://www.pushsafer.com/api" -Body $pushsaferBody
```

Kết quả thành công có `status = 1`. Sau đó chạy backend và kiểm tra `http://localhost:8000/api/health`:

| Giá trị `pushsafer` | Ý nghĩa |
|---|---|
| `not_configured` | Chưa điền Private Key |
| `ready` | Đã đọc cấu hình, chưa gửi thông báo |
| `connected` | Lần gửi gần nhất thành công |
| `error` | Key/Device ID bị từ chối hoặc hết lượt API |
| `disconnected` | Không kết nối được dịch vụ |

## 3. Thứ tự chạy demo

1. Khởi động PostgreSQL và MQTT Broker.
2. Chạy `python backend/app.py` từ thư mục gốc, hoặc `python app.py` khi đang ở `backend`.
3. Bật ESP32 và kiểm tra MQTT telemetry trong log backend.
4. Chờ ThingSpeak cập nhật Field 1–7.
5. Tạo điều kiện `WARNING`/`DANGER` một lần và kiểm tra điện thoại nhận Pushsafer.

API key chỉ thuộc backend. Không đưa key vào frontend, firmware hoặc ảnh chụp màn hình khi nộp bài.
