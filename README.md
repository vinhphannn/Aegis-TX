# Aegis-TX — Custom RC Transmitter Firmware

![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32_Dual--Core-blue)
![Radio: LoRa 433MHz](https://img.shields.io/badge/Radio-LoRa_RA--02_433MHz-orange)
![RTOS: FreeRTOS](https://img.shields.io/badge/RTOS-FreeRTOS_Dual--Core-success)

Firmware tự phát triển cho tay cầm điều khiển RC (transmitter), xây dựng trên nền ESP32 chạy FreeRTOS hai nhân, sử dụng module LoRa RA-02 ở tần số 433MHz làm lớp vật lý cho đường truyền điều khiển. Dự án bao gồm cả phần cứng transmitter, module thu (receiver) riêng, và một tùy biến trên giao thức ExpressLRS (ELRS) để hoạt động ổn định trên băng tần LoRa.

## Video demo

https://github.com/user-attachments/assets/53d7025c-c662-4c51-ad10-2ab564fe2dbc

---

## Kiến trúc hệ thống

- **Transmitter:** ESP32 hai nhân — một nhân xử lý đọc input (stick, switch) và giao tiếp radio, nhân còn lại xử lý giao diện/hiển thị và kết nối BLE, tránh tình trạng trễ input do tranh chấp tài nguyên.
- **Đường truyền chính:** Giao thức ELRS tùy biến để chạy trên module LoRa RA-02 433MHz thay vì phần cứng ELRS gốc, đảm bảo tầm xa tốt hơn ở băng tần thấp.
- **Receiver:** Module thu riêng, giải mã và xuất tín hiệu điều khiển tương thích với các flight controller sử dụng giao thức CRSF.
- **Chế độ thứ hai — BLE Gamepad:** Tay cầm có thể chuyển sang chế độ kết nối BLE trực tiếp với PC, giả lập như một gamepad chuẩn (HID), cho phép dùng để luyện tập bay trên phần mềm simulator mà không cần thông qua flight controller thực.

## Tính năng chính

1. **Xử lý input thời gian thực:** Đọc stick/switch qua ADC, lọc nhiễu, ánh xạ (mapping) kênh có thể tùy chỉnh.
2. **Tùy biến ELRS trên LoRa 433MHz:** Điều chỉnh tham số điều chế và định thời (timing) để giao thức ELRS hoạt động ổn định trên phần cứng LoRa RA-02, vốn không phải phần cứng gốc mà ELRS hỗ trợ sẵn.
3. **Chế độ kép Transmitter / BLE Gamepad:** Chuyển đổi giữa điều khiển máy bay thật (qua radio) và điều khiển giả lập trên PC (qua BLE HID) chỉ bằng một thao tác chuyển chế độ trên tay cầm.
4. **Kiến trúc dual-core FreeRTOS:** Tách biệt task input/radio và task giao diện/kết nối để đảm bảo độ trễ điều khiển ổn định.

## Hướng dẫn build & nạp firmware

```bash
git clone https://github.com/vinhphannn/Aegis-TX.git
cd Aegis-TX

# Sau khi kích hoạt ESP-IDF v5.1.6
idf.py set-target esp32
idf.py build
idf.py -p PORT flash monitor
```

## Trạng thái dự án

Đang trong quá trình hoàn thiện, các module chính (radio, receiver, BLE gamepad) đã hoạt động ổn định; đang tiếp tục cải thiện phạm vi phủ sóng và độ trễ điều khiển.

## Firmware khôi phục

`stable_checkpoint/` lưu bản firmware TX01 trước khi đổi tên thành Aegis-TX. Giữ nguyên binary và tên file trong checkpoint để bảo toàn khả năng khôi phục; firmware build mới mang tên `Aegis-TX.bin`.
