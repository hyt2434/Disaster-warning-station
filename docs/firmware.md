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
            Frontend   Cloud DB   Notification
                        / AI-DS     / Push service
```

---

## 3. Cấu trúc project

```text
firmware/
├── main_station/
│   └── main_station.ino
├── f7_station/
│   └── f7_station.ino
└── tools/
    ├── connect_wifi_main.cpp
    └── connect_wifi_c3.cpp
```

Hai firmware được upload riêng cho từng board. Các file trong `tools/` chỉ dùng để kiểm tra kết nối Wi-Fi/MQTT độc lập.

---

# PHẦN A - ESP32-S3 MAIN

## 4. Chức năng của Main

| Chức năng | Thiết bị | Firmware xử lý |
|---|---|---|
| F1 | DHT11 | Nhiệt độ + độ ẩm |
| F2 | Buzzer | AUTO local + MANUAL từ web/MQTT |
| F3 | MQ-2 | ADC raw + moving average + trạng thái gas |
| F6 | JSN-SR04T + LED | Khoảng cách -> mực nước -> 4 cấp LED |
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
| MQ-2 | 150 ms |
| JSN-SR04T | 500 ms |
| LED | liên tục |
| Buzzer | liên tục |
| MQTT loop | liên tục |
| Telemetry | 2000 ms |
| MQTT retry | 5000 ms |
| Wi-Fi retry | 10000 ms |

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

Nếu đọc lỗi `NaN`:

- không crash;
- không overwrite giá trị hợp lệ cuối cùng;
- trường `dhtLastReadValid = false` được gửi trong telemetry;
- chu kỳ tiếp theo tự thử lại.

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
 gasFiltered
      |
      v
Compare with demo thresholds
      |
      +--> SAFE
      +--> WARNING
      +--> DANGER
```

Firmware **không gọi dữ liệu là ppm**, vì chưa có calibration khí chuẩn.

Serial Monitor in cả 5 mẫu và giá trị trung bình để hỗ trợ calibration.

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

Nếu không nhận được echo, firmware giữ trạng thái nước hợp lệ trước đó. Khoảng cách
nhỏ hơn `23 cm` được clamp về `23 cm`, tương ứng mức nước tối đa đo được là `77 cm`.

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

## 10. Logic buzzer

Buzzer có hai nguồn yêu cầu:

```text
AUTO safety
    OR
MANUAL web command
    |
    v
FINAL BUZZER STATE
```

Trong code:

```cpp
bool shouldBeOn = autoDanger() || manualBuzzerOn;
```

`autoDanger()` hiện được định nghĩa là:

```text
Gas == DANGER
    OR
Water == CRITICAL
```

Điều này có nghĩa:

- Website gửi `OFF` **không thể tắt một cảnh báo AUTO đang nguy hiểm**.
- Khi gas/water về vùng an toàn và manual cũng OFF, buzzer tự tắt.
- Mất Wi-Fi/MQTT không ảnh hưởng AUTO buzzer.

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

Code cũng chấp nhận `1/0` và `TRUE/FALSE`.

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
  "gasRaw": 1125,
  "gasFiltered": 1120,
  "distanceCm": 65.4,
  "waterLevelCm": 34.6,
  "waterLevelPercent": 34.6,
  "motionStatus": "SAFE",
  "motionSource": "MQTT",
  "motionTilt": 1.2,
  "motionVibration": 0.18,
  "motionImpact": 0.42,
  "systemStatus": "WARNING",
  "buzzer": false
}
```

### Publish buzzer state / ACK

```text
disaster/main/state/buzzer
```

Ví dụ:

```json
{
  "deviceId": "main-01",
  "state": true,
  "mode": "AUTO",
  "reason": "GAS_DANGER",
  "manualRequest": false,
  "autoDanger": true
}
```

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
  +--> ax ay az
  +--> gx gy gz
        |
        v
   50 Hz sampling
        |
        +--> Tilt
        +--> Vibration RMS
        +--> Impact
                |
                v
       NORMAL/WARNING/DANGER
                |
                +--> MQTT telemetry
                +--> MQTT alert
```

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
   - baseline acceleration magnitude;
   - gyro bias.

Nhờ vậy, node có thể được lắp ở một orientation không hoàn toàn bằng phẳng mà vẫn đo độ nghiêng **so với tư thế ban đầu**.

---

## 15. Tilt

Từ vector gravity:

```text
roll  = atan2(ay, az)
pitch = atan2(-ax, sqrt(ay^2 + az^2))
```

Firmware so sánh `roll/pitch` hiện tại với baseline và lấy độ lệch lớn hơn làm `tiltAngleDeg`.

Ngưỡng mặc định:

| State | Enter | Exit |
|---|---:|---:|
| WARNING | 10 deg | 8 deg |
| DANGER | 20 deg | 15 deg |

Đây là ngưỡng khởi đầu, không phải giá trị đã kiểm định.

---

## 16. Vibration

Firmware không dùng một raw sample đơn lẻ.

```text
A = sqrt(ax^2 + ay^2 + az^2)
        |
        v
A - baselineA
        |
        v
RMS window 25 samples (~0.5 s)
        |
        v
vibrationRms
```

Ngưỡng mặc định:

| State | Enter | Exit |
|---|---:|---:|
| WARNING | 1.20 m/s^2 | 0.80 m/s^2 |
| DANGER | 2.50 m/s^2 | 1.80 m/s^2 |

Cần thu dữ liệu thực tế ở trạng thái:

- kệ đứng yên;
- rung nhẹ bình thường;
- rung đáng ngờ;
- rung mạnh;

sau đó mới chốt threshold cuối.

---

## 17. Impact

Firmware tính:

```text
impactDelta = abs(accelerationMagnitude - baselineAccelerationMagnitude)
```

Nếu:

```text
impactDelta >= 10 m/s^2
```

thì tạo event `IMPACT` và latch DANGER trong khoảng 1.5 giây.

Ngưỡng này cũng phải hiệu chỉnh bằng thử nghiệm va chạm thực tế an toàn.

---

## 18. Alert F7

Topic:

```text
disaster/f7/alert
```

Ví dụ:

```json
{
  "deviceId": "f7-01",
  "type": "TILT",
  "level": "DANGER",
  "tiltAngleDeg": 24.3,
  "vibrationRms": 0.31,
  "impactDelta": 0.22
}
```

Alert có cooldown 10 giây để tránh spam.

Nếu node phát hiện event khi mất MQTT, firmware giữ **1 alert gần nhất trong RAM** và thử publish sau khi kết nối lại.

Backend sau đó mới gọi PushSafer/IFTTT/notification service. API key notification không nằm trong ESP.

---

## 19. Telemetry F7

Topic:

```text
disaster/f7/telemetry
```

Firmware đọc MPU6050 ở 50 Hz nhưng chỉ publish 1 Hz.

Ví dụ:

```json
{
  "deviceId": "f7-01",
  "accelX": 0.1,
  "accelY": 0.2,
  "accelZ": 9.7,
  "gyroX": 0.01,
  "gyroY": 0.02,
  "gyroZ": 0.01,
  "rollDeg": 1.2,
  "pitchDeg": -2.1,
  "tiltAngleDeg": 3.0,
  "vibrationRms": 0.12,
  "impactDelta": 0.15,
  "tiltDanger": false,
  "vibrationDanger": false,
  "impactDanger": false,
  "status": "NORMAL",
  "batterySupported": false,
  "batteryVoltage": null,
  "batteryPercent": null
}
```

---

## 20. Battery F7 - không được hiển thị % giả

Với wiring hiện tại, pin 18650 chỉ cấp nguồn vào chân battery của XIAO; **không có mạch đo điện áp pin nối về ADC**.

Vì vậy firmware cố ý gửi:

```json
{
  "batterySupported": false,
  "batteryVoltage": null,
  "batteryPercent": null
}
```

Nếu muốn F8 hiển thị pin thật, cần bổ sung mạch chia áp từ battery sang một chân ADC còn trống rồi mới viết phần đo điện áp và ánh xạ sang phần trăm.

---

# PHẦN C - CÀI ĐẶT VÀ CHẠY

## 21. Libraries cần cài

### Cả hai ESP

- `PubSubClient`
- `ArduinoJson` **7.x**

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
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

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

### Bật buzzer manual

```bash
mosquitto_pub -h <BROKER_IP> -t disaster/main/command/buzzer -m ON
```

### Tắt manual request

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
13. Tạo rung có kiểm soát -> kiểm tra `vibrationRms`.
14. Tạo va chạm nhẹ, an toàn -> kiểm tra event `IMPACT`.
15. Tắt broker rồi bật lại -> xác nhận hai ESP tự reconnect.
16. Chạy soak test ít nhất 60 phút trước khi đóng hộp.

---

## 25. Những giá trị bắt buộc phải calibration trước demo cuối

Không nên coi các constant hiện tại là thông số cuối cùng.

Cần chốt bằng dữ liệu thật:

```text
MAIN
- SENSOR_HEIGHT_CM
- MIN_VALID_DISTANCE_CM
- GAS_WARNING_ENTER / EXIT
- GAS_DANGER_ENTER / EXIT
- WATER_WARNING_DISTANCE_CM
- WATER_DANGER_DISTANCE_CM

F7
- TILT_WARNING_ENTER / EXIT
- TILT_DANGER_ENTER / EXIT
- VIB_WARNING_ENTER / EXIT
- VIB_DANGER_ENTER / EXIT
- IMPACT_DELTA_THRESHOLD
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
filter + hysteresis
        |
        v
DECIDE
GasState + WaterState
        |
        +------> LOCAL SAFETY: LED + Buzzer
        |
        +------> MQTT telemetry

MQTT command -> manualBuzzerOn
AUTO OR MANUAL -> final buzzer
```

### F7

```text
READ MPU6050 @ 50 Hz
        |
        v
CALIBRATE / FILTER
        |
        v
Tilt + Vibration RMS + Impact
        |
        v
NORMAL / WARNING / DANGER
        |
        +------> MQTT telemetry @ 1 Hz
        |
        +------> MQTT alert on dangerous event
```

---

## 27. Trạng thái hiện tại của firmware

Hai file firmware đã có đầy đủ skeleton chạy thực tế gồm:

- sensor polling;
- filtering;
- hysteresis;
- local safety;
- Wi-Fi reconnect;
- MQTT reconnect;
- MQTT Last Will online/offline;
- JSON telemetry;
- buzzer command + state ACK;
- MPU6050 calibration;
- tilt/vibration/impact;
- alert cooldown;
- offline alert queue cho F7;
- timestamp UTC qua NTP khi mạng khả dụng.

Phần còn lại quan trọng nhất trước demo không phải thêm nhiều code, mà là **calibration threshold bằng dữ liệu thật và test end-to-end**.
