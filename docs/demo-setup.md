# Hướng dẫn chạy demo

Tài liệu này tập trung vào ESP32 Main, PostgreSQL, ThingSpeak, Pushsafer, AI và website.

## 1. Luồng hoạt động

```text
Cảm biến ESP32 Main
        ↓
ESP32 tính SAFE / WARNING / DANGER
        ↓ MQTT
Backend FastAPI
   ├── PostgreSQL: lưu đầy đủ để website đọc
   ├── ThingSpeak: lưu một mẫu mỗi 15 giây
   ├── Pushsafer: gửi nguyên nhân khi trạng thái chuyển sang DANGER
   └── AI: dùng dữ liệu ThingSpeak để dự đoán hệ thống sau 5 phút
        ↓ REST API
Frontend React
```

ESP32 Main là nơi quyết định đèn và còi. Backend và AI không tự gửi lệnh bật còi.

## 2. Chân kết nối ESP32 Main

| Thiết bị | Chân ESP32-S3 |
|---|---:|
| DHT11 DATA | GPIO4 |
| MQ-2 analog | GPIO5 |
| JSN-SR04T TRIG | GPIO6 |
| JSN-SR04T ECHO | GPIO7 |
| Buzzer | GPIO15 |
| LED xanh | GPIO16 |
| LED vàng | GPIO17 |
| LED đỏ | GPIO18 |

Nếu ECHO của JSN-SR04T hoặc analog output của MQ-2 có thể đạt 5 V, phải dùng cầu chia áp trước khi nối vào ESP32.

## 3. Logic cảm biến trên ESP32 Main

ESP32 đọc cảm biến và gửi telemetry mỗi 2 giây. Frontend cũng tải dữ liệu mới mỗi 2 giây.

### DHT11

| Dữ liệu | Điều kiện | Trạng thái |
|---|---:|---|
| Nhiệt độ | `< 35°C` | SAFE |
| Nhiệt độ | `35°C đến dưới 40°C` | WARNING |
| Nhiệt độ | `>= 40°C` | DANGER |
| Độ ẩm | Chỉ đo và hiển thị | Không tạo cảnh báo riêng |

Nếu DHT11 đọc lỗi, firmware giữ giá trị hợp lệ gần nhất để tránh tạo cảnh báo giả.

### MQ-2

Firmware đọc 5 lần rồi lấy trung bình để giá trị bớt dao động.

| Giá trị ADC trung bình | Trạng thái |
|---:|---|
| `< 1300` | SAFE |
| `1300 đến dưới 1600` | WARNING |
| `>= 1600` | DANGER |

Giá trị này là ADC dùng cho demo, không phải nồng độ ppm.

### JSN-SR04T đo mực nước

```text
Mực nước = 100 cm - khoảng cách đến mặt nước
```

| Khoảng cách | Mực nước | Trạng thái |
|---:|---:|---|
| `> 40 cm` | `< 60 cm` | SAFE |
| `> 30 đến 40 cm` | `60 đến dưới 70 cm` | WARNING |
| `23 đến 30 cm` | `70 đến 77 cm` | DANGER |

- Khoảng cách dưới 23 cm nằm trong vùng mù nên được giới hạn thành 23 cm và xem là DANGER.
- Nếu cảm biến không nhận được echo, `distanceCm` và `waterLevelCm` được gửi là `null`, không đổi thành `0`.

### Trạng thái tổng hợp và LED

Hệ thống lấy mức cao nhất trong nhiệt độ, gas, nước và motion nếu có dữ liệu F7.

| System | LED xanh | LED vàng | LED đỏ |
|---|---:|---:|---:|
| SAFE | Bật | Tắt | Tắt |
| WARNING | Tắt | Bật | Tắt |
| DANGER | Tắt | Tắt | Bật |

## 4. Logic buzzer — Alarm Event Mute

| System | `buzzerMuted` | Buzzer |
|---|---:|---|
| SAFE | Tự đặt lại `false` | OFF |
| WARNING | Giữ nguyên | OFF |
| DANGER | `false` | ON |
| DANGER | `true` | OFF |

Ý nghĩa nút trên website:

| Lệnh | Khi nào | Kết quả |
|---|---|---|
| OFF | Đang DANGER | Tắt tiếng sự kiện hiện tại, nhưng System vẫn DANGER |
| OFF | SAFE hoặc WARNING | Không tạo trạng thái tắt còi vĩnh viễn |
| ON | Đang DANGER | Xóa mute và bật lại còi |
| ON | SAFE hoặc WARNING | Xóa mute nhưng còi vẫn OFF |

Mute chỉ được xóa tự động khi toàn bộ hệ thống trở về SAFE. Vì vậy một DANGER mới vẫn bật còi bình thường.

## 5. Chuẩn bị PostgreSQL

Tạo database mới, ví dụ:

```sql
CREATE DATABASE disaster_warning;
```

Khởi động backend. SQLAlchemy models và `create_tables()` sẽ tạo `sensor_readings` trên database trống. `infrastructure/database/schema.sql` chỉ là bản tham khảo hoặc lựa chọn setup thủ công, không phải bước bắt buộc thêm.

Nếu đã từng chạy schema cũ, phải recreate database hoặc migrate thủ công trước. `create_all()` không tự đổi table đã tồn tại. Với database demo không cần giữ dữ liệu, hãy backup nếu cần rồi tạo lại database trống.

## 6. Cấu hình `.env`

Sao chép `.env.example` thành `.env`, sau đó điền:

```dotenv
DATABASE_URL=postgresql://postgres:<password>@localhost:5432/disaster_warning
VITE_API_BASE_URL=http://127.0.0.1:8000

MQTT_BROKER_HOST=127.0.0.1
MQTT_BROKER_PORT=1883

THINGSPEAK_WRITE_API_KEY=<Write API Key>
THINGSPEAK_CHANNEL_ID=<Channel ID>
THINGSPEAK_READ_API_KEY=<Read API Key nếu channel private>

PUSHSAFER_PRIVATE_KEY=<Private Key>
PUSHSAFER_DEVICE_ID=a
```

API key chỉ đặt trong `.env`, không ghi vào file Python hoặc commit lên Git.

## 7. Cấu hình ThingSpeak

Tạo một channel và đặt tên các field:

| ThingSpeak field | Dữ liệu |
|---|---|
| Field 1 | Temperature |
| Field 2 | Humidity |
| Field 3 | Gas ADC |
| Field 4 | Water Level |
| Field 5 | Motion Status |
| Field 6 | System Status |
| Field 7 | F7 Vibration |

Trạng thái được đổi thành số để vẽ biểu đồ:

| Trạng thái | Giá trị |
|---|---:|
| SAFE | 0 |
| WARNING | 1 |
| DANGER | 2 |

ESP32 gửi MQTT mỗi 2 giây, nhưng backend chỉ gửi ThingSpeak tối đa một lần mỗi 15 giây để tránh rate limit.

Hướng dẫn tạo channel, lấy API key và cấu hình Pushsafer nằm tại
[Cài đặt ThingSpeak và Pushsafer](cloud-notifications-setup.md).

## 8. Cấu hình và nạp ESP32 Main

Mở [main_station.ino](../firmware/main_station/main_station.ino) bằng Arduino IDE và sửa:

```cpp
const char* HOME_WIFI_SSID = "YOUR_WIFI_SSID";
const char* HOME_WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MQTT_HOST = "IP_MAY_TINH_CHAY_MOSQUITTO";
```

Chọn đúng board và cổng, sau đó `Verify` và `Upload`. Serial Monitor dùng `115200 baud`.

## 9. Cấu hình và kiểm tra F7

Mở `firmware/f7_station/f7_station.ino` và điền cùng Wi-Fi, MQTT broker với Main:

```cpp
const char* HOME_WIFI_SSID = "YOUR_WIFI_SSID";
const char* HOME_WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MQTT_HOST = "IP_MAY_TINH_CHAY_MOSQUITTO";
```

F7 đọc MPU6050 và publish MQTT mỗi 2 giây. Frontend gọi API mỗi 2 giây nên dữ liệu F7 tự cập nhật khi luồng `F7 -> MQTT -> Backend -> Web` hoạt động.

Kiểm tra đường truyền MQTT:

1. Đảm bảo Main, F7 và laptop dùng cùng Wi-Fi.
2. Điền `MQTT_HOST` là IPv4 hiện tại của laptop trong cả hai firmware.
3. Bật Mosquitto.
4. Subscribe toàn bộ topic:

```powershell
mosquitto_sub -h <IP_LAPTOP> -p 1883 -t "disaster/#" -v
```

5. Bật F7 và kiểm tra:

```text
disaster/f7/status online
disaster/f7/state SAFE
disaster/f7/telemetry {...}
```

6. Bật Main, sau đó nghiêng hoặc rung F7.
7. Main phải in `[MQTT] Motion -> WARNING` hoặc `[MQTT] Motion -> DANGER`.

Khi mất Wi-Fi, Main vẫn đọc cảm biến local và điều khiển LED/buzzer. F7 vẫn đọc MPU6050 nhưng tạm thời không gửi được dữ liệu cho Main hoặc backend.

## 10. Chạy chương trình

Backend:

```powershell
cd backend
python app.py
```

Frontend, trong terminal khác:

```powershell
cd frontend
npm install
npm run dev
```

Mở `http://localhost:5173`.

## 11. Retrain AI từ ThingSpeak

Sau khi ThingSpeak có ít nhất 100 bản ghi hợp lệ:

```powershell
python ai/retrain.py
```

AI đọc Field 1, 3 và 4, bỏ các mẫu thiếu dữ liệu và lưu model tại:

```text
backend/app/ml_models/disaster_model.pkl
```

Khởi động lại backend sau khi retrain để nạp model mới.

Random Forest hiện là classifier demo. Nhãn train được sinh từ cùng các rule nguy hiểm của firmware (nhiệt độ, gas và nước), không phải ground-truth thiên tai độc lập. F5 ngoại suy sensor đến +5 phút trước rồi mới dùng classifier đánh giá rủi ro.

## 12. Kiểm tra nhanh trước khi demo

| Kiểm tra | Kết quả mong đợi |
|---|---|
| ESP32 Serial | Có Wi-Fi, MQTT và telemetry JSON |
| Backend `/api/health` | PostgreSQL, MQTT và ThingSpeak có trạng thái |
| PostgreSQL | Bảng `sensor_readings` tăng bản ghi |
| ThingSpeak | Field 1–7 cập nhật khoảng 15 giây/lần; Field 7 là độ rung F7 |
| Pushsafer | Có thông báo và nguyên nhân khi hệ thống mới chuyển sang DANGER |
| Website | Hiện nhiệt độ, gas, nước, System, Buzzer và Mute |
| Nhấn OFF khi DANGER | Còi tắt nhưng System vẫn DANGER |
| System trở về SAFE | `buzzerMuted=false` |
