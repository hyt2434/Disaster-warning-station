# MQTT Contract — Disaster Warning Station

Tài liệu này mô tả đúng dữ liệu đang được firmware, backend và frontend sử dụng.

## 1. Quy ước chung

- Topic gốc: `disaster`
- QoS đang dùng: `1`
- Telemetry gửi định kỳ và không retain.
- Trạng thái online/offline và trạng thái còi được retain.
- Lệnh điều khiển còi không được retain để ESP32 không nhận lại lệnh cũ sau khi khởi động.
- ESP32 Main là nơi duy nhất quyết định trạng thái nguy hiểm và điều khiển còi.
- Backend và AI chỉ nhận, lưu, dự đoán và hiển thị dữ liệu; không tự bật còi.

## 2. Danh sách topic

| Topic | Nơi gửi | Nơi nhận | Payload | Retain |
|---|---|---|---|---|
| `disaster/main/telemetry` | ESP32 Main | Backend | JSON | Không |
| `disaster/main/status` | ESP32 Main | Backend | `online` / `offline` | Có |
| `disaster/main/command/buzzer` | Backend | ESP32 Main | `ON` / `OFF` | Không |
| `disaster/main/state/buzzer` | ESP32 Main | Backend | `ON` / `OFF` | Có |
| `disaster/f7/telemetry` | ESP32 F7 | Backend | JSON | Không |
| `disaster/f7/state` | ESP32 F7 | ESP32 Main | `NORMAL` / `WARNING` / `DANGER` | Có |
| `disaster/f7/status` | ESP32 F7 | Backend | `online` / `offline` | Có |
| `disaster/backend/status` | Backend | Broker/client khác | JSON online/offline | Có |

## 3. Telemetry của ESP32 Main

Topic:

```text
disaster/main/telemetry
```

Payload hiện tại:

```json
{
  "temperature": 33.2,
  "humidity": 69.3,
  "gas": 942,
  "distanceCm": 51.4,
  "waterLevelCm": 48.6,
  "motion": "SAFE",
  "motionSource": "MQTT",
  "motionRoll": 1.2,
  "motionPitch": 2.1,
  "motionTilt": 3.0,
  "motionVibration": 0.2,
  "motionImpact": 0.3,
  "system": "DANGER",
  "buzzer": false,
  "buzzerMuted": true
}
```

Ý nghĩa các trường:

| Trường | Kiểu dữ liệu | Ý nghĩa |
|---|---|---|
| `temperature` | number | Nhiệt độ từ DHT |
| `humidity` | number | Độ ẩm từ DHT |
| `gas` | number | Giá trị ADC đã lấy trung bình từ MQ-2 |
| `distanceCm` | number hoặc null | Khoảng cách từ cảm biến đến mặt nước |
| `waterLevelCm` | number hoặc null | Mực nước tính từ đáy |
| `motion` | string | Mức chuyển động do F7 gửi sang |
| `motionSource` | string | `MQTT`, `DIRECT` hoặc `NONE` |
| `motionRoll`, `motionPitch` | number | Góc F7 nhận qua UDP local |
| `motionTilt` | number | Độ lệch nghiêng của F7 |
| `motionVibration`, `motionImpact` | number | Mức rung và va chạm của F7 |
| `system` | string | Trạng thái tổng hợp: `SAFE`, `WARNING`, `DANGER` |
| `buzzer` | boolean | Trạng thái thật của còi |
| `buzzerMuted` | boolean | Sự kiện cảnh báo hiện tại đã bị tắt tiếng hay chưa |

Khi cảm biến nước không nhận được echo, ESP32 phải gửi:

```json
{
  "distanceCm": null,
  "waterLevelCm": null
}
```

Backend và database phải giữ nguyên `null`. Không được đổi thành `0`, vì `0` là một giá trị đo có ý nghĩa khác.

Ba trạng thái sau độc lập với nhau:

```json
{
  "system": "DANGER",
  "buzzer": false,
  "buzzerMuted": true
}
```

Payload trên hoàn toàn hợp lệ: hệ thống vẫn nguy hiểm nhưng người dùng đã tắt tiếng sự kiện hiện tại.

## 4. Logic Alarm Event Mute

ESP32 Main xử lý theo bảng sau:

| Trạng thái hệ thống | `buzzerMuted` | Kết quả còi |
|---|---:|---|
| `SAFE` | bất kỳ | Tắt còi và đặt lại mute thành `false` |
| `WARNING` | giữ nguyên | Tắt còi |
| `DANGER` | `false` | Bật còi |
| `DANGER` | `true` | Tắt còi |

Lệnh `OFF`:

- Nếu đang `DANGER`: đặt `buzzerMuted = true` và tắt còi.
- Nếu không phải `DANGER`: không tạo mute kéo dài sang sự kiện sau.

Lệnh `ON`:

- Xóa mute bằng cách đặt `buzzerMuted = false`.
- Chỉ bật còi nếu hệ thống vẫn đang `DANGER`.
- Không được ép còi bật khi hệ thống là `SAFE` hoặc `WARNING`.

Khi hệ thống trở về `SAFE`, ESP32 xóa mute. Vì vậy một sự kiện `DANGER` mới vẫn có thể bật còi.

## 5. Điều khiển còi từ website

Frontend không gửi MQTT trực tiếp. Luồng xử lý là:

```text
Frontend
  -> POST /api/devices/main/buzzer
  -> Backend
  -> disaster/main/command/buzzer
  -> ESP32 Main
```

Payload MQTT chỉ là một trong hai chuỗi:

```text
ON
```

```text
OFF
```

Topic lệnh phải dùng `retain = false`.

## 6. Trạng thái thật của còi

Topic:

```text
disaster/main/state/buzzer
```

Payload:

```text
ON
```

hoặc:

```text
OFF
```

ESP32 gửi topic này sau khi trạng thái còi thật thay đổi và sau khi kết nối lại MQTT. Topic này có thể retain để backend mới kết nối biết trạng thái gần nhất.

Backend chỉ cập nhật `Buzzer State` từ topic này hoặc từ telemetry. Backend không được suy luận rằng `system = DANGER` thì chắc chắn còi đang bật.

## 7. Trạng thái online/offline

ESP32 Main:

```text
disaster/main/status
```

ESP32 F7:

```text
disaster/f7/status
```

Payload:

```text
online
```

hoặc:

```text
offline
```

Các topic này dùng retain và Last Will để backend nhận biết thiết bị mất kết nối.

## 8. Telemetry và state của ESP32 F7

F7 tiếp tục dùng payload hiện có. Backend chấp nhận các trường chuyển động như:

```json
{
  "deviceId": "f7-01",
  "roll": 1.2,
  "pitch": 2.1,
  "tilt": 3.0,
  "vibration": 0.2,
  "impact": 0.3,
  "status": "NORMAL"
}
```

F7 gửi trạng thái cần thiết qua `disaster/f7/state` để ESP32 Main tổng hợp vào trường `motion` và `system`.

Ngoài MQTT, F7 luôn tạo Wi-Fi `DISASTER_F7_DIRECT`. Khi Main mất Wi-Fi nhà, Main kết nối vào mạng này và nghe UDP cổng `4210`. Payload local là một dòng CSV đơn giản:

```text
STATUS,ROLL,PITCH,TILT,VIBRATION,IMPACT
```

Ví dụ:

```text
WARNING,1.2,2.1,12.0,1.35,2.10
```

Đường UDP chỉ phục vụ cảnh báo local giữa F7 và Main. MQTT vẫn là đường đưa dữ liệu lên backend và website.

Khi backend nhận `disaster/f7/telemetry`, dữ liệu được:

1. lưu vào bảng PostgreSQL `f7_readings`;
2. giữ làm dữ liệu F7 mới nhất để ghép vào bản ghi `sensor_readings` tiếp theo;
3. gửi độ rung lên Field 7 của channel ThingSpeak chung tối đa 15 giây/lần; nếu Main offline thì bản ghi Cloud chỉ có Field 7;
4. dùng Field 5 và Field 7 để dự đoán trạng thái hệ thống sau 5 phút;
5. cập nhật API `/api/devices/f7/latest` và `/api/devices/f7/readings`.

## 9. Lưu dữ liệu

Backend đổi tên camelCase từ ESP32 sang snake_case khi lưu:

| ESP32 | PostgreSQL | ThingSpeak |
|---|---|---|
| `distanceCm` | `distance_cm` | Không gửi |
| `waterLevelCm` | `water_level_cm` | Field 4 |
| `motion` | `motion_status` | Field 5: SAFE=0, WARNING=1, DANGER=2 |
| `system` | `status` | Field 6: SAFE=0, WARNING=1, DANGER=2 |
| `buzzer` | `buzzer` | Không gửi |
| `buzzerMuted` | `buzzer_muted` | Không gửi |

ThingSpeak còn dùng Field 1 cho nhiệt độ, Field 2 cho độ ẩm và Field 3 cho gas. Backend giới hạn một lần gửi mỗi 15 giây. Nếu PostgreSQL hoặc ThingSpeak lỗi, MQTT control vẫn hoạt động độc lập.

## 10. Quy tắc cho AI

Mô hình hiện tại chỉ dùng dữ liệu cảm biến:

```text
temperature
humidity
gas_filtered
water_danger
```

AI không dùng `system`, `buzzer` hoặc `buzzerMuted` làm feature. AI chỉ dự đoán rủi ro và tuyệt đối không publish lệnh `ON` cho còi.

Nếu dữ liệu nước là `null`, backend bỏ qua lần dự đoán đó và báo `insufficient_data`. Không được đổi `null` thành `0`.

## 11. Các trường hợp cần kiểm tra khi demo

1. `SAFE` -> còi tắt, mute được xóa.
2. `DANGER` mới -> còi bật.
3. Nhấn tắt tiếng trong `DANGER` -> hệ thống vẫn `DANGER`, còi tắt, mute là `true`.
4. Telemetry `DANGER` lặp lại -> còi vẫn tắt nếu đã mute.
5. `DANGER -> WARNING -> DANGER` khi chưa về `SAFE` -> mute vẫn được giữ.
6. `DANGER -> SAFE -> DANGER` -> mute được xóa ở `SAFE`, sự kiện mới bật còi.
7. Nhấn `ON` khi `SAFE` -> xóa mute nhưng không bật còi.
8. Cảm biến nước lỗi -> API và PostgreSQL giữ `null`; backend bỏ qua Field 4 khi gửi ThingSpeak.

Đây là contract hiện tại. Nếu thay đổi tên trường hoặc topic, phải cập nhật đồng thời firmware, backend, frontend và tài liệu này.
