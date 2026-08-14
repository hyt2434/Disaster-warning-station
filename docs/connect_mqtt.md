Hướng dẫn setup Mosquitto MQTT Broker trên Windows

Tài liệu này dùng cho hệ thống Disaster Warning Station, trong đó:

Laptop chạy Mosquitto MQTT Broker

ESP32-S3 Main kết nối đến broker qua Wi-Fi

XIAO ESP32-C3 F7 kết nối đến cùng broker

Backend/website sau này cũng kết nối đến broker để nhận/gửi dữ liệu

1. Kiến trúc mạng

Các thiết bị phải kết nối vào cùng một mạng Wi-Fi/LAN:

                    Wi-Fi / LAN
                         │
          ┌──────────────┼──────────────┐
          │              │              │
          ▼              ▼              ▼
       Laptop        ESP32-S3       XIAO C3
   Mosquitto Broker     Main             F7
       port 1883

Laptop đóng vai trò MQTT Broker.

ESP32 không được dùng 127.0.0.1 làm địa chỉ broker.

127.0.0.1 trên ESP có nghĩa là chính ESP, không phải laptop.

ESP phải dùng IPv4 Wi-Fi/LAN của laptop.

2. Kiểm tra Mosquitto Service

Mở Command Prompt hoặc PowerShell.

Trong CMD:

sc query mosquitto

Nếu thấy:

STATE : 4  RUNNING

thì Mosquitto đang chạy.

Nếu muốn dùng PowerShell:

Get-Service mosquitto

Get-Service chỉ chạy trong PowerShell, không chạy trực tiếp trong CMD.

3. Kiểm tra Mosquitto đang listen ở đâu

Chạy:

netstat -ano | findstr :1883

Nếu thấy:

TCP    127.0.0.1:1883    0.0.0.0:0    LISTENING
TCP    [::1]:1883        [::]:0       LISTENING

thì Mosquitto chỉ nhận kết nối từ chính laptop.

ESP32 sẽ không kết nối được.

Mục tiêu của chúng ta là:

TCP    0.0.0.0:1883      0.0.0.0:0    LISTENING
TCP    [::]:1883         [::]:0       LISTENING

0.0.0.0:1883 nghĩa là broker đang nghe trên các IPv4 network interface của máy.

4. Dừng Mosquitto Service

Mở CMD hoặc PowerShell bằng Run as administrator.

Trong CMD:

net stop mosquitto

Kết quả mong muốn:

The Mosquitto Broker service was stopped successfully.

Kiểm tra lại:

sc query mosquitto

Mong muốn:

STATE : 1  STOPPED

Nếu gặp:

System error 5 has occurred.
Access is denied.

nghĩa là terminal chưa chạy bằng quyền Administrator.

5. Tạo file cấu hình Mosquitto

Tạo thư mục:

mkdir C:\mqtt

Mở file cấu hình:

notepad C:\mqtt\mosquitto.conf

Điền:

listener 1883
allow_anonymous true

Sau đó Save.

Ý nghĩa:

listener 1883

→ mở MQTT listener trên TCP port 1883.

allow_anonymous true

→ cho phép client kết nối mà chưa cần username/password.

Cấu hình này phù hợp cho giai đoạn phát triển và test trong LAN.

Khi đưa hệ thống lên môi trường thật hoặc mạng không đáng tin cậy, nên cấu hình username/password và TLS thay vì anonymous MQTT.

6. Chạy Mosquitto bằng file cấu hình mới

Trong CMD hoặc PowerShell:

"C:\Program Files\mosquitto\mosquitto.exe" -c "C:\mqtt\mosquitto.conf" -v

Trong đó:

-c chọn file configuration

-v bật log chi tiết

Nếu đúng, terminal sẽ xuất hiện nội dung gần giống:

Config loaded from C:\mqtt\mosquitto.conf.
Opening ipv4 listen socket on port 1883.
Opening ipv6 listen socket on port 1883.
mosquitto version ... running

Giữ cửa sổ này mở trong khi test.

Nếu đóng cửa sổ này thì broker chạy thủ công cũng sẽ dừng.

7. Kiểm tra lại port 1883

Mở một terminal khác:

netstat -ano | findstr :1883

Kết quả đúng:

TCP    0.0.0.0:1883    0.0.0.0:0    LISTENING
TCP    [::]:1883       [::]:0       LISTENING

Nếu vẫn thấy:

127.0.0.1:1883

thì broker chưa chạy bằng file config mong muốn.

8. Mở Windows Firewall cho MQTT

Mở PowerShell Run as administrator:

New-NetFirewallRule `
  -DisplayName "Mosquitto MQTT 1883" `
  -Direction Inbound `
  -Protocol TCP `
  -LocalPort 1883 `
  -Action Allow

Hoặc CMD Administrator:

netsh advfirewall firewall add rule name="Mosquitto MQTT 1883" dir=in action=allow protocol=TCP localport=1883

Kiểm tra rule bằng PowerShell:

Get-NetFirewallRule -DisplayName "Mosquitto MQTT 1883"

9. Tìm IP của laptop

Chạy:

ipconfig

Tìm đúng adapter đang dùng để kết nối Wi-Fi:

Wireless LAN adapter Wi-Fi:

Ví dụ:

IPv4 Address. . . . . . . . . . : 172.20.10.2
Subnet Mask . . . . . . . . . . : 255.255.255.240
Default Gateway . . . . . . . . : 172.20.10.1

Khi đó MQTT Broker IP là:

172.20.10.2

ESP sẽ sử dụng:

const char* MQTT_SERVER = "172.20.10.2";
const int MQTT_PORT = 1883;

Không dùng:

const char* MQTT_SERVER = "127.0.0.1";

10. Kiểm tra laptop có thể publish/subscribe qua IP LAN

Giả sử IP laptop là:

172.20.10.2

Terminal 1 — Subscriber

CMD:

"C:\Program Files\mosquitto\mosquitto_sub.exe" -h 172.20.10.2 -p 1883 -t "disaster/#" -v

Hoặc PowerShell:

& "C:\Program Files\mosquitto\mosquitto_sub.exe" `
  -h 172.20.10.2 `
  -p 1883 `
  -t "disaster/#" `
  -v

Terminal này sẽ đứng chờ message. Đây là bình thường.

Terminal 2 — Publisher

CMD:

"C:\Program Files\mosquitto\mosquitto_pub.exe" -h 172.20.10.2 -p 1883 -t "disaster/test" -m "Server OK"

Hoặc PowerShell:

& "C:\Program Files\mosquitto\mosquitto_pub.exe" `
  -h 172.20.10.2 `
  -p 1883 `
  -t "disaster/test" `
  -m "Server OK"

Subscriber phải nhận:

disaster/test Server OK

Nếu được thì broker đã hoạt động đúng ở mức cơ bản.

11. Cấu hình ESP32 Main

Trong firmware ESP32-S3 Main:

const char* MQTT_SERVER = "IP_LAPTOP";
const int MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "ESP32-MAIN";

Ví dụ:

const char* MQTT_SERVER = "172.20.10.2";
const int MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "ESP32-MAIN";

Topic test:

const char* MQTT_TOPIC = "disaster/main/test";

Message test:

Hello from MAIN

Laptop subscribe:

"C:\Program Files\mosquitto\mosquitto_sub.exe" -h 172.20.10.2 -p 1883 -t "disaster/#" -v

Mong muốn:

disaster/main/test Hello from MAIN

12. Cấu hình XIAO ESP32-C3 F7

XIAO dùng cùng broker:

const char* MQTT_SERVER = "172.20.10.2";
const int MQTT_PORT = 1883;

Nhưng phải dùng Client ID khác Main:

const char* MQTT_CLIENT_ID = "XIAO-F7";

Topic:

const char* MQTT_TOPIC = "disaster/f7/test";

Message:

Hello from F7

Mong muốn trên laptop:

disaster/f7/test Hello from F7

13. Client ID phải khác nhau

Không dùng:

MAIN → ESP32-CLIENT
F7   → ESP32-CLIENT

Nên dùng:

MAIN → ESP32-MAIN
F7   → XIAO-F7

Nếu hai thiết bị dùng cùng MQTT Client ID, broker có thể ngắt client cũ khi client mới kết nối.

14. Kết quả cuối cùng cần đạt

Subscriber laptop:

disaster/main/test Hello from MAIN
disaster/f7/test Hello from F7

Checklist:

[ ] Mosquitto cài đặt thành công
[ ] Mosquitto chạy
[ ] Port 1883 LISTENING
[ ] Broker listen tại 0.0.0.0:1883
[ ] Firewall cho phép TCP 1883
[ ] Laptop có IPv4 Wi-Fi xác định
[ ] ESP Main dùng đúng IP laptop
[ ] XIAO F7 dùng đúng IP laptop
[ ] Main và laptop cùng mạng
[ ] F7 và laptop cùng mạng
[ ] MAIN có Client ID riêng
[ ] F7 có Client ID riêng
[ ] mosquitto_sub nhận Hello from MAIN
[ ] mosquitto_sub nhận Hello from F7

15. Các lỗi thường gặp

MQTT connect failed, state = -2

Thường là lỗi ở tầng TCP/network:

ESP
 ↓
không kết nối được
 ↓
Laptop:1883

Kiểm tra:

- MQTT_SERVER có đúng IPv4 laptop không?
- Laptop và ESP có cùng Wi-Fi không?
- Mosquitto có chạy không?
- netstat có 0.0.0.0:1883 không?
- Windows Firewall đã mở TCP 1883 chưa?

ESP kết nối Wi-Fi nhưng không kết nối MQTT

Ví dụ:

Laptop: 10.126.4.68
ESP:    172.20.10.13

Hai địa chỉ này có thể đang nằm ở hai mạng khác nhau.

Cần kiểm tra lại Wi-Fi thực tế của cả hai thiết bị.

Mosquitto chỉ hiện 127.0.0.1:1883

Nếu:

netstat -ano | findstr :1883

trả về:

127.0.0.1:1883

thì broker chỉ nhận localhost.

Hãy chạy bằng:

"C:\Program Files\mosquitto\mosquitto.exe" -c "C:\mqtt\mosquitto.conf" -v

với:

listener 1883
allow_anonymous true

Error: Address already in use

Nếu chạy Mosquitto và gặp:

Error: Address already in use

kiểm tra:

netstat -ano | findstr :1883

Có thể một Mosquitto Service hoặc process khác đang chiếm port.

Kiểm tra service:

sc query mosquitto

Dừng service nếu cần:

net stop mosquitto

Access is denied khi dừng service

Nếu:

System error 5 has occurred.
Access is denied.

hãy mở terminal bằng:

Run as administrator

rồi chạy lại:

net stop mosquitto

16. Lưu ý khi phát triển firmware thật

Trong firmware test, có thể dùng vòng lặp reconnect đơn giản.

Trong firmware Main cuối cùng, không nên để mất Wi-Fi/MQTT làm block toàn bộ chương trình.

Ngay cả khi:

Wi-Fi OFF
MQTT OFF
Backend OFF

ESP Main vẫn phải tiếp tục:

- đọc DHT11
- đọc MQ-2
- đo mực nước
- điều khiển LED
- tự bật buzzer khi nguy hiểm
- tự tắt buzzer khi hết nguy hiểm

Wi-Fi/MQTT chỉ là lớp truyền thông, không phải điều kiện để hệ thống cảnh báo cục bộ hoạt động.

17. Kiến trúc sau khi setup hoàn tất

                         Wi-Fi / LAN
                              │
          ┌───────────────────┼───────────────────┐
          │                   │                   │
          ▼                   ▼                   ▼
     ESP32-S3 MAIN        XIAO C3 F7          Laptop
          │                   │                   │
          │ MQTT              │ MQTT              │
          └──────────────┬────┘                   │
                         ▼                        │
                 Mosquitto Broker ◄───────────────┘
                     TCP 1883
                         │
                         ▼
                       Backend
                         │
                         ▼
                       Website

18. Tài liệu chính thức

Eclipse Mosquitto Documentation: https://mosquitto.org/documentation/

Mosquitto migration/configuration notes: https://mosquitto.org/documentation/migrating-to-2-0/

Mosquitto authentication methods: https://mosquitto.org/documentation/authentication-methods/

mosquitto_sub manual: https://mosquitto.org/man/mosquitto_sub-1.html

mosquitto_pub manual: https://mosquitto.org/man/mosquitto_pub-1.html