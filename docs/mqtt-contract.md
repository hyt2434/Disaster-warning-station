# MQTT Contract — Disaster Warning Station

Tài liệu này là contract duy nhất giữa hai firmware và backend. Không có alias field cũ.

## Quy ước

- Topic gốc: `disaster`.
- Telemetry/state F7 dùng `SAFE`, `WARNING`, `DANGER`.
- Telemetry định kỳ không retain; status và buzzer state được retain.
- ESP32 Main là nơi quyết định system, LED và buzzer. Backend/AI chỉ lưu, dự đoán, thông báo và hiển thị.
- Main và F7 cùng kết nối Home Wi-Fi và Mosquitto; không có đường truyền trực tiếp riêng giữa hai ESP.

## Topic, QoS và retain

| Topic | Publisher → Subscriber | Payload | QoS thực tế | Retain |
|---|---|---|---:|---:|
| `disaster/main/telemetry` | Main → Backend | JSON | 0 | Không |
| `disaster/main/status` | Main → Backend | `online` / Last Will `offline` | publish 0; Last Will 1 | Có |
| `disaster/main/command/buzzer` | Backend → Main | `ON` / `OFF` | 1; Main subscribe QoS 1 | Không |
| `disaster/main/state/buzzer` | Main → Backend | `ON` / `OFF` | 0 | Có |
| `disaster/f7/telemetry` | F7 → Backend | JSON | 0 | Không |
| `disaster/f7/state` | F7 → Main | `SAFE` / `WARNING` / `DANGER` | 0 | Có |
| `disaster/f7/status` | F7 → Backend | `online` / Last Will `offline` | publish 0; Last Will 1 | Có |
| `disaster/backend/status` | Backend → client khác | JSON online/offline | 1 | Có |

PubSubClient publish không truyền QoS nên các publish thông thường của firmware là QoS 0. Không mô tả tất cả topic là QoS 1.

## Main telemetry

Topic `disaster/main/telemetry`:

```json
{
  "deviceId": "main-station-01",
  "temperature": 33.2,
  "humidity": 69.3,
  "gas": 942,
  "distanceCm": 51.4,
  "waterLevelCm": 48.6,
  "motion": "SAFE",
  "system": "DANGER",
  "buzzer": false,
  "buzzerMuted": true
}
```

| Field | Ý nghĩa |
|---|---|
| `deviceId` | ID cố định của Main |
| `temperature`, `humidity` | DHT11 |
| `gas` | trung bình 5 mẫu ADC MQ-2; không phải ppm |
| `distanceCm`, `waterLevelCm` | phép đo JSN-SR04T hoặc `null` khi không có echo |
| `motion` | trạng thái F7 mà Main nhận để tính system |
| `system` | mức cao nhất của temperature/gas/water/motion |
| `buzzer` | trạng thái phần cứng thật |
| `buzzerMuted` | người dùng đã tắt tiếng alarm event hiện tại |

Main không publish roll/pitch/tilt/vibration/impact. Backend nhận chi tiết trực tiếp từ topic F7. Backend từ chối payload Main thiếu field bắt buộc thay vì tạo số 0 giả.

Ba trạng thái này độc lập và hoàn toàn hợp lệ:

```json
{ "system": "DANGER", "buzzer": false, "buzzerMuted": true }
```

## Alarm Event Mute

| System | `buzzerMuted` | Buzzer |
|---|---:|---|
| `SAFE` | tự reset `false` | OFF |
| `WARNING` | giữ nguyên | OFF |
| `DANGER` | `false` | ON |
| `DANGER` | `true` | OFF |

`OFF` chỉ mute sự kiện DANGER hiện tại. `ON` xóa mute và chỉ làm còi kêu nếu system vẫn DANGER. Command không retain để không phát lại sau reboot.

## F7 telemetry và state

Topic `disaster/f7/telemetry`:

```json
{
  "deviceId": "f7-station-01",
  "roll": 1.2,
  "pitch": 2.1,
  "tilt": 3.0,
  "vibration": 0.2,
  "impact": 0.3,
  "status": "SAFE"
}
```

F7 đồng thời publish `SAFE`, `WARNING` hoặc `DANGER` lên `disaster/f7/state` để Main tổng hợp local safety.

Main và F7 cùng kết nối Home Wi-Fi và Mosquitto. F7 gửi trạng thái chuyển động cho Main qua `disaster/f7/state`; không có đường truyền trực tiếp riêng giữa hai ESP.

Backend cache telemetry chi tiết tối đa 10 giây. Khi xử lý một Main telemetry:

- F7 còn fresh: ghi các giá trị vào `f7_*`;
- F7 thiếu hoặc stale: ghi toàn bộ `f7_* = null`;
- `motion` của Main vẫn dùng cho ThingSpeak Field 5 nhưng không được ghi thay cho `f7_status`.

Khi Main offline, F7 không tạo row PostgreSQL riêng. Backend vẫn có thể gửi `vibration` lên ThingSpeak Field 7. Để tránh hai Pushsafer cho cùng một sự kiện, Main system DANGER chịu trách nhiệm notification khi Main còn online; F7 chỉ tự notification khi Main đã mất telemetry mới.

## Mapping lưu trữ

| MQTT | PostgreSQL | ThingSpeak |
|---|---|---|
| Main `gas` | `gas_average` | Field 3 |
| Main `waterLevelCm` | `water_level_cm` | Field 4 |
| Main `motion` | không lưu riêng | Field 5: SAFE=0, WARNING=1, DANGER=2 |
| Main `system` | `status` | Field 6: SAFE=0, WARNING=1, DANGER=2 |
| F7 `roll/pitch/tilt/vibration/impact/status` | `f7_*` | vibration ở Field 7 |
| Main `buzzer`, `buzzerMuted` | `buzzer`, `buzzer_muted` | không gửi |

Field 1 là temperature, Field 2 là humidity. Backend giới hạn upload ThingSpeak tối đa một lần mỗi 15 giây.

## AI/F5

F5 gồm hai bước:

```text
20 mẫu ThingSpeak → ngoại suy đại lượng vật lý đến +5 phút
                    → Random Forest classifier
                    → kết hợp ngưỡng F7
                    → SAFE / WARNING / DANGER
```

Model dùng `temperature`, `gas_average`, `water_level_cm`. Field 5 và 6 là category nên giữ giá trị mới nhất, không ngoại suy tuyến tính. AI không dùng `system`, buzzer hoặc mute làm feature và không publish lệnh bật còi.
