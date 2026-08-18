# Hướng dẫn setup demo Disaster Warning Station

Tài liệu này áp dụng cho hai firmware trong repository:

- `firmware/main_station/main_station.ino`
- `firmware/f7_station/f7_station.ino`

The goal is to keep the demo simple while preserving this requirement:

> If the normal/home Wi‑Fi or MQTT path is lost, XIAO ESP32‑C3 must still send motion danger data directly to ESP32‑S3.

---

# 1. Architecture

## Normal mode

```text
MPU6050
   |
   v
XIAO ESP32-C3
   |
   | Home WiFi
   v
Mosquitto MQTT
   |
   +--------> FastAPI / React dashboard
   |
   +--------> ESP32-S3 MAIN
```

C3 publishes:

```text
disaster/f7/state
disaster/f7/telemetry
```

S3 subscribes:

```text
disaster/f7/state
```

---

## Wi‑Fi/MQTT failure mode

ESP32-S3 always creates its own private Wi‑Fi:

```text
SSID: DISASTER_MAIN_DIRECT
Password: 12345678
IP: 192.168.4.1
```

If C3 cannot use the normal Wi‑Fi for 5 seconds, or cannot reach MQTT for 10 seconds:

```text
Home WiFi lost
      |
      v
C3 disconnects from Home WiFi
      |
      v
C3 connects to:
DISASTER_MAIN_DIRECT
      |
      v
C3 sends UDP directly to S3
      |
      v
S3 still receives motion status
      |
      v
LED / buzzer still work
```

Nếu Wi‑Fi vẫn kết nối nhưng Mosquitto/backend MQTT bị tắt, C3 cũng chuyển sang cùng đường direct sau khoảng 10 giây.

No ESP-NOW is used.

No physical data wire is needed between C3 and S3.

---

# 2. Important design choice

ESP32-S3 runs:

```cpp
WIFI_AP_STA
```

This means it performs two Wi‑Fi roles at the same time.

## STA

`STA = Station`

S3 connects to your normal router/hotspot.

Used for:

```text
S3 -> Home WiFi -> Mosquitto
```

## AP

`AP = Access Point`

S3 creates its own Wi‑Fi network.

Used for:

```text
C3 -> S3 directly
```

The AP stays active even when the normal router disappears.

---

# 3. Required libraries

## Built into the ESP32 Arduino core

You do not need to install these separately:

```text
WiFi
WiFiUDP
Wire
```

## Install these libraries

### Both boards

```text
PubSubClient
```

### ESP32-S3 MAIN

```text
DHT sensor library by Adafruit
Adafruit Unified Sensor
```

### XIAO ESP32-C3

```text
Adafruit MPU6050
Adafruit Unified Sensor
```

---

# 4. Arduino IDE setup

Install the ESP32 board package.

For XIAO ESP32-C3, select the Seeed XIAO ESP32-C3 board if it is available in your installed board package.

For a generic ESP32-S3 development board, select the exact S3 board you are physically using. If you use a generic S3 DevKit, `ESP32S3 Dev Module` is commonly appropriate.

Do not choose a different board blindly if your actual board model is known.

---

# 5. Mở và upload bằng Arduino IDE

Firmware đã được đặt theo đúng cấu trúc sketch của Arduino IDE:

```text
firmware/main_station/main_station.ino
firmware/f7_station/f7_station.ino
```

Thực hiện riêng cho từng board:

1. Mở file `.ino` tương ứng bằng Arduino IDE.
2. Chọn đúng board trong `Tools > Board`.
3. Chọn đúng cổng trong `Tools > Port`.
4. Cài các thư viện ở mục 3 bằng `Library Manager`.
5. Sửa Wi-Fi và `MQTT_HOST` trước khi upload.
6. Nhấn `Verify`, sau đó nhấn `Upload`.
7. Mở `Serial Monitor` và chọn `9600 baud`.

Không mở hai sketch trong cùng một cửa sổ Arduino IDE và không upload nhầm firmware sang board còn lại.

---

# 6. XIAO ESP32-C3 <-> MPU6050 wiring

The firmware uses:

```cpp
MPU_SDA_PIN = 6;
MPU_SCL_PIN = 7;
```

Connect:

| MPU6050 | XIAO ESP32-C3 |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO6 / D4 |
| SCL | GPIO7 / D5 |

For a common MPU6050 breakout, 3.3 V is the simplest safe choice for this demo.

---

# 7. ESP32-S3 sensor wiring used by the firmware

| Device | ESP32-S3 |
|---|---:|
| DHT11 DATA | GPIO4 |
| MQ-2 analog | GPIO5 |
| JSN-SR04T TRIG | GPIO6 |
| JSN-SR04T ECHO | GPIO7 |
| Buzzer | GPIO15 |
| Green LED | GPIO16 |
| Yellow LED | GPIO17 |
| Red LED | GPIO18 |

## Very important: JSN-SR04T ECHO

If the ECHO pin outputs 5 V, do not connect it directly to an ESP32 GPIO.

Use a voltage divider so the ESP32 receives approximately 3.3 V or less.

## MQ-2 analog note

Some MQ-2 modules are powered from 5 V and their analog output may exceed the safe ADC input voltage of the ESP32.

Measure/check your module and use a voltage divider when necessary so the ESP32 ADC pin never receives more than its safe voltage.

---

# 8. Configure both code files

Các cấu hình dưới đây phải được điền trước khi upload hai board.

## A. Home Wi‑Fi

In both files:

```cpp
const char* HOME_WIFI_SSID = "YOUR_WIFI_SSID";
const char* HOME_WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

Example:

```cpp
const char* HOME_WIFI_SSID = "MyHotspot";
const char* HOME_WIFI_PASSWORD = "123456789";
```

---

## B. MQTT broker IP

In both files:

```cpp
const char* MQTT_HOST = "192.168.1.100";
```

Replace this with your computer's local IP.

On Windows:

```powershell
ipconfig
```

Find the IPv4 address of the adapter connected to the same router/hotspot.

Example:

```text
IPv4 Address: 192.168.1.25
```

Then use:

```cpp
const char* MQTT_HOST = "192.168.1.25";
```

`MQTT_HOST` là IP LAN của máy chạy Mosquitto, không thêm `http://` và không dùng `localhost` trên ESP32.

Nếu Mosquitto không dùng tài khoản, giữ trống trong cả hai firmware:

```cpp
const char* MQTT_USER = "";
const char* MQTT_PASSWORD = "";
```

Nếu broker có authentication, hai giá trị này phải trùng với tài khoản Mosquitto.

Backend cũng phải kết nối vào cùng broker. Với Mosquitto chạy trên chính máy backend, giữ trong `.env`:

```dotenv
MQTT_BROKER_HOST=127.0.0.1
MQTT_BROKER_PORT=1883
```

MongoDB Atlas là Cloud storage bắt buộc của demo. Thêm vào `.env`:

```dotenv
MONGODB_URI=mongodb+srv://<user>:<password>@<cluster>/?appName=<app>
MONGODB_DATABASE=disaster_db
MONGODB_COLLECTION=sensor_readings
```

Không ghi MongoDB URI trực tiếp vào file Python. Nếu Atlas tạm mất kết nối, backend lưu PostgreSQL, xếp telemetry vào hàng đợi RAM và tự đồng bộ lại lên MongoDB khi Cloud hoạt động.

Luồng dữ liệu khi chạy thành công:

```text
ESP32 Main
  -> disaster/main/telemetry
  -> Mosquitto
  -> FastAPI MQTT client
  -> PostgreSQL + MongoDB/AI
  -> React đọc PostgreSQL qua REST API
```

---

## C. Direct fallback Wi‑Fi

These values must be identical in both files.

S3:

```cpp
const char* DIRECT_AP_SSID = "DISASTER_MAIN_DIRECT";
const char* DIRECT_AP_PASSWORD = "12345678";
```

C3:

```cpp
const char* MAIN_AP_SSID = "DISASTER_MAIN_DIRECT";
const char* MAIN_AP_PASSWORD = "12345678";
```

The password must contain at least 8 characters.

---

## D. UDP port

S3:

```cpp
const int DIRECT_UDP_PORT = 4210;
```

C3:

```cpp
const int DIRECT_UDP_PORT = 4210;
```

They must match.

---

# 9. First test: S3 direct Wi‑Fi

Upload the S3 firmware first.

Open Serial Monitor at:

```text
9600 baud
```

You should see something similar to:

```text
[DIRECT] S3 fallback AP started.
[DIRECT] SSID: DISASTER_MAIN_DIRECT
[DIRECT] AP IP: 192.168.4.1
[DIRECT] UDP port: 4210
```

Now use your phone/laptop to scan Wi‑Fi.

You should see:

```text
DISASTER_MAIN_DIRECT
```

If you cannot see this SSID, do not continue to C3 fallback testing yet.

---

# 10. Second test: C3 + MPU6050

Upload the C3 firmware.

Immediately after boot:

```text
[MPU] CALIBRATION START
[MPU] Keep the sensor still for about 2 seconds.
```

Do not move the MPU6050 during these two seconds.

After calibration you should see values such as:

```text
Baseline Roll         : 87.20
Baseline Pitch        : -12.30
Baseline Acceleration : 9.79
```

The exact values do not need to be 0 degrees or exactly 9.81.

The baseline represents the physical mounting position of your sensor.

---

# 11. Third test: normal Wi‑Fi + MQTT

Keep your router/hotspot ON.

Start Mosquitto.

The C3 should show:

```text
NETWORK : HOME WIFI + MQTT
WiFi    : CONNECTED to <your WiFi>
MQTT    : Connected
```

The S3 should receive motion state through MQTT:

```text
[MQTT] Motion from C3: SAFE
```

or:

```text
[MQTT] Motion from C3: WARNING
```

or:

```text
[MQTT] Motion from C3: DANGER
```

---

# 12. MQTT topics

## C3 state

```text
disaster/f7/state
```

Payload:

```text
NORMAL
```

or:

```text
WARNING
```

or:

```text
DANGER
```

This is deliberately simple so S3 can parse it easily.

---

## C3 telemetry

```text
disaster/f7/telemetry
```

Example:

```json
{
  "roll": 87.5,
  "pitch": -12.4,
  "tilt": 4.2,
  "vibration": 0.35,
  "impact": 0.80,
  "status": "NORMAL"
}
```

---

## Main telemetry

```text
disaster/main/telemetry
```

This includes local sensor information and motion information received from C3.

---

## Manual buzzer

Web publishes:

```text
Topic:
disaster/main/command/buzzer
```

Payload:

```text
ON
```

or:

```text
OFF
```

---

## Connection status

Các status topic được publish dạng retained:

```text
disaster/main/status   -> online / offline
disaster/f7/status     -> online / direct / offline
disaster/backend/status -> online / offline
```

Nhờ retained message, backend hoặc công cụ MQTT mở sau vẫn đọc được trạng thái kết nối gần nhất.

---

# 13. Fourth test: simulate home Wi‑Fi failure

This is the important demo.

Start with everything working normally.

Then turn OFF the router/hotspot used by the ESPs.

Wait about 5 seconds.

C3 should show:

```text
[WiFi] Home WiFi unavailable.
[WiFi] SWITCH TO DIRECT MODE.
[WiFi] Connecting directly to S3 AP: DISASTER_MAIN_DIRECT
```

After it connects:

```text
NETWORK : DIRECT TO S3
WiFi    : CONNECTED to DISASTER_MAIN_DIRECT
```

Then C3 sends:

```text
[DIRECT] Sent to S3: NORMAL,2.1,0.30,0.50
```

or, when dangerous:

```text
[DIRECT] Sent to S3: DANGER,25.4,3.10,4.50
```

---

# 14. What S3 should show during Wi‑Fi failure

Even though the home Wi‑Fi and Mosquitto are unavailable, S3 should print:

```text
[DIRECT] Motion from C3: DANGER
| tilt=25.4
| vibration=3.10
| impact=4.50
```

Then:

```text
Motion : DANGER via DIRECT
SYSTEM : DANGER
BUZZER : ON
```

Result:

```text
RED LED ON
BUZZER ON
```

This proves the local fallback path works without the normal Wi‑Fi/MQTT path.

---

# 15. Motion thresholds

The demo firmware uses:

## Tilt

```text
< 10 deg        NORMAL
10 - <20 deg    WARNING
>= 20 deg       DANGER
```

## Vibration

```text
< 1.20 m/s2        NORMAL
1.20 - <2.50       WARNING
>= 2.50            DANGER
```

## Impact

```text
< 10 m/s2        NORMAL
>= 10 m/s2       DANGER
```

These are demo thresholds and should be calibrated with the real physical model.

---

# 16. Main system thresholds

## Temperature

```text
< 35 C        SAFE
35 - <40 C    WARNING
>= 40 C       DANGER
```

## MQ-2

```text
< 700 ADC        SAFE
700 - <1000      WARNING
>= 1000          DANGER
```

MQ-2 values here are ADC values, not ppm.

## Water level

```text
< 20 cm        SAFE
20 - <40 cm    WARNING
>= 40 cm       DANGER
```

Change the water thresholds to fit the actual height of your physical model.

---

# 17. Overall system logic

S3 receives four levels:

```text
Temperature
Gas
Water
Motion
```

Then it takes the highest level.

Example:

```text
Temperature = SAFE
Gas         = SAFE
Water       = WARNING
Motion      = DANGER
```

Result:

```text
SYSTEM = DANGER
```

Therefore:

```text
RED LED
BUZZER ON
```

---

# 18. Why UDP is used for fallback

UDP is used only for the direct C3 -> S3 backup path.

For this demo it is useful because:

```text
No broker is required.
No web server is required.
No TCP connection setup is required.
The packet is tiny.
The code is easy to explain.
```

Packet format:

```text
STATUS,TILT,VIBRATION,IMPACT
```

Example:

```text
DANGER,25.4,3.10,4.50
```

S3 reads the first value for the system state and also keeps the three motion values for display.

---

# 19. What happens when the router comes back?

The S3 continues trying to reconnect its STA interface to the home Wi‑Fi.

Therefore:

```text
S3 AP      -> still works
S3 Home WiFi -> reconnects
MQTT       -> reconnects
```

For simplicity, once C3 has entered DIRECT mode, C3 stays connected directly to S3 until C3 is rebooted.

This avoids repeatedly disconnecting C3 while trying to find the home Wi‑Fi.

After the home Wi‑Fi returns:

```text
C3 -> S3 direct path still works.
S3 -> MQTT can work again.
```

If you want C3 to return to HOME Wi‑Fi + MQTT mode, press RESET on the C3.

This is intentional in the simple demo version.

---

# 20. Recommended viva explanation

A short answer:

> Normally both ESPs use the home Wi‑Fi and MQTT. The ESP32-S3 also keeps a private Access Point active at all times. If the C3 cannot connect to the normal Wi‑Fi for five seconds, it switches to the S3 private Wi‑Fi and sends the motion state directly to the S3 using UDP. Therefore the local warning path does not depend on the router, Internet, Mosquitto, or web dashboard.

If asked why S3 uses AP + STA:

> STA lets S3 connect to the normal network and MQTT, while AP gives the C3 a direct backup network. Both roles run on the same S3.

If asked why UDP:

> The fallback packet is very small and only needs local one-way communication, so UDP keeps the implementation simple and removes the MQTT broker from the emergency path.

---

# 21. Recommended demo sequence

Do the demo in this exact order.

## Demo A — normal state

```text
Router ON
Mosquitto ON

C3 -> MQTT -> S3
```

Show:

```text
Motion = NORMAL via MQTT
```

---

## Demo B — motion warning

Tilt MPU6050 slightly.

Show:

```text
Tilt = WARNING
Motion = WARNING
Yellow LED
```

---

## Demo C — motion danger

Tilt more than approximately 20 degrees.

Show:

```text
Motion = DANGER
System = DANGER
Red LED
Buzzer ON
```

---

## Demo D — Wi‑Fi failure

Turn router/hotspot OFF.

Wait approximately 5 seconds.

Show C3:

```text
SWITCH TO DIRECT MODE
CONNECTED to DISASTER_MAIN_DIRECT
```

Show S3:

```text
Motion = ... via DIRECT
```

Move/tilt MPU6050 again.

Show:

```text
Motion = DANGER via DIRECT
Red LED
Buzzer ON
```

This is the strongest part of the demo because it proves local warning still works without the normal network.

---

# 22. Troubleshooting

## C3 cannot find MPU6050

Check:

```text
3.3V
GND
SDA = GPIO6
SCL = GPIO7
```

Also verify the selected board and I2C wiring.

---

## C3 never switches to direct mode

Check that home Wi‑Fi is actually unavailable, or that the MQTT broker is unavailable long enough.

C3 waits approximately:

```text
5 seconds for HOME WiFi failure
10 seconds for MQTT broker failure
```

before switching.

---

## C3 says direct mode but cannot connect

Use a phone/laptop to check whether this Wi‑Fi exists:

```text
DISASTER_MAIN_DIRECT
```

If it does not exist, fix the S3 AP first.

Check that SSID/password are identical in both files.

---

## S3 AP exists but S3 receives no UDP packets

Check that both code files use:

```text
UDP port = 4210
```

and that C3 reports:

```text
CONNECTED to DISASTER_MAIN_DIRECT
```

C3 must receive an IP such as:

```text
192.168.4.x
```

S3 is:

```text
192.168.4.1
```

---

## MQTT state -2

This generally means the TCP connection to the MQTT broker could not be established.

For the normal path check:

```text
MQTT_HOST
Mosquitto is running
PC firewall
PC and ESP are on the same network
Port 1883
```

The direct fallback path does not require MQTT.

---

# 23. Final mental model

Remember only this:

```text
NORMAL
C3 -> Home WiFi -> MQTT -> S3

FAILURE
C3 -> S3 WiFi -> UDP -> S3
```

And S3 always does:

```text
Local sensors
      +
Motion from C3
      |
      v
highest danger level
      |
      +----> LED
      |
      +----> BUZZER
```

---

# 24. Sensor thresholds and LED behavior

This section is the most important part to remember during the demo.

The system uses three common levels:

```text
SAFE / NORMAL = 0
WARNING       = 1
DANGER        = 2
```

The ESP32-S3 takes the **highest level among all sensors** and uses that as the overall system state.

```text
Temperature
Gas
Water
Motion
   |
   v
take the highest level
   |
   v
SYSTEM STATUS
   |
   +----> LED
   |
   +----> Buzzer
```

Therefore, the LED represents the **overall system danger level**, not only one individual sensor.

## 24.1 LED rule

| Overall system level | LED | Buzzer |
|---|---|---|
| `SAFE` / `NORMAL` | Green LED | OFF |
| `WARNING` | Yellow LED | OFF |
| `DANGER` | Red LED | ON |

In the current simplified demo firmware:

```text
SAFE    -> GREEN
WARNING -> YELLOW
DANGER  -> RED + BUZZER
```

There is no red blinking state in this simple demo version.

---

# 25. DHT11 — Temperature and Humidity

DHT11 measures:

```text
Temperature
Humidity
```

Only **temperature** participates in the warning logic.

Humidity is currently used only for monitoring and MQTT telemetry.

## 25.1 Temperature thresholds

The current demo code uses:

```cpp
TEMP_WARNING = 35.0;
TEMP_DANGER  = 40.0;
```

Therefore:

| Temperature | Level | If this is the highest system level |
|---:|---|---|
| `< 35°C` | SAFE | Green LED |
| `35°C to < 40°C` | WARNING | Yellow LED |
| `>= 40°C` | DANGER | Red LED + Buzzer |

Examples:

```text
Temperature = 30°C
-> SAFE
-> GREEN
```

```text
Temperature = 37°C
-> WARNING
-> YELLOW
```

```text
Temperature = 42°C
-> DANGER
-> RED + BUZZER
```

Important: these are **demo thresholds**, not official fire-safety standards.

## 25.2 Humidity thresholds

The current code does **not** use humidity to generate warning levels.

Example:

```text
Humidity = 50%
Humidity = 70%
Humidity = 90%
```

The system only displays/sends the value. Humidity alone does not change the LED or buzzer.

---

# 26. MQ-2 — Gas / Smoke sensor

The MQ-2 value used in this demo is an **ADC value**, not ppm.

The ESP32 ADC is configured as 12-bit, so the theoretical ADC range is:

```text
0 -> 4095
```

The current demo thresholds are:

```cpp
GAS_WARNING = 700;
GAS_DANGER  = 1000;
```

| MQ-2 ADC value | Level | If this is the highest system level |
|---:|---|---|
| `< 700` | SAFE | Green LED |
| `700 to < 1000` | WARNING | Yellow LED |
| `>= 1000` | DANGER | Red LED + Buzzer |

Examples:

```text
MQ-2 = 450
-> SAFE
-> GREEN
```

```text
MQ-2 = 820
-> WARNING
-> YELLOW
```

```text
MQ-2 = 1250
-> DANGER
-> RED + BUZZER
```

Important: `700` and `1000` are only starting values for the demo. Observe the real MQ-2 readings on your hardware and adjust if necessary. Do not call these values ppm unless the sensor has been calibrated for ppm measurement.

---

# 27. JSN-SR04T — Water level

The ultrasonic sensor first measures:

```text
distance from sensor to water surface
```

The system then calculates:

```text
waterLevel = sensorHeight - measuredDistance
```

Example:

```text
Sensor height     = 100 cm
Measured distance = 75 cm
Water level       = 100 - 75 = 25 cm
```

The current demo thresholds are:

```cpp
WATER_WARNING = 20.0;
WATER_DANGER  = 40.0;
```

| Water level | Level | If this is the highest system level |
|---:|---|---|
| `< 20 cm` | SAFE | Green LED |
| `20 cm to < 40 cm` | WARNING | Yellow LED |
| `>= 40 cm` | DANGER | Red LED + Buzzer |

Examples:

```text
Water level = 10 cm
-> SAFE
-> GREEN
```

```text
Water level = 28 cm
-> WARNING
-> YELLOW
```

```text
Water level = 47 cm
-> DANGER
-> RED + BUZZER
```

## 27.1 Distance and water level are opposite

When the water rises:

```text
distance from sensor to water DECREASES
```

but:

```text
water level INCREASES
```

Example with a 100 cm installation height:

| Distance from sensor to water | Calculated water level | Level |
|---:|---:|---|
| 90 cm | 10 cm | SAFE |
| 75 cm | 25 cm | WARNING |
| 50 cm | 50 cm | DANGER |

Remember:

```text
smaller distance
      =
higher water
      =
more dangerous
```

## 27.2 Adjust water thresholds to the real model

If your physical tank/model is not 100 cm high, change:

```cpp
SENSOR_HEIGHT_CM
```

Example, if the real model is only 25 cm high:

```cpp
SENSOR_HEIGHT_CM = 25.0;
WATER_WARNING = 7.0;
WATER_DANGER  = 15.0;
```

Then:

```text
< 7 cm       -> SAFE
7 to <15 cm  -> WARNING
>= 15 cm     -> DANGER
```

---

# 28. MPU6050 — Motion sensor

The XIAO ESP32-C3 + MPU6050 detects three types of motion:

```text
TILT
VIBRATION
IMPACT
```

The C3 calculates a level for each one and then takes the highest level as the overall **MOTION STATUS**.

That motion status is sent to the S3 by:

```text
MQTT during normal operation
```

or:

```text
UDP direct connection when normal Wi-Fi is lost
```

---

# 29. Tilt thresholds

Tilt means how much the sensor has changed angle compared with its calibrated starting position.

```cpp
TILT_WARNING = 10.0;
TILT_DANGER  = 20.0;
```

| Tilt angle | Level | If this is the highest system level |
|---:|---|---|
| `< 10°` | NORMAL | Green LED |
| `10° to < 20°` | WARNING | Yellow LED |
| `>= 20°` | DANGER | Red LED + Buzzer |

Examples:

```text
Tilt = 4°
-> NORMAL
```

```text
Tilt = 14°
-> WARNING
```

```text
Tilt = 27°
-> DANGER
```

---

# 30. Vibration thresholds

Vibration is based on the average change in acceleration compared with the baseline measured during calibration.

```cpp
VIBRATION_WARNING = 1.20;
VIBRATION_DANGER  = 2.50;
```

Unit:

```text
m/s²
```

| Vibration value | Level | If this is the highest system level |
|---:|---|---|
| `< 1.20 m/s²` | NORMAL | Green LED |
| `1.20 to < 2.50 m/s²` | WARNING | Yellow LED |
| `>= 2.50 m/s²` | DANGER | Red LED + Buzzer |

Examples:

```text
Vibration = 0.45
-> NORMAL
```

```text
Vibration = 1.70
-> WARNING
```

```text
Vibration = 3.20
-> DANGER
```

The exact measured value depends on how the sensor is mounted, so test and adjust before the presentation.

---

# 31. Impact threshold

Impact represents a short and strong acceleration spike.

The current threshold is:

```cpp
IMPACT_DANGER = 10.0;
```

| Impact value | Level |
|---:|---|
| `< 10 m/s²` | NORMAL |
| `>= 10 m/s²` | DANGER |

There is intentionally no WARNING state for impact in the simple demo.

```text
Impact = 3.5
-> NORMAL
```

```text
Impact = 12.4
-> DANGER
-> RED + BUZZER
```

For safety, do not hit the electronics directly. Tap or shake the structure to which the sensor is attached.

---

# 32. Motion overall level

The C3 independently calculates:

```text
Tilt level
Vibration level
Impact level
```

Then it takes the highest one.

Example:

```text
Tilt      = WARNING
Vibration = NORMAL
Impact    = NORMAL

MOTION = WARNING
```

Another example:

```text
Tilt      = NORMAL
Vibration = WARNING
Impact    = DANGER

MOTION = DANGER
```

The S3 then combines this motion state with its own local sensors.

---

# 33. Complete system threshold table

This is the main table to review before viva.

| Sensor / value | SAFE / NORMAL | WARNING | DANGER |
|---|---|---|---|
| Temperature | `< 35°C` | `35 to < 40°C` | `>= 40°C` |
| Humidity | Monitoring only | — | — |
| MQ-2 | `< 700 ADC` | `700 to < 1000` | `>= 1000` |
| Water level | `< 20 cm` | `20 to < 40 cm` | `>= 40 cm` |
| Tilt | `< 10°` | `10 to < 20°` | `>= 20°` |
| Vibration | `< 1.20 m/s²` | `1.20 to < 2.50` | `>= 2.50` |
| Impact | `< 10 m/s²` | — | `>= 10` |

LED rule:

| Highest level among all sensors | LED | Buzzer |
|---|---|---|
| SAFE / NORMAL | Green | OFF |
| WARNING | Yellow | OFF |
| DANGER | Red | ON |

---

# 34. Example situations

## Example A — everything normal

```text
Temperature = 30°C       SAFE
Gas         = 450 ADC    SAFE
Water       = 10 cm      SAFE
Motion      = NORMAL

SYSTEM = SAFE
```

Result:

```text
GREEN LED
BUZZER OFF
```

## Example B — only temperature warning

```text
Temperature = 37°C       WARNING
Gas         = 450 ADC    SAFE
Water       = 10 cm      SAFE
Motion      = NORMAL

SYSTEM = WARNING
```

Result:

```text
YELLOW LED
BUZZER OFF
```

## Example C — gas danger

```text
Temperature = 31°C       SAFE
Gas         = 1200 ADC   DANGER
Water       = 10 cm      SAFE
Motion      = NORMAL

SYSTEM = DANGER
```

Result:

```text
RED LED
BUZZER ON
```

## Example D — water warning and motion danger

```text
Temperature = SAFE
Gas         = SAFE
Water       = WARNING
Motion      = DANGER
```

The highest value is `DANGER`, therefore:

```text
SYSTEM = DANGER
RED LED
BUZZER ON
```

## Example E — motion danger while home Wi-Fi is OFF

```text
Home WiFi = OFF
MQTT      = OFF

C3 connects to:
DISASTER_MAIN_DIRECT

C3 sends:
DANGER,25.0,0.50,1.20
```

S3 receives:

```text
Motion = DANGER via DIRECT
```

Therefore:

```text
SYSTEM = DANGER
RED LED
BUZZER ON
```

This proves that the local emergency path still works without the normal Wi-Fi/MQTT path.

---

# 35. Best viva explanation for the LED logic

A concise explanation:

> Each sensor is first converted into a common level: SAFE, WARNING, or DANGER. The ESP32-S3 compares all sensor levels and selects the highest one as the overall system status. SAFE turns on the green LED, WARNING turns on the yellow LED, and DANGER turns on the red LED and buzzer. Therefore, one dangerous sensor is enough to put the whole system into DANGER.

Example:

```text
Temperature = SAFE
Gas         = WARNING
Water       = SAFE
Motion      = DANGER
```

Then:

```text
SYSTEM = DANGER
```

because DANGER is the highest level.

---

# 36. Important note about demo thresholds

All thresholds in this guide are intended for the simplified classroom demo firmware.

They should be experimentally adjusted based on:

```text
real sensor readings
sensor mounting
tank/model dimensions
environment
demo conditions
```

Especially:

```text
MQ-2
Water level
MPU6050 vibration
Impact
```

should be tested on the actual model before presentation.

Do not describe the demo thresholds as official safety standards.
