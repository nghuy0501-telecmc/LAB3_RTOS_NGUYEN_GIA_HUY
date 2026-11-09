# Lab #3 - Giám sát môi trường vườn cây công nghiệp (ESP32 + FreeRTOS + MQTT)

**Họ tên:Nguyễn Gia Huy
**lớp:Kỹ sư kỹ thuật thông tin và truyền thông Khóa 64

## Mô tả
ESP32 đọc nhiệt độ, độ ẩm từ DHT22, gửi lên MQTT broker (broker.emqx.io) và hiển thị trên Web.
Điều khiển máy bơm, quạt thủ công từ Web; máy phun sương tự động khi độ ẩm dưới 40%.

## Kiến trúc FreeRTOS
| Task | Core | Nhiệm vụ |
|---|---|---|
| TaskTransmit | 0 | WiFi/MQTT, đọc `dataQueue` và publish, nhận lệnh từ Web |
| TaskRead | 1 | Đọc DHT22, in Serial, gửi `dataQueue` và `alertQueue` |
| TaskAlert | 1 | Đọc `alertQueue`, điều khiển máy phun sương bằng PWM |

## Sơ đồ chân
DHT22 SDA: GPIO22 | Bơm: GPIO17 | Quạt: GPIO18 | Phun sương: GPIO19

## Thuật toán phun sương
- 35-40%: 25% công suất, 10 s
- 30-35%: 50% công suất, 20 s
- Dưới 30%: 100% công suất, 30 s

## Cấu trúc thư mục
- `sketch.ino`, `diagram.json`, `libraries.txt`: project Wokwi
- `index.html`: giao diện Web
- `images/`: ảnh kết quả

## Link Wokwi
https://wokwi.com/projects/480203113703285761
