# Firmware ESP32

## 1. Mục tiêu hệ thống

Hệ thống gồm **2 vi điều khiển chạy 2 firmware độc lập**:

- **ESP32-S3 Main**: DHT11, MQ-2, JSN-SR04T, 3 LED, buzzer, Wi-Fi, MQTT.
- **XIAO ESP32-C3 F7**: MPU6050, Wi-Fi, MQTT, nguồn pin 18650.

Nguyên tắc kiến trúc quan trọng nhất:

> **Network là lớp truyền thông, không phải lớp an toàn.**
>
> ESP32-S3 Main vẫn phải đọc cảm biến, đổi LED và kích hoạt buzzer ngay cả khi Wi-Fi, MQTT, backend hoặc website bị mất kết nối.

Backend/website chịu trách nhiệm giám sát, lưu dữ liệu, hiển thị, điều khiển từ xa và gửi notification; không thay thế logic an toàn cục bộ.

---

## 2. Kiến trúc tổng quát

```text
Sensors / Outputs
      |
      v
+------------------+                  +------------------+
| ESP32-S3 MAIN    |                  | XIAO ESP32-C3 F7|
| DHT11            |                  | MPU6050          |
| MQ-2             |                  | Tilt/Vibration   |
| JSN-SR04T        |                  | Impact           |
| LED + Buzzer     |                  +--------+---------+
+--------+---------+                           |
         | MQTT                                | MQTT
         +----------------+--------------------+
                          v
                    Mosquitto Broker
                          |
                          v
                       Backend
               +----------+-----------+
               |          |           |
               v          v           v
            Frontend  PostgreSQL  ThingSpeak / AI
                                      + Pushsafer
```

---

## 3. Cấu trúc project

```text
firmware/
├── main_station/
│   └── main_station.ino
└── f7_station/
    └── f7_station.ino
```

Hai firmware được upload riêng cho từng board.

---

# PHẦN A - ESP32-S3 MAIN

## 4. Chức năng của Main

| Chức năng | Thiết bị | Firmware xử lý |
|---|---|---|
| F1 | DHT11 | Nhiệt độ + độ ẩm |
| F2 | Buzzer | Tự động theo DANGER + mute sự kiện hiện tại từ web/MQTT |
| F3 | MQ-2 | ADC raw + trung bình 5 mẫu + trạng thái gas |
| F6 | JSN-SR04T + LED | Khoảng cách -> mực nước -> 3 cấp LED |
| Network | Wi-Fi + MQTT | Telemetry, command, online/offline |

## 5. GPIO Main

| Thiết bị | GPIO |
|---|---:|
| DHT11 SIG | GPIO4 |
| MQ-2 AO | GPIO5 |
| JSN TRIG | GPIO6 |
| JSN ECHO | GPIO7 |
| Buzzer SIG | GPIO15 |
| LED xanh | GPIO16 |
| LED vàng | GPIO17 |
| LED đỏ | GPIO18 |

### Lưu ý điện áp

- `JSN-SR04T ECHO` phải đi qua **mạch chia áp** trước khi vào GPIO7 vì tín hiệu ECHO có thể ở mức 5 V.
- Với `MQ-2 AO`, phải đo điện áp output thực tế của module. Không được đưa điện áp vượt giới hạn ADC của ESP32 vào GPIO5.
- Tất cả GND phải nối chung.

---

## 6. Chu kỳ Main

| Task | Chu kỳ |
|---|---:|
| DHT11 | 2000 ms |
| MQ-2 | 2000 ms, lấy trung bình 5 mẫu mỗi lần đọc |
| JSN-SR04T | 2000 ms |
| LED | liên tục |
| Buzzer | liên tục |
| MQTT loop | liên tục |
| Telemetry | 2000 ms |
| MQTT retry | 5000 ms |
| Wi-Fi reconnect | Do ESP32 tự xử lý |

Firmware dùng `millis()` để scheduler các task. Không có `delay(5000)` hoặc vòng `while` reconnect làm đứng hệ thống.

`pulseIn()` của JSN vẫn là thao tác blocking ngắn, nhưng đã giới hạn timeout tối đa 30 ms cho mỗi lần đo.

---

## 7. DHT11

```text
DHT11
  |
  +--> temperature
  +--> humidity
```

Nếu đọc lỗi `NaN`, firmware giữ giá trị và mức an toàn hợp lệ gần nhất rồi tự thử lại ở chu kỳ tiếp theo. Firmware không biến một phép đo lỗi thành `SAFE` giả.

---

## 8. MQ-2

Luồng xử lý:

```text
analogRead(GPIO5)
      |
      v
    gasRaw
      |
      v
Average (5 samples)
      |
      v
    gasAverage
      |
      v
Compare with demo thresholds
      |
      +--> SAFE
      +--> WARNING
      +--> DANGER
```

Firmware **không gọi dữ liệu là ppm**, vì chưa có calibration khí chuẩn.

Serial Monitor in raw sample cuối và giá trị trung bình để hỗ trợ calibration.

### Ngưỡng mặc định trong code

| Trạng thái | Khoảng ADC |
|---|---:|
| SAFE | `< 1300` |
| WARNING | `1300 đến < 1600` |
| DANGER | `>= 1600` |

`1300/1600` là ngưỡng demo dựa trên mức nền quan sát được khoảng `1110-1130`; vẫn cần hiệu chỉnh bằng cảm biến thật.

---

## 9. JSN-SR04T và mực nước

Firmware đo:

```text
TRIG -> ultrasonic pulse -> ECHO duration -> distance
```

Sau đó:

```text
waterLevelCm = installationHeightCm - distanceCm
```

Ví dụ:

```text
installationHeight = 100 cm
distance           = 65 cm
waterLevel         = 35 cm
```

Nếu không nhận được echo, firmware đánh dấu phép đo không hợp lệ, giữ mức an toàn hợp lệ gần nhất
và publish `distanceCm: null`, `waterLevelCm: null`. Giá trị `null` không được backend đổi
thành `0`. Khoảng cách nhỏ hơn `23 cm` được clamp về `23 cm`,
tương ứng mức nước tối đa đo được là `77 cm`.

### Trạng thái LED

| Water state | LED |
|---|---|
| SAFE | Xanh |
| WARNING | Vàng |
| DANGER | Đỏ + buzzer |

### Ngưỡng mặc định

| State | Khoảng cách cảm biến | Mức nước tương ứng |
|---|---:|---:|
| SAFE | `> 40 cm` | `< 60 cm` |
| WARNING | `> 30 đến 40 cm` | `60 đến < 70 cm` |
| DANGER | `23 đến 30 cm` | `70 đến 77 cm` |

Ngưỡng nguy hiểm `70 cm` nằm trong khoảng đo trực tiếp tối đa `77 cm` của cấu hình hiện tại.

Đặc biệt phải sửa:

```cpp
const float SENSOR_HEIGHT_CM = 100.0;
const float MIN_VALID_DISTANCE_CM = 23.0;
```

thành chiều cao thật từ cảm biến xuống mốc `0 cm` của mô hình.

---

## 10. Logic Alarm Event Mute

ESP32 Main quản lý độc lập ba trạng thái:

```text
system        = mức nguy hiểm của cảm biến
buzzer        = trạng thái vật lý thực tế của còi
buzzerMuted   = người dùng đã tắt tiếng sự kiện hiện tại hay chưa
```

Quy tắc trong `updateBuzzer()`:

```text
SAFE                     -> buzzer OFF, reset buzzerMuted=false
WARNING                  -> buzzer OFF, giữ nguyên buzzerMuted
DANGER + buzzerMuted=false -> buzzer ON
DANGER + buzzerMuted=true  -> buzzer OFF
```

Lệnh `OFF` chỉ đặt `buzzerMuted=true` khi hệ thống đang DANGER. Lệnh `OFF` lúc SAFE hoặc
WARNING không tạo trạng thái tắt vĩnh viễn. Lệnh `ON` xóa mute; còi chỉ bật nếu hệ thống
đang DANGER. Vì vậy telemetry DANGER lặp lại không thể tự bật lại còi sau khi người dùng mute.

---

## 11. MQTT Main

### Subscribe

```text
disaster/main/command/buzzer
```

Payload hỗ trợ:

```text
ON
OFF
```

Firmware chỉ chấp nhận `ON` hoặc `OFF`. Command không được publish retained.

### Publish telemetry

```text
disaster/main/telemetry
```

Ví dụ:

```json
{
  "deviceId": "main-station-01",
  "temperature": 30.2,
  "humidity": 68.0,
  "gas": 1120,
  "distanceCm": 65.4,
  "waterLevelCm": 34.6,
  "motion": "SAFE",
  "system": "DANGER",
  "buzzer": false,
  "buzzerMuted": true
}
```

`system=DANGER`, `buzzer=false`, `buzzerMuted=true` là trạng thái hợp lệ: môi trường vẫn
nguy hiểm nhưng người dùng đã xác nhận và tắt âm thanh của sự kiện hiện tại.

### Publish buzzer state / ACK

```text
disaster/main/state/buzzer
```

Payload hiện tại là plain text `ON` hoặc `OFF`, biểu diễn đúng trạng thái vật lý của còi.

Topic này được publish `retained`, nên backend/frontend có thể lấy trạng thái gần nhất.

### Online/offline

```text
disaster/main/status
```

- connect thành công -> `online`
- MQTT Last Will -> `offline`

---

# PHẦN B - XIAO ESP32-C3 F7

## 12. Nhiệm vụ F7

```text
MPU6050
  |
  +--> đọc nhóm 20 mẫu mỗi 2 giây
  +--> tính roll, pitch, tilt, vibration, impact
  +--> lấy mức cao nhất: SAFE / WARNING / DANGER
  +--> MQTT disaster/f7/state -> Main
  +--> MQTT disaster/f7/telemetry -> Backend
```

Main và F7 cùng kết nối Home Wi-Fi và Mosquitto. F7 gửi mức chuyển động cho Main qua MQTT; dữ liệu chi tiết được gửi trực tiếp cho backend.

## 13. Pin MPU6050

| XIAO ESP32-C3 | MPU6050 |
|---|---|
| 3V3 | VCC |
| GND | GND |
| D4 / GPIO6 | SDA |
| D5 / GPIO7 | SCL |

---

## 14. Calibration khi boot

Khi MPU6050 khởi động:

1. lấy 100 samples;
2. sample mỗi 20 ms;
3. tổng thời gian khoảng 2 giây;
4. người dùng phải để hộp/kệ **đứng yên**;
5. firmware tính:
   - baseline roll;
   - baseline pitch;
   - không dùng baseline acceleration cho vibration/impact.

Nhờ vậy, node có thể được lắp ở một orientation không hoàn toàn bằng phẳng mà vẫn đo độ nghiêng **so với tư thế ban đầu**.

Nếu `mpu.begin()` thất bại, firmware ghi lỗi nhưng vẫn khởi động Wi-Fi/AP/MQTT. F7 chỉ đọc và publish motion sau khi MPU được tìm thấy và calibration hợp lệ; không còn vòng `while` vô hạn chặn toàn bộ network.

---

## 15. Tilt

Từ vector gravity:

```text
roll  = atan2(ay, az)
pitch = atan2(-ax, sqrt(ay^2 + az^2))
```

Firmware so sánh `roll/pitch` hiện tại với baseline và lấy độ lệch lớn hơn làm `tiltAngleDeg`.

Ngưỡng mặc định:

| State | Điều kiện |
|---|---:|
| SAFE | nhỏ hơn 15 deg |
| WARNING | từ 15 deg |
| DANGER | từ 30 deg |

Đây là ngưỡng khởi đầu, không phải giá trị đã kiểm định.

---

## 16. Vibration

Firmware không dùng một raw sample đơn lẻ mà phân tích 20 mẫu liên tiếp.

```text
A = sqrt(ax^2 + ay^2 + az^2) cho 20 mẫu
        |
        v
averageA của nhóm hiện tại
        |
        v
trung bình abs(A - averageA)
        |
        v
vibration
```

Ngưỡng mặc định:

| State | Điều kiện |
|---|---:|
| SAFE | nhỏ hơn 0.80 m/s^2 |
| WARNING | từ 0.80 m/s^2 |
| DANGER | từ 2.00 m/s^2 |

Cần thu dữ liệu thực tế ở trạng thái:

- kệ đứng yên;
- rung nhẹ bình thường;
- rung đáng ngờ;
- rung mạnh;

sau đó mới chốt threshold cuối.

---

## 17. Impact

Firmware tính độ lệch `abs(A - averageA)` của 20 mẫu và chọn độ lệch lớn thứ hai để bỏ qua một spike nhiễu đơn lẻ. Nếu:

```text
impactDelta >= 8 m/s^2
```

thì mức impact là `DANGER` trong lần đọc hiện tại. Firmware dùng thay đổi lớn thứ hai trong nhóm 20 mẫu để một mẫu nhiễu đơn lẻ không tạo báo động giả.

Ngưỡng này cũng phải hiệu chỉnh bằng thử nghiệm va chạm thực tế an toàn.

---

## 18. Alert F7

F7 publish trạng thái `SAFE`, `WARNING` hoặc `DANGER` lên `disaster/f7/state`. Backend nhận telemetry F7, sau đó mới quyết định gửi Pushsafer. API key notification không nằm trong ESP.

---

## 19. Telemetry F7

Topic:

```text
disaster/f7/telemetry
```

Firmware đọc một nhóm 20 mẫu và publish mỗi 2 giây.

Ví dụ:

```json
{
  "deviceId": "f7-station-01",
  "roll": 1.2,
  "pitch": -2.1,
  "tilt": 3.0,
  "vibration": 0.12,
  "impact": 0.15,
  "status": "SAFE"
}
```

---

## 20. Battery F7

Với wiring hiện tại, pin 18650 chỉ cấp nguồn vào chân battery của XIAO; **không có mạch đo điện áp pin nối về ADC**.

Firmware hiện không gửi field battery vì chưa có mạch đo điện áp nối vào ADC. Nếu muốn F8 hiển thị pin thật, cần bổ sung mạch chia áp từ battery sang một chân ADC còn trống rồi mới viết phần đo điện áp và ánh xạ sang phần trăm.

---

# PHẦN C - CÀI ĐẶT VÀ CHẠY

## 21. Libraries cần cài

### Cả hai ESP

- `PubSubClient`

### ESP32-S3 Main

- `DHT sensor library` by Adafruit
- `Adafruit Unified Sensor`

### XIAO ESP32-C3 F7

- `Adafruit MPU6050`
- `Adafruit BusIO`
- `Adafruit Unified Sensor`

ESP32 board package: `esp32` by Espressif Systems.

---

## 22. Cấu hình bắt buộc trước khi upload

Trong **cả hai** firmware, sửa:

```cpp
const char* HOME_WIFI_SSID = "YOUR_WIFI_SSID";
const char* HOME_WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* MQTT_HOST = "192.168.1.100";
const uint16_t MQTT_PORT = 1883;
```

Nếu Mosquitto có username/password:

```cpp
const char* MQTT_USER = "your_user";
const char* MQTT_PASSWORD = "your_password";
```

Nếu không có auth, giữ `""`.

---

## 23. Test MQTT nhanh

### Xóa mute và bật lại còi nếu hệ thống đang DANGER

```bash
mosquitto_pub -h <BROKER_IP> -t disaster/main/command/buzzer -m ON
```

### Tắt tiếng sự kiện DANGER hiện tại

```bash
mosquitto_pub -h <BROKER_IP> -t disaster/main/command/buzzer -m OFF
```

### Xem Main telemetry

```bash
mosquitto_sub -h <BROKER_IP> -t disaster/main/telemetry -v
```

### Xem F7 telemetry + alert

```bash
mosquitto_sub -h <BROKER_IP> -t 'disaster/f7/#' -v
```

---

## 24. Trình tự test nên làm

1. Test riêng ESP32-S3 boot + Serial.
2. Test DHT11.
3. Test MQ-2 raw và baseline trong không khí bình thường.
4. Test JSN khoảng cách ở 3-5 mốc đã biết.
5. Test 3 LED.
6. Test buzzer local.
7. Test Wi-Fi/MQTT.
8. Test command `ON/OFF` từ MQTT.
9. Rút Wi-Fi và xác nhận Main vẫn đổi LED/buzzer theo sensor.
10. Test XIAO + I2C MPU6050.
11. Giữ F7 yên 2 giây để calibration.
12. Nghiêng nhẹ -> WARNING, nghiêng mạnh -> DANGER.
13. Tạo rung có kiểm soát -> kiểm tra `vibration`.
14. Tạo va chạm nhẹ, an toàn -> kiểm tra `impact`.
15. Xác nhận telemetry F7 và frontend thay đổi mỗi 2 giây.
16. Subscribe `disaster/f7/#` và xác nhận status, state, telemetry.
17. Nghiêng/rung F7 và xác nhận Main nhận `[MQTT] Motion`.

---

## 25. Những giá trị bắt buộc phải calibration trước demo cuối

Không nên coi các constant hiện tại là thông số cuối cùng.

Cần chốt bằng dữ liệu thật:

```text
MAIN
- SENSOR_HEIGHT_CM
- MIN_VALID_DISTANCE_CM
- GAS_WARNING / GAS_DANGER
- WATER_WARNING_CM / WATER_DANGER_CM

F7
- TILT_WARNING_DEGREES / TILT_DANGER_DEGREES
- VIBRATION_WARNING / VIBRATION_DANGER
- IMPACT_DANGER
```

Nên lưu bảng calibration vào báo cáo để chứng minh ngưỡng không được chọn ngẫu nhiên.

---

## 26. Logic cuối cùng cần nhớ khi thuyết trình

### Main

```text
READ
DHT + MQ2 + Water
        |
        v
PROCESS
lấy trung bình + so sánh ngưỡng
        |
        v
DECIDE
Temperature + Gas + Water + Motion
        |
        +------> LOCAL SAFETY: LED + Buzzer
        |
        +------> MQTT telemetry

MQTT command OFF -> mute the current DANGER event
MQTT command ON  -> clear mute; buzzer sounds only while DANGER
SAFE             -> clear mute for the next event
```

### F7

```text
READ 20 MPU6050 SAMPLES
        |
        v
AVERAGE / COMPARE WITH BASELINE
        |
        v
Tilt + Vibration + Impact
        |
        v
SAFE / WARNING / DANGER
        |
        +------> MQTT state to Main every 2 seconds
        +------> MQTT telemetry to Backend every 2 seconds
```

---

## 27. Trạng thái hiện tại của firmware

Hai file firmware hiện có các phần chính:

- sensor polling;
- lấy trung bình cảm biến;
- local safety;
- Wi-Fi reconnect;
- MQTT reconnect;
- MQTT Last Will online/offline;
- JSON telemetry;
- buzzer command + state ACK;
- MPU6050 calibration;
- tilt/vibration/impact;
- chu kỳ telemetry 2 giây.

Phần còn lại quan trọng nhất trước demo không phải thêm nhiều code, mà là **calibration threshold bằng dữ liệu thật và test end-to-end**.
